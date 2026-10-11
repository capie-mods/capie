<#
.SYNOPSIS
    Builds the offline launcher candidate (RenameAnythingLauncher.exe) from four fixed, accepted binaries.

.DESCRIPTION
    Development build script (PowerShell 7; the built launcher itself does not need it).
    Steps, all inside plugin/build/launcher-candidate:
      1. hash the protected sources and fixed inputs; every fixed input must match its pinned SHA-256,
      2. copy the bundle into runtime\ and generate manifest.generated.hpp from the copies,
      3. compile the launcher and its component test (output tee'd to build.log), run the test,
      4. run read-only CLI cases (exit codes only; no game, injector or UI is started),
      5. check the embedded application manifest requests no elevation,
      6. check that no protected file changed and that the build had no warnings,
      7. write build-review.json.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. "Built offline launcher candidate only: <folder>".

.EXAMPLE
    pwsh -File .\plugin\launcher\build.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param([string]$VisualStudioDir = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'This development build script requires PowerShell 7; the built launcher does not.' }
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'NativeBuild.psm1') -Force

# --- Constants ---------------------------------------------------------------
$script:LauncherExeName = 'RenameAnythingLauncher.exe'
$script:LauncherTestExeName = 'launcher_test.exe'
$script:GameSha256 = '7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5'
$script:CliTimeoutMilliseconds = 10000
$script:LauncherFlags = @(
    '/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/std:c++20', '/EHsc', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE'
)
$script:LauncherLibraries = @('bcrypt.lib', 'user32.lib', 'shell32.lib', 'ole32.lib', 'comdlg32.lib')
$script:LauncherLinkOptions = @('/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED', "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
$script:LauncherTestLibraries = @('bcrypt.lib', 'shell32.lib')
$script:WarningPattern = '\bwarning\s+[A-Z]+[0-9]+'
$script:AsInvokerPattern = 'level="asInvoker"'
$script:FixedInputs = @(
    @{ source = 'plugin/build/country-capability/rename_anything_country.dll'; name = 'rename_anything_country.dll'
        sha = '18841A6D7552224F0D34BBC05983916912969735109A548C16DE4581845E10B5'; dll = $true
    },
    @{ source = 'plugin/build/country-capability/rename_anything_injector.exe'; name = 'rename_anything_injector.exe'
        sha = 'DF8D60E9F1C403193DAC79FE5229793569E30D01D17F5172C5B11F1CB14063C7'; dll = $false
    },
    @{ source = 'plugin/build/inline-editor-render-bridge/rename_anything_inline_editor_bridge.dll'; name = 'rename_anything_inline_editor_bridge.dll'
        sha = 'B20FBE88756F7F22D989230D6D51953D82BCCD2349CD00BDCC545C382DF8BAF6'; dll = $true
    },
    @{ source = 'plugin/build/inline-editor-render-bridge/inject_inline_editor_render_bridge.exe'; name = 'inject_inline_editor_render_bridge.exe'
        sha = '31B510BECD5DCBA3588231C8312C32B1F2C2CC49B0CAFC8BBB2A51B18F455AE4'; dll = $false
    }
)
$script:ProtectedSources = @(
    'plugin/src/injector.cpp', 'plugin/src/image_guard.hpp', 'plugin/src/country.cpp', 'plugin/src/country_capability.hpp', 'plugin/build.ps1'
)
$script:ReviewLimits = @(
    'Fresh runtime startup timing/playset integration PENDING', 'WaitForInputIdle is not menu readiness proof',
    'Failure guardian can remain for a hung already-created helper', 'Accepted native product/focus/Enter remain separate'
)

# --- Paths -----------------------------------------------------------------------
$sourceDir = $PSScriptRoot
$repoDir = [IO.Path]::GetFullPath((Join-Path $sourceDir '..\..'))
$outputDir = Join-Path $repoDir 'plugin\build\launcher-candidate'
$runtimeDir = Join-Path $outputDir 'runtime'
$buildLog = Join-Path $outputDir 'build.log'
$baselinePath = Join-Path $repoDir 'probes/rn_inline_editor_product_qa/source-baseline.json'

# --- Adapter (test seam) -----------------------------------------------------------
# StartReadOnlyCli(Executable, Arguments, TimeoutMs) -> @{ ExitCode; StdOut; StdErr }. Never kills the child.
$script:StartReadOnlyCli = {
    param([string]$Executable, [string[]]$Arguments, [int]$TimeoutMilliseconds)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $child = [Diagnostics.Process]::Start($start)
    try {
        if (-not $child.WaitForExit($TimeoutMilliseconds)) { throw 'Read-only CLI timed out; not terminated. No GUI/game launch is permitted in these tests.' }
        @{ ExitCode = $child.ExitCode; StdOut = $child.StandardOutput.ReadToEnd(); StdErr = $child.StandardError.ReadToEnd() }
    } finally { $child.Dispose() }
}

function Get-ProtectedName {
    <#
    .SYNOPSIS
        Lists the repository files that must not change during the build.
    .DESCRIPTION
        Protected sources, the fixed inputs and, when present, every file listed in
        probes/rn_inline_editor_product_qa/source-baseline.json; sorted and unique.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        $protectedNames = Get-ProtectedName
    #>
    [CmdletBinding()]
    [OutputType([string[]], [object[]])]
    param()
    $names = @($script:ProtectedSources) + @($script:FixedInputs | ForEach-Object source)
    if (Test-Path -LiteralPath $baselinePath) {
        $names += @((Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json).PSObject.Properties.Name)
    }
    return @($names | Sort-Object -Unique)
}

function Get-ProtectedHash {
    <#
    .SYNOPSIS
        SHA-256 of every protected file, in a stable order.
    .DESCRIPTION
        Taken before and after the build to prove nothing protected changed.
    .PARAMETER Names
        Repository-relative paths.
    .OUTPUTS
        System.Collections.Specialized.OrderedDictionary
    .EXAMPLE
        $before = Get-ProtectedHash -Names $protectedNames
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Specialized.OrderedDictionary])]
    param([Parameter(Mandatory)][string[]]$Names)
    $answer = [ordered]@{}
    foreach ($name in $Names) { $answer[$name] = (Get-FileHash -LiteralPath (Join-Path $repoDir $name) -Algorithm SHA256).Hash }
    return $answer
}

function New-BundleManifestHeader {
    <#
    .SYNOPSIS
        Generates the text of manifest.generated.hpp from the copied bundle.
    .DESCRIPTION
        Lists the game hash and, per bundle file, its name, lower-case SHA-256 and whether it is a DLL.
    .PARAMETER RuntimeDirectory
        Folder with the copied bundle files.
    .OUTPUTS
        System.String
    .EXAMPLE
        New-BundleManifestHeader -RuntimeDirectory $runtimeDir
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][string]$RuntimeDirectory)
    $lines = @('#pragma once', '// Build-generated from four fixed, accepted binaries only.', 'namespace rnl::manifest {',
        ('inline constexpr char game_sha256[]="' + $script:GameSha256 + '";'),
        ('inline constexpr std::array<BundleEntry,' + $script:FixedInputs.Count + '> files={{'))
    foreach ($entry in $script:FixedInputs) {
        $digest = (Get-FileHash -LiteralPath (Join-Path $RuntimeDirectory $entry.name) -Algorithm SHA256).Hash.ToLowerInvariant()
        $isDll = if ($entry.dll) { 'true' } else { 'false' }
        $lines += ' {L"' + $entry.name + '","' + $digest + '",' + $isDll + '},'
    }
    $lines += '}};', '}'
    return ($lines -join "`n") + "`n"
}

function Invoke-ReadOnlyCli {
    <#
    .SYNOPSIS
        Runs the launcher with arguments that must not start anything and checks the exit code.
    .DESCRIPTION
        Records every case in $script:CliResults for build-review.json.
    .PARAMETER Executable
        Launcher copy to run.
    .PARAMETER Arguments
        Command-line arguments.
    .PARAMETER Expected
        Expected exit code.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-ReadOnlyCli -Executable $exe -Arguments @('--help') -Expected 0
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][int]$Expected
    )
    $r = & $script:StartReadOnlyCli $Executable $Arguments $script:CliTimeoutMilliseconds
    $script:CliResults.Add([ordered]@{
            executable = $Executable; arguments = $Arguments; exit = $r.ExitCode; expected = $Expected
            stdout = ([string]$r.StdOut).Trim(); stderr = ([string]$r.StdErr).Trim()
        })
    if ($r.ExitCode -ne $Expected) { throw "CLI expected $Expected, observed $($r.ExitCode): $($r.StdErr)" }
}

# 1. Fixed inputs and protected files.
$protectedNames = Get-ProtectedName
$before = Get-ProtectedHash -Names $protectedNames
foreach ($entry in $script:FixedInputs) {
    if ($before[$entry.source] -ne $entry.sha) { throw "Fixed accepted input mismatch: $($entry.source)" }
}
# 2. Bundle copy and generated manifest.
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
foreach ($entry in $script:FixedInputs) {
    Copy-Item -LiteralPath (Join-Path $repoDir $entry.source) -Destination (Join-Path $runtimeDir $entry.name) -Force
    if ((Get-FileHash -LiteralPath (Join-Path $runtimeDir $entry.name) -Algorithm SHA256).Hash -ne $entry.sha) { throw 'Bundle copy mismatch.' }
}
Copy-Item -LiteralPath (Join-Path $repoDir 'plugin/LICENSE.MinHook.txt') -Destination (Join-Path $outputDir 'LICENSE.MinHook.txt') -Force
$manifestHeader = New-BundleManifestHeader -RuntimeDirectory $runtimeDir
[IO.File]::WriteAllText((Join-Path $outputDir 'manifest.generated.hpp'), $manifestHeader, [Text.UTF8Encoding]::new($false))
$vsArgs = @{
    VisualStudioDir = $VisualStudioDir
    MissingMessage  = 'Visual Studio C++ x64 tools are required to BUILD only.'
    FailureMessage  = 'vcvars64 failed.'
}
[void](Import-NbVisualStudioEnvironment @vsArgs)
$script:CliResults = [Collections.Generic.List[object]]::new()
Push-Location $outputDir
try {
    # 3. Compile and test.
    $launcherArgs = New-NbCompilerArgument -Flags $script:LauncherFlags -CppFlags @() -Source (Join-Path $sourceDir 'launcher.cpp') -IncludeDir $outputDir `
        -LinkLibrary $script:LauncherLibraries -LinkOption $script:LauncherLinkOptions -OutFile $script:LauncherExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $launcherArgs -FailureMessage 'Launcher compilation failed.' -LogPath $buildLog
    $testArgs = New-NbCompilerArgument -Flags $script:LauncherFlags -CppFlags @() -Source (Join-Path $sourceDir 'launcher_test.cpp') `
        -LinkLibrary $script:LauncherTestLibraries -OutFile $script:LauncherTestExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $testArgs -FailureMessage 'Launcher test compilation failed.' -LogPath $buildLog -AppendLog
    Invoke-NbTool -FilePath ('.\' + $script:LauncherTestExeName) -ArgumentList @((Join-Path $outputDir 'component-fixtures')) `
        -FailureMessage 'Launcher component tests failed.' -LogPath $buildLog -AppendLog
    # 4. Read-only CLI cases.
    $exe = Join-Path $outputDir $script:LauncherExeName
    $cliDir = Join-Path $outputDir 'cli-fixtures'
    $emptyDir = Join-Path $cliDir 'missing-bundle'
    New-Item -ItemType Directory -Force -Path $emptyDir | Out-Null
    $wrongHost = Join-Path $cliDir 'hoi4.exe'
    Copy-Item -LiteralPath $exe -Destination $wrongHost -Force
    $emptyExe = Join-Path $emptyDir $script:LauncherExeName
    Copy-Item -LiteralPath $exe -Destination $emptyExe -Force
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--help') -Expected 0
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--game', $wrongHost, '--verify-only') -Expected 10
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--game', (Join-Path $cliDir 'does-not-exist\hoi4.exe'), '--verify-only') -Expected 10
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--game', 'hoi4.exe', '--verify-only') -Expected 10
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--verify-only') -Expected 2
    Invoke-ReadOnlyCli -Executable $exe -Arguments @('--game', $wrongHost, '--verify-only', '--dll', 'arbitrary.dll') -Expected 2
    Invoke-ReadOnlyCli -Executable $emptyExe -Arguments @('--game', $wrongHost, '--verify-only') -Expected 20
    Invoke-ReadOnlyCli -Executable $emptyExe -Arguments @('--help') -Expected 0
    # 5. No elevation request in the embedded application manifest.
    $embeddedManifest = Join-Path $outputDir 'launcher.embedded.manifest'
    Invoke-NbTool -FilePath 'mt.exe' -ArgumentList @('-nologo', ('-inputresource:' + $exe + ';#1'), ('-out:' + $embeddedManifest)) `
        -FailureMessage 'Cannot inspect own application manifest.' -LogPath $buildLog -AppendLog
    if ((Get-Content -LiteralPath $embeddedManifest -Raw) -notmatch $script:AsInvokerPattern) { throw 'Launcher must not request elevation.' }
} finally { Pop-Location }
# 6. Protected files unchanged, no warnings.
$after = Get-ProtectedHash -Names $protectedNames
foreach ($name in $protectedNames) { if ($before[$name] -ne $after[$name]) { throw "Protected file changed: $name" } }
if (Select-String -LiteralPath $buildLog -Pattern $script:WarningPattern -Quiet) { throw 'Own build warnings are not accepted.' }
# 7. Review record.
$sourceHashes = [ordered]@{}
foreach ($sourceFile in (Get-ChildItem -LiteralPath $sourceDir -File | Sort-Object Name)) {
    $sourceHashes[$sourceFile.Name] = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
}
$report = [ordered]@{
    date = [DateTimeOffset]::Now.ToString('o'); status = 'Built offline; no HOI4/injector/gameplay/UI activation'; runtime_accepted = $false
    warnings = 0; cli_readonly_cases = $script:CliResults; source_hashes = $sourceHashes
    launcher_sha256 = (Get-FileHash -LiteralPath (Join-Path $outputDir $script:LauncherExeName) -Algorithm SHA256).Hash
    protected_count = $protectedNames.Count; protected_before = $before; protected_after = $after; fixed_bundle = $script:FixedInputs
    limits = $script:ReviewLimits
}
[IO.File]::WriteAllText((Join-Path $outputDir 'build-review.json'), ($report | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Built offline launcher candidate only: $outputDir"
