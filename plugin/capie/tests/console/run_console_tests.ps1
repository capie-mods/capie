<#
.SYNOPSIS
    Builds and runs the offline tests of the console-command candidate (both switch values).

.DESCRIPTION
    Compiles tests/console/console_test.cpp twice with the CAPIE flags (/W4 /WX /std:c++20):
    once with /DCAPIE_CONSOLE_CANDIDATE=1 (full suite against a fake console) and once with
    /DCAPIE_CONSOLE_CANDIDATE=0 (the bridge must stay disabled). Runs both and fails on any
    failed check. Needs no MinHook and no game file; never touches a game process.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER OutputDirectory
    Output folder relative to the repository root (default plugin\build\capie-console\tests).

.OUTPUTS
    None. Prints the summary line of each run.

.EXAMPLE
    .\plugin\capie\tests\console\run_console_tests.ps1
#>
[CmdletBinding()]
[OutputType([void])]
param(
    [string]$VisualStudioDir = '',
    [ValidateNotNullOrEmpty()][string]$OutputDirectory = 'plugin\build\capie-console\tests'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$out = Join-Path $repo $OutputDirectory
New-Item -ItemType Directory -Force -Path $out | Out-Null

if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}

$flags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/std:c++20', '/EHsc', '/permissive-', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
$source = Join-Path $PSScriptRoot 'console_test.cpp'
Push-Location $out
try {
    foreach ($candidate in 1, 0) {
        $exe = "console_test_candidate$candidate.exe"
        & cl.exe @flags "/DCAPIE_CONSOLE_CANDIDATE=$candidate" $source /link "/OUT:$exe"
        if ($LASTEXITCODE -ne 0) { throw "$exe compilation failed (exit $LASTEXITCODE)" }
        $lines = & (Join-Path $out $exe) 2>&1
        $code = $LASTEXITCODE
        $lines | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
        if ($code -ne 0) { throw "$exe failed (exit $code)" }
    }
} finally { Pop-Location }
Write-Host "Console candidate tests passed: $out"
