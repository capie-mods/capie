<#
.SYNOPSIS
    Builds the CAPIE launcher (RenameAnythingLauncher.exe, wrapper mode) with the reviewed CAPIE binaries and runs its offline tests.

.DESCRIPTION
    Output: plugin/build/capie-launcher/ (the accepted plugin/build/launcher-candidate is not touched).
      1. Every input binary (the CAPIE 1.5 release build plugin/build/capie-v15-dev/capie_core.dll,
         capie_editor.dll, capie_injector.exe; never debug/ or a console build) must match the
         SHA-256 recorded in plugin/capie/deploy/DEPLOYMENT.md.
      2. The inputs are copied into runtime\ and launcher_manifest.generated.hpp is written from
         the copies (name, lower-case SHA-256, DLL or EXE).
      3. The launcher (/SUBSYSTEM:WINDOWS, asInvoker), fake_process.exe and both test suites are
         compiled with /MT /W4 /WX /std:c++20 /permissive-; warnings fail the build.
      4. launcher_unit_test and launcher_flow_test run against scratch folders and fake processes.
      5. The real launcher runs read-only CLI cases and wrapper cases against fakes in a scratch
         folder, with LOCALAPPDATA pointed at that folder (no dialog: --no-dialogs).
      6. The embedded manifest must request asInvoker; compile_commands.json is written for clang-tidy.
      7. build-review.json and SHA256SUMS.txt are written.
    Never starts HOI4, Steam, the Paradox Launcher or a real injector. The real hoi4.exe, when
    present, is only hashed (--verify-only), like the CAPIE build's static checks.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER GameExe
    Real hoi4.exe for the read-only --verify-only case. Missing file = case skipped (reported).

.OUTPUTS
    System.String. Summary lines.

.EXAMPLE
    pwsh -File .\plugin\capie\build_launcher.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [string]$VisualStudioDir = '',
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required to build (not to run the launcher).' }
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'NativeBuild.psm1') -Force

# --- Constants ---------------------------------------------------------------
$script:LauncherVersion = '1.0.0-dev'
$script:LauncherExeName = 'RenameAnythingLauncher.exe'
$script:CppFlags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/DUNICODE',
    '/D_UNICODE', '/std:c++20', '/EHsc', '/permissive-')
$script:LauncherLibraries = @('bcrypt.lib', 'user32.lib', 'shell32.lib', 'ole32.lib', 'comdlg32.lib', 'advapi32.lib')
$script:LauncherLinkOptions = @('/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED', "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
$script:FakeLinkOptions = @('/SUBSYSTEM:WINDOWS', '/ENTRY:wmainCRTStartup')
$script:WarningPattern = '\bwarning\s+[A-Z]+[0-9]+'
$script:AsInvokerPattern = 'level="asInvoker"'
$script:CliTimeoutMilliseconds = 60000
# Reviewed CAPIE stage-2 release binaries (plugin/capie/deploy/DEPLOYMENT.md). Order = BundleManifest order.
$script:BundleInputs = @(
    @{ source = 'plugin/build/capie-v15-dev/capie_core.dll'; name = 'capie_core.dll'; dll = $true
        sha = '7A1C946F8A15F6120A0232453E3013B9F854EC2A6B526D0AB9C86F19959D0DE1'
    },
    @{ source = 'plugin/build/capie-v15-dev/capie_editor.dll'; name = 'capie_editor.dll'; dll = $true
        sha = '0DAD2732552305F43E1E87FB6E10E6A5B473E683EA27F1E82D0EBD9B8934D656'
    },
    @{ source = 'plugin/build/capie-v15-dev/capie_injector.exe'; name = 'capie_injector.exe'; dll = $false
        sha = 'EB5E9FF5F9B944B55CA5EB4B343D18D507584284FB60F5F119A8BE8843484C25'
    }
)
# The accepted launcher candidate stays the reference; the build proves it did not change it.
$script:ProtectedNames = @('plugin/launcher/launcher.cpp', 'plugin/launcher/launcher_core.hpp',
    'plugin/launcher/launcher_test.cpp', 'plugin/launcher/build.ps1', 'plugin/launcher/README.md',
    'plugin/build/launcher-candidate/RenameAnythingLauncher.exe')

# --- Paths ---------------------------------------------------------------------
$repoDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceDir = Join-Path $PSScriptRoot 'src\launcher'
$testDir = Join-Path $PSScriptRoot 'tests\launcher'
$outputDir = Join-Path $repoDir 'plugin\build\capie-launcher'
$runtimeDir = Join-Path $outputDir 'runtime'
$testOutDir = Join-Path $outputDir 'tests'
$scratchDir = Join-Path ([IO.Path]::GetTempPath()) 'capie-launcher-tests'
$buildLog = Join-Path $outputDir 'build.log'

function Get-Sha256 {
    <#
    .SYNOPSIS
        Upper-case SHA-256 of a file.
    .PARAMETER Path
        File.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-Sha256 'C:\x\a.dll'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function New-LauncherManifestHeader {
    <#
    .SYNOPSIS
        Text of launcher_manifest.generated.hpp from the copied bundle.
    .PARAMETER RuntimeDirectory
        Folder with the copies.
    .OUTPUTS
        System.String
    .EXAMPLE
        New-LauncherManifestHeader -RuntimeDirectory $runtimeDir
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][string]$RuntimeDirectory)
    $lines = @('#pragma once', '// Generated by plugin/capie/build_launcher.ps1 from the reviewed CAPIE binaries. Do not edit.',
        '#include <array>', '', 'namespace capie::launcher::manifest {', '',
        '/// @brief One pinned bundle file.', 'struct ManifestEntry {', '    const wchar_t* name;  ///< file name in the runtime folder',
        '    const char* sha256;   ///< lower-case SHA-256', '    bool dll;             ///< DLL (else EXE)', '};', '',
        '/// @brief Bundle files in BundleManifest order (core, editor, injector).',
        ('inline constexpr std::array<ManifestEntry, ' + $script:BundleInputs.Count + '> bundle_files = {{'))
    foreach ($entry in $script:BundleInputs) {
        $digest = (Get-Sha256 (Join-Path $RuntimeDirectory $entry.name)).ToLowerInvariant()
        $isDll = if ($entry.dll) { 'true' } else { 'false' }
        $lines += ('    {L"' + $entry.name + '", "' + $digest + '", ' + $isDll + '},')
    }
    $lines += '}};', '', '/// @brief Launcher build version.', ('inline constexpr wchar_t launcher_version[] = L"' + $script:LauncherVersion + '";'),
        '', '}  // namespace capie::launcher::manifest'
    return ($lines -join "`n") + "`n"
}

function Invoke-Cli {
    <#
    .SYNOPSIS
        Runs the built launcher with arguments and an environment, waits (bounded) and checks the exit code.
    .PARAMETER Executable
        Launcher path.
    .PARAMETER Arguments
        Raw argument string (passed verbatim, as Steam would).
    .PARAMETER Expected
        Expected exit code.
    .PARAMETER Environment
        Extra environment variables.
    .OUTPUTS
        System.Collections.Specialized.OrderedDictionary. The recorded case.
    .EXAMPLE
        Invoke-Cli -Executable $exe -Arguments '--help' -Expected 0
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Specialized.OrderedDictionary])]
    param(
        [Parameter(Mandatory)][string]$Executable,
        [AllowEmptyString()][string]$Arguments,
        [Parameter(Mandatory)][int]$Expected,
        [hashtable]$Environment = @{}
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($key in $Environment.Keys) { $start.Environment[$key] = [string]$Environment[$key] }
    $child = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        if (-not $child.WaitForExit($script:CliTimeoutMilliseconds)) { throw "CLI case timed out (not terminated): $Arguments" }
        $case = [ordered]@{ arguments = $Arguments; exit = $child.ExitCode; expected = $Expected
            stdout = $stdout.Result.Trim(); stderr = $stderr.Result.Trim()
        }
    } finally { $child.Dispose() }
    $script:CliResults.Add($case)
    if ($case.exit -ne $Expected) { throw "CLI '$Arguments' expected $Expected, observed $($case.exit): $($case.stderr)" }
    return $case
}

# 1. Inputs and protected files.
$before = [ordered]@{}
foreach ($name in $script:ProtectedNames) { $before[$name] = Get-Sha256 (Join-Path $repoDir $name) }
foreach ($entry in $script:BundleInputs) {
    if ((Get-Sha256 (Join-Path $repoDir $entry.source)) -ne $entry.sha) { throw "CAPIE input differs from DEPLOYMENT.md: $($entry.source)" }
}
# 2. Bundle copy and generated manifest.
foreach ($dir in $outputDir, $runtimeDir, $testOutDir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
foreach ($entry in $script:BundleInputs) {
    $target = Join-Path $runtimeDir $entry.name
    Copy-Item -LiteralPath (Join-Path $repoDir $entry.source) -Destination $target -Force
    if ((Get-Sha256 $target) -ne $entry.sha) { throw "Bundle copy mismatch: $($entry.name)" }
}
Copy-Item -LiteralPath (Join-Path $repoDir 'plugin/build/capie-v15-dev/LICENSE.MinHook.txt') -Destination (Join-Path $outputDir 'LICENSE.MinHook.txt') -Force
[IO.File]::WriteAllText((Join-Path $outputDir 'launcher_manifest.generated.hpp'), (New-LauncherManifestHeader -RuntimeDirectory $runtimeDir),
    [Text.UTF8Encoding]::new($false))
[void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
$script:CliResults = [Collections.Generic.List[object]]::new()

# 3. Compile.
Push-Location $outputDir
try {
    $arguments = New-NbCompilerArgument -Flags $script:CppFlags -CppFlags @() -Source (Join-Path $sourceDir 'launcher_main.cpp') -IncludeDir $outputDir `
        -LinkLibrary $script:LauncherLibraries -LinkOption $script:LauncherLinkOptions -OutFile $script:LauncherExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Launcher compilation failed.' -LogPath $buildLog
} finally { Pop-Location }
Push-Location $testOutDir
try {
    $arguments = New-NbCompilerArgument -Flags $script:CppFlags -CppFlags @() -Source (Join-Path $testDir 'fake_process.cpp') `
        -LinkLibrary @('user32.lib') -LinkOption $script:FakeLinkOptions -OutFile 'fake_process.exe'
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Fake process compilation failed.' -LogPath $buildLog -AppendLog
    foreach ($suite in 'launcher_unit_test', 'launcher_flow_test') {
        $arguments = New-NbCompilerArgument -Flags $script:CppFlags -CppFlags @() -Source (Join-Path $testDir "$suite.cpp") `
            -LinkLibrary $script:LauncherLibraries -OutFile "$suite.exe"
        Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage "$suite compilation failed." -LogPath $buildLog -AppendLog
    }
    # 4. Offline tests.
    if (Test-Path -LiteralPath $scratchDir) { Remove-Item -LiteralPath $scratchDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $scratchDir | Out-Null
    $fake = Join-Path $testOutDir 'fake_process.exe'
    $script:SuiteResults = [ordered]@{}
    foreach ($suite in 'launcher_unit_test', 'launcher_flow_test') {
        $lines = & (Join-Path $testOutDir "$suite.exe") (Join-Path $scratchDir $suite) $fake 2>&1
        $code = $LASTEXITCODE
        $lines | Add-Content -LiteralPath $buildLog
        $summary = [string]($lines | Select-Object -Last 1)
        $script:SuiteResults[$suite] = $summary
        Write-Output "  $suite`: $summary"
        if ($code -ne 0) { $lines | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }; throw "$suite failed (exit $code)" }
    }
} finally { Pop-Location }

# 5. CLI cases with the real launcher (fakes only; LOCALAPPDATA = scratch).
$exe = Join-Path $outputDir $script:LauncherExeName
$cliDir = Join-Path $scratchDir 'cli'
$gameDir = Join-Path $cliDir 'Hearts of Iron IV'
$appData = Join-Path $cliDir 'appdata'
foreach ($dir in $cliDir, $gameDir, $appData) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
foreach ($name in 'dowser.exe', 'hoi4.exe', 'relay.exe') { Copy-Item -LiteralPath $fake -Destination (Join-Path $gameDir $name) -Force }
$emptyDir = Join-Path $cliDir 'no-bundle'
New-Item -ItemType Directory -Force -Path $emptyDir | Out-Null
$emptyExe = Join-Path $emptyDir $script:LauncherExeName
Copy-Item -LiteralPath $exe -Destination $emptyExe -Force
$record = Join-Path $cliDir 'record.txt'
$fakeEnv = @{ LOCALAPPDATA = $appData; FAKE_RECORD = $record; FAKE_MARK = 'cli-mark'; FAKE_GAME_LIFETIME_MS = '1500' }
$fakeHoi4 = Join-Path $gameDir 'hoi4.exe'
$fakeDowser = Join-Path $gameDir 'dowser.exe'
[void](Invoke-Cli -Executable $exe -Arguments '--help' -Expected 0)
[void](Invoke-Cli -Executable $exe -Arguments "--game `"$fakeHoi4`" --verify-only" -Expected 10)
[void](Invoke-Cli -Executable $exe -Arguments "--game `"$cliDir\missing\hoi4.exe`" --verify-only" -Expected 10)
[void](Invoke-Cli -Executable $exe -Arguments '--game hoi4.exe --verify-only' -Expected 10)
[void](Invoke-Cli -Executable $exe -Arguments '--verify-only' -Expected 2)
[void](Invoke-Cli -Executable $exe -Arguments "--game `"$fakeHoi4`" --verify-only --dll x.dll" -Expected 2)
[void](Invoke-Cli -Executable $emptyExe -Arguments "--game `"$fakeHoi4`" --verify-only" -Expected 20)
if (Test-Path -LiteralPath $GameExe) {
    [void](Invoke-Cli -Executable $exe -Arguments "--game `"$GameExe`" --verify-only" -Expected 0)
} else { Write-Output '  (real hoi4.exe not found: positive --verify-only case skipped)' }
# Wrapper refusals: nothing may start.
[void](Invoke-Cli -Executable $exe -Arguments "--no-dialogs `"$env:ComSpec`" /c exit 0" -Expected 2 -Environment $fakeEnv)
[void](Invoke-Cli -Executable $exe -Arguments "--no-dialogs `"$cliDir\missing\dowser.exe`"" -Expected 2 -Environment $fakeEnv)
[void](Invoke-Cli -Executable $exe -Arguments "--no-dialogs relative\dowser.exe" -Expected 2 -Environment $fakeEnv)
if (Test-Path -LiteralPath $record) { throw 'A refused wrapper command started a process.' }
# Wrapper with fakes: the fake game hash is unknown, so the game runs without CAPIE and the launcher waits for it.
$tail = ' "-- " -debug_mode "a\"b"'
$case = Invoke-Cli -Executable $exe -Arguments ("--no-dialogs `"$fakeDowser`"" + $tail) -Expected 0 -Environment $fakeEnv
$records = @(Get-Content -LiteralPath $record -Encoding utf8)
$dowserLine = @($records | Where-Object { $_ -like 'dowser|*' })
$gameLine = @($records | Where-Object { $_ -like 'hoi4|*' })
if ($dowserLine.Count -ne 1 -or $dowserLine[0].Split('|')[2] -ne ("`"$fakeDowser`"" + $tail)) { throw "Wrapped command line changed: $dowserLine" }
if ($dowserLine[0].Split('|')[4] -ne 'cli-mark') { throw 'Environment not inherited.' }
if ($gameLine.Count -ne 1) { throw 'Fake game did not run exactly once.' }
if (@($records | Where-Object { $_ -like 'injector|*' }).Count -ne 0) { throw 'Injector ran for an unknown game build.' }
$launcherLog = Get-Content -LiteralPath (Join-Path $appData 'RenameAnything\launcher.log') -Raw -Encoding utf8
foreach ($expected in 'unknown build', 'notice: ', 'started tree ended') {
    if ($launcherLog -notmatch [regex]::Escape($expected)) { throw "launcher.log lacks '$expected'" }
}
# Shortcut mode with an explicit game: starts the fake Paradox Launcher, remembers the path.
Remove-Item -LiteralPath $record -Force
[void](Invoke-Cli -Executable $exe -Arguments "--no-dialogs --game `"$fakeHoi4`"" -Expected 0 -Environment $fakeEnv)
if (@(Get-Content -LiteralPath $record | Where-Object { $_ -like 'hoi4|*' }).Count -ne 1) { throw 'Shortcut mode did not start the game.' }
if (-not (Test-Path -LiteralPath (Join-Path $appData 'RenameAnything\hoi4.path'))) { throw 'Shortcut mode did not remember the game.' }
Write-Output "  CLI cases: $($script:CliResults.Count) passed"

# 6. asInvoker, warnings, compile database.
$embedded = Join-Path $outputDir 'launcher.embedded.manifest'
Invoke-NbTool -FilePath 'mt.exe' -ArgumentList @('-nologo', ('-inputresource:' + $exe + ';#1'), ('-out:' + $embedded)) `
    -FailureMessage 'Cannot read the embedded manifest.' -LogPath $buildLog -AppendLog
if ((Get-Content -LiteralPath $embedded -Raw) -notmatch $script:AsInvokerPattern) { throw 'Launcher must not request elevation.' }
if (Select-String -LiteralPath $buildLog -Pattern $script:WarningPattern -Quiet) { throw 'Compiler warnings are not accepted.' }
$tidyUnits = @((Join-Path $sourceDir 'launcher_main.cpp')) + @('launcher_unit_test.cpp', 'launcher_flow_test.cpp', 'fake_process.cpp' |
        ForEach-Object { Join-Path $testDir $_ })
$database = foreach ($file in $tidyUnits) {
    [ordered]@{ directory = $outputDir; file = $file
        command = ('clang-cl /std:c++20 /EHsc /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE "/I' + $outputDir + '" -c "' + $file + '"')
    }
}
[IO.File]::WriteAllText((Join-Path $outputDir 'compile_commands.json'), (ConvertTo-Json @($database) -Depth 3), [Text.UTF8Encoding]::new($false))

# 7. Protected files, review record, checksums.
foreach ($name in $script:ProtectedNames) {
    if ((Get-Sha256 (Join-Path $repoDir $name)) -ne $before[$name]) { throw "Protected file changed: $name" }
}
$sources = [ordered]@{}
foreach ($file in @(Get-ChildItem -LiteralPath $sourceDir -File) + @(Get-ChildItem -LiteralPath $testDir -File) + @(Get-Item -LiteralPath $PSCommandPath)) {
    $sources[[IO.Path]::GetRelativePath($repoDir, $file.FullName).Replace('\', '/')] = Get-Sha256 $file.FullName
}
$shipped = [ordered]@{ $script:LauncherExeName = Get-Sha256 $exe; 'LICENSE.MinHook.txt' = Get-Sha256 (Join-Path $outputDir 'LICENSE.MinHook.txt') }
foreach ($entry in $script:BundleInputs) { $shipped['runtime/' + $entry.name] = Get-Sha256 (Join-Path $runtimeDir $entry.name) }
$review = [ordered]@{
    date = [DateTimeOffset]::Now.ToString('o'); version = $script:LauncherVersion
    status = 'Built offline; fakes only; no HOI4, Steam, Paradox Launcher or real injector started'
    runtime_accepted = $false; warnings = 0; suites = $script:SuiteResults; cli_cases = $script:CliResults
    shipped = $shipped; sources = $sources; protected_unchanged = $before
}
[IO.File]::WriteAllText((Join-Path $outputDir 'build-review.json'), ($review | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
$sums = foreach ($key in $shipped.Keys) { "$($shipped[$key])  $key" }
[IO.File]::WriteAllText((Join-Path $outputDir 'SHA256SUMS.txt'), (($sums -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "Built CAPIE launcher (offline, not runtime-accepted): $outputDir"
