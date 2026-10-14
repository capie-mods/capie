<#
.SYNOPSIS
    Mutation check of the CAPIE caption core, focus release, editor bridge, bounded reader and public contract, encoder via public_names_test and the country and faction name layers (120 mutants).

.DESCRIPTION
    Reads tests/mutants.json: each mutant is one exact, unique textual replacement in one
    source file plus the suite that must kill it. For every mutant the src/ and tests/ trees
    (C++ only) are copied to a short working folder (-WorkRoot\<number>; the MSVC compiler
    fails on long include paths), the replacement is applied, the suite is compiled from the
    copy and run WITHOUT the EXE argument (no file or process access).

    Before any mutant, every suite is compiled and run unmutated in the same folder layout
    for every flavor; it must report 0 failures, otherwise the script stops (infrastructure
    problem, not a kill). A compile error is a kill only when it is not an include/path
    error (C1083 or C1041 stop the script). Editor-bridge and public-editor mutants run in both flavors
    (CAPIE_DEBUG_DIAGNOSTICS=1 and 0) and are killed only when killed in both; the
    caption, focus, memory and public-keys suites do not depend on the flavor and run once.
    A mutant is killed by a compile error, failing checks, a crash or the timeout. Any
    survivor fails the script. Never touches a game process.

.PARAMETER MinHookDir
    MinHook source folder.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER TimeoutSeconds
    Per-run timeout.

.PARAMETER Only
    Optional mutant name filter (wildcards).

.PARAMETER WorkRoot
    Short working folder for the mutant copies (default %TEMP%\capie-mut).

.PARAMETER MutantsFile
    Mutant list relative to this folder (default tests\mutants.json). The console-command
    candidate keeps its own list in tests\console\mutants_console.json.

.OUTPUTS
    System.String. One line per mutant and flavor, and a summary line.

.EXAMPLE
    .\plugin\capie\mutation_check.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [string]$MinHookDir = 'C:\Users\user\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\Tools\minhook',
    [string]$VisualStudioDir = '',
    [int]$TimeoutSeconds = 60,
    [string]$Only = '*',
    [string]$WorkRoot = (Join-Path ([IO.Path]::GetTempPath()) 'capie-mut'),
    [ValidateNotNullOrEmpty()][string]$MutantsFile = 'tests\mutants.json'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
if (Test-Path -LiteralPath $WorkRoot) { Remove-Item -LiteralPath $WorkRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$WorkRoot = (Resolve-Path -LiteralPath $WorkRoot).Path
$minhookInclude = '/I' + (Join-Path $MinHookDir 'include')
Push-Location $WorkRoot
try {
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    & cl.exe /nologo /O2 /MT /c /TC @sources $minhookInclude *> minhook-build.log
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
} finally { Pop-Location }
$objects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj') | ForEach-Object { Join-Path $WorkRoot $_ }
$bridgeSuite = 'tests/legacy/editor_integration_test.cpp'
$dualFlavorSuites = @($bridgeSuite, 'tests/public_editor_test.cpp')

function New-Tree {
    <#
    .SYNOPSIS
        Copies the C++ sources and tests to a fresh working folder.
    .PARAMETER Directory
        Target folder.
    .OUTPUTS
        None.
    .EXAMPLE
        New-Tree 'C:\Temp\capie-mut\1'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$Directory)
    if (Test-Path -LiteralPath $Directory) { Remove-Item -LiteralPath $Directory -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $Directory -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests') -Destination $Directory -Recurse
    # Only C++ is needed; Python copies would be picked up by the repository's Python linters.
    Get-ChildItem -LiteralPath (Join-Path $Directory 'tests') -Recurse -Include '*.py', '*.json' | Remove-Item -Force
}

function Invoke-Suite {
    <#
    .SYNOPSIS
        Compiles and runs one suite in a working folder; returns a status object.
    .PARAMETER Directory
        Working folder (with src/ and tests/).
    .PARAMETER Suite
        Suite path relative to the folder.
    .PARAMETER Summary
        Text that precedes "N checks, M failures" in the suite output.
    .PARAMETER Flavor
        1 (debug) or 0 (release).
    .OUTPUTS
        System.Management.Automation.PSCustomObject. Kind (compile, path, timeout, crash, failed, passed) and Text.
    .EXAMPLE
        Invoke-Suite $dir 'tests/memory_test.cpp' 'memory tests:' 1
    #>
    [CmdletBinding()]
    [OutputType([pscustomobject])]
    param([string]$Directory, [string]$Suite, [string]$Summary, [int]$Flavor)
    $exe = Join-Path $Directory "suite$Flavor.exe"
    $flags = @('/nologo', '/O2', '/MT', '/utf-8', '/std:c++20', '/EHsc', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/UNDEBUG',
        "/DCAPIE_DEBUG_DIAGNOSTICS=$Flavor", $minhookInclude)
    Push-Location $Directory
    try {
        & cl.exe @flags (Join-Path $Directory $Suite) @objects /link bcrypt.lib "/OUT:$exe" *> "build$Flavor.log"
        if ($LASTEXITCODE -ne 0) {
            $log = Get-Content -Raw (Join-Path $Directory "build$Flavor.log")
            if ($log -match 'C1083|C1041') { return [pscustomobject]@{ Kind = 'path'; Text = 'include/path error' } }
            return [pscustomobject]@{ Kind = 'compile'; Text = 'COMPILE-ERROR' }
        }
        $process = Start-Process -FilePath $exe -NoNewWindow -PassThru -WorkingDirectory $Directory `
            -RedirectStandardOutput (Join-Path $Directory "run$Flavor.log") -RedirectStandardError (Join-Path $Directory "run$Flavor.err")
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); return [pscustomobject]@{ Kind = 'timeout'; Text = "TIMEOUT ${TimeoutSeconds}s" }
        }
        $found = (Get-Content (Join-Path $Directory "run$Flavor.log") | Select-String ([regex]::Escape($Summary)) | Select-Object -Last 1)
        if ($found -and $found.Line -match '(\d+) checks, (\d+) failures') {
            if ([int]$matches[2] -gt 0) { return [pscustomobject]@{ Kind = 'failed'; Text = "$($matches[1]) checks, $($matches[2]) failures" } }
            return [pscustomobject]@{ Kind = 'passed'; Text = "$($matches[1]) checks, 0 failures" }
        }
        return [pscustomobject]@{ Kind = 'crash'; Text = "crash/no summary (exit $($process.ExitCode))" }
    } finally { Pop-Location }
}

$mutants = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot $MutantsFile) -Raw | ConvertFrom-Json)

# Baseline: every suite, unmutated, in the same short layout, in every flavor it runs in.
$baselineDir = Join-Path $WorkRoot 'base'
New-Tree $baselineDir
foreach ($suite in ($mutants | Select-Object suite, summary -Unique)) {
    $flavors = if ($suite.suite -in $dualFlavorSuites) { @(1, 0) } else { @(1) }
    foreach ($flavor in $flavors) {
        $result = Invoke-Suite $baselineDir $suite.suite $suite.summary $flavor
        if ($result.Kind -ne 'passed') { throw "baseline $($suite.suite) flavor $flavor is not clean: $($result.Text)" }
        "baseline {0} flavor={1}: {2}" -f $suite.suite, $flavor, $result.Text
    }
}

$survivors = [System.Collections.Generic.List[string]]::new()
$counts = @{}
$index = 0
foreach ($mutant in $mutants) {
    ++$index
    if ($mutant.name -notlike $Only) { continue }
    $dir = Join-Path $WorkRoot ([string]$index)
    New-Tree $dir
    $target = Join-Path $dir $mutant.file
    $original = [IO.File]::ReadAllText($target).Replace("`r`n", "`n")
    $find = $mutant.find.Replace("`r`n", "`n")
    $count = ([regex]::Matches($original, [regex]::Escape($find))).Count
    if ($count -ne 1) { throw "mutant $($mutant.name): find text matched $count times (must be exactly 1)" }
    [IO.File]::WriteAllText($target, $original.Replace($find, $mutant.replace))
    $counts[$mutant.suite] = 1 + [int]($counts[$mutant.suite])
    $flavors = if ($mutant.suite -in $dualFlavorSuites) { @(1, 0) } else { @(1) }
    $killedEverywhere = $true
    foreach ($flavor in $flavors) {
        $result = Invoke-Suite $dir $mutant.suite $mutant.summary $flavor
        if ($result.Kind -eq 'path') { throw "mutant $($mutant.name): $($result.Text) (working folder too long?)" }
        $flavorName = if ($flavor -eq 1) { 'debug' } else { 'release' }
        if ($result.Kind -eq 'passed') {
            $killedEverywhere = $false
            "{0,-36} {1,-7} SURVIVED: {2}" -f $mutant.name, $flavorName, $result.Text
        } else {
            "{0,-36} {1,-7} killed: {2}" -f $mutant.name, $flavorName, $result.Text
        }
    }
    if (-not $killedEverywhere) { $survivors.Add($mutant.name) }
    Remove-Item -LiteralPath $dir -Recurse -Force
}
$total = ($counts.Values | Measure-Object -Sum).Sum
"mutants: $total ($(($counts.GetEnumerator() | Sort-Object Key | ForEach-Object { "$($_.Value) via $([IO.Path]::GetFileName($_.Key))" }) -join ', ')); bridge mutants in debug and release; survivors: $($survivors.Count) $($survivors -join ',')"
if ($survivors.Count) { exit 1 }
