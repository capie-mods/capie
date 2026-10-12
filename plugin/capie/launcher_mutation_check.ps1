<#
.SYNOPSIS
    Mutation check of the CAPIE launcher (tests/launcher/mutants.json, 29 mutants).

.DESCRIPTION
    Each mutant is one exact, unique textual replacement in one launcher source plus the
    suite that must kill it (launcher_unit_test or launcher_flow_test). For every mutant
    the src/ tree and tests/launcher/ are copied to a short working folder, the replacement
    is applied, the suite is compiled from the copy (same flags as build_launcher.ps1, so a
    new warning also kills) and run against a scratch folder with fake_process.exe, which is
    built once from the unmutated source.

    Both suites first run unmutated in the same layout and must report 0 failures, otherwise
    the script stops (infrastructure problem, not a kill). A mutant is killed by a compile
    error, failing checks, a crash or the timeout; an include/path error (C1083, C1041)
    stops the script. Any survivor fails the script. Fakes only; never starts a game.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER TimeoutSeconds
    Per-run timeout.

.PARAMETER Only
    Optional mutant name filter (wildcards).

.PARAMETER WorkRoot
    Short working folder (default %TEMP%\capie-lmut).

.OUTPUTS
    System.String. One line per mutant and a summary line.

.EXAMPLE
    pwsh -File .\plugin\capie\launcher_mutation_check.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [string]$VisualStudioDir = '',
    [int]$TimeoutSeconds = 300,
    [string]$Only = '*',
    [string]$WorkRoot = (Join-Path ([IO.Path]::GetTempPath()) 'capie-lmut')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'NativeBuild.psm1') -Force

$script:Flags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE',
    '/std:c++20', '/EHsc', '/permissive-')
$script:Libraries = @('bcrypt.lib', 'user32.lib', 'shell32.lib', 'ole32.lib', 'comdlg32.lib', 'advapi32.lib')

[void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
if (Test-Path -LiteralPath $WorkRoot) { Remove-Item -LiteralPath $WorkRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$WorkRoot = (Resolve-Path -LiteralPath $WorkRoot).Path
$fake = Join-Path $WorkRoot 'fake_process.exe'
Push-Location $WorkRoot
try {
    & cl.exe @script:Flags (Join-Path $PSScriptRoot 'tests\launcher\fake_process.cpp') /link user32.lib /SUBSYSTEM:WINDOWS `
        /ENTRY:wmainCRTStartup "/OUT:$fake" *> fake-build.log
    if ($LASTEXITCODE -ne 0) { throw 'fake_process.exe compilation failed' }
} finally { Pop-Location }

function New-Tree {
    <#
    .SYNOPSIS
        Copies src/ and tests/launcher/ (C++ only) to a fresh working folder.
    .PARAMETER Directory
        Target folder.
    .OUTPUTS
        None.
    .EXAMPLE
        New-Tree 'C:\Temp\capie-lmut\1'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$Directory)
    if (Test-Path -LiteralPath $Directory) { Remove-Item -LiteralPath $Directory -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $Directory 'tests') | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $Directory -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\launcher') -Destination (Join-Path $Directory 'tests') -Recurse
    Get-ChildItem -LiteralPath (Join-Path $Directory 'tests') -Recurse -Include '*.json' | Remove-Item -Force
}

function Invoke-Suite {
    <#
    .SYNOPSIS
        Compiles and runs one suite in a working folder; returns a status object.
    .PARAMETER Directory
        Working folder (with src/ and tests/launcher/).
    .PARAMETER Suite
        Suite path relative to the folder.
    .PARAMETER Summary
        Text that precedes "N checks, M failures" in the suite output.
    .OUTPUTS
        System.Management.Automation.PSCustomObject. Kind (compile, path, timeout, crash, failed, passed) and Text.
    .EXAMPLE
        Invoke-Suite $dir 'tests/launcher/launcher_unit_test.cpp' 'launcher unit tests:'
    #>
    [CmdletBinding()]
    [OutputType([pscustomobject])]
    param([string]$Directory, [string]$Suite, [string]$Summary)
    $exe = Join-Path $Directory 'suite.exe'
    Push-Location $Directory
    try {
        & cl.exe @script:Flags (Join-Path $Directory $Suite) /link @script:Libraries "/OUT:$exe" *> build.log
        if ($LASTEXITCODE -ne 0) {
            if ((Get-Content -Raw (Join-Path $Directory 'build.log')) -match 'C1083|C1041') {
                return [pscustomobject]@{ Kind = 'path'; Text = 'include/path error' }
            }
            return [pscustomobject]@{ Kind = 'compile'; Text = 'COMPILE-ERROR' }
        }
        $scratch = Join-Path $Directory 'scratch'
        $process = Start-Process -FilePath $exe -ArgumentList @("`"$scratch`"", "`"$fake`"") -NoNewWindow -PassThru `
            -WorkingDirectory $Directory -RedirectStandardOutput (Join-Path $Directory 'run.log') -RedirectStandardError (Join-Path $Directory 'run.err')
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true); return [pscustomobject]@{ Kind = 'timeout'; Text = "TIMEOUT ${TimeoutSeconds}s" }
        }
        $found = Get-Content (Join-Path $Directory 'run.log') | Select-String ([regex]::Escape($Summary)) | Select-Object -Last 1
        if ($found -and $found.Line -match '(\d+) checks, (\d+) failures') {
            if ([int]$Matches[2] -gt 0) { return [pscustomobject]@{ Kind = 'failed'; Text = "$($Matches[1]) checks, $($Matches[2]) failures" } }
            return [pscustomobject]@{ Kind = 'passed'; Text = "$($Matches[1]) checks, 0 failures" }
        }
        return [pscustomobject]@{ Kind = 'crash'; Text = "crash/no summary (exit $($process.ExitCode))" }
    } finally { Pop-Location }
}

$mutants = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tests\launcher\mutants.json') -Raw | ConvertFrom-Json)
$baselineDir = Join-Path $WorkRoot 'base'
New-Tree $baselineDir
foreach ($suite in ($mutants | Select-Object suite, summary -Unique)) {
    $result = Invoke-Suite $baselineDir $suite.suite $suite.summary
    if ($result.Kind -ne 'passed') { throw "baseline $($suite.suite) is not clean: $($result.Text)" }
    'baseline {0}: {1}' -f $suite.suite, $result.Text
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
    $count = ([regex]::Matches($original, [regex]::Escape($mutant.find))).Count
    if ($count -ne 1) { throw "mutant $($mutant.name): find text matched $count times (must be exactly 1)" }
    [IO.File]::WriteAllText($target, $original.Replace($mutant.find, $mutant.replace))
    $counts[$mutant.suite] = 1 + [int]($counts[$mutant.suite])
    $result = Invoke-Suite $dir $mutant.suite $mutant.summary
    if ($result.Kind -eq 'path') { throw "mutant $($mutant.name): $($result.Text)" }
    if ($result.Kind -eq 'passed') {
        $survivors.Add($mutant.name)
        '{0,-28} SURVIVED: {1}' -f $mutant.name, $result.Text
    } else {
        '{0,-28} killed: {1}' -f $mutant.name, $result.Text
    }
    Remove-Item -LiteralPath $dir -Recurse -Force
}
$total = ($counts.Values | Measure-Object -Sum).Sum
"mutants: $total ($(($counts.GetEnumerator() | Sort-Object Key | ForEach-Object { "$($_.Value) via $([IO.Path]::GetFileName($_.Key))" }) -join ', ')); survivors: $($survivors.Count) $($survivors -join ',')"
if ($survivors.Count) { exit 1 }
