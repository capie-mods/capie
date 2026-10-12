<#
.SYNOPSIS
    Runs clang-tidy and a clang-format check over the CAPIE sources (both build flavors).

.DESCRIPTION
    Looks for clang-tidy.exe and clang-format.exe in the Visual Studio "C++ Clang tools for
    Windows" component (VC\Tools\Llvm\x64\bin) and then in PATH. Uses
    plugin/build/capie/compile_commands.json, which build.ps1 writes. Exits 1 when a tool
    is missing (with installation instructions), when clang-tidy reports anything (all
    checks are errors, see plugin/capie/.clang-tidy) or when a file is not formatted.
    Reads sources only; never touches a game process.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. Tool output and a summary line.

.EXAMPLE
    .\plugin\capie\tools\run-clang-tidy.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param([string]$VisualStudioDir = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-LlvmTool {
    <#
    .SYNOPSIS
        Finds an LLVM tool in the VS installation or in PATH.
    .PARAMETER Name
        Executable name.
    .PARAMETER Root
        Visual Studio installation path.
    .OUTPUTS
        System.String. Full path, or empty when missing.
    .EXAMPLE
        Find-LlvmTool 'clang-tidy.exe' $vs
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([string]$Name, [string]$Root)
    if ($Root) {
        $candidate = Join-Path $Root ('VC\Tools\Llvm\x64\bin\' + $Name)
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return ''
}

if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) { $VisualStudioDir = & $vswhere -latest -products * -property installationPath }
}
$tidy = Find-LlvmTool 'clang-tidy.exe' $VisualStudioDir
$format = Find-LlvmTool 'clang-format.exe' $VisualStudioDir
if (-not $tidy -or -not $format) {
    Write-Output ('clang-tidy/clang-format not found. Install the Visual Studio component "C++ Clang tools for Windows" ' +
        '(Microsoft.VisualStudio.Component.VC.Llvm.Clang) or put LLVM 17+ in PATH, then rerun.')
    exit 1
}
$capie = Split-Path -Parent $PSScriptRoot
$database = Join-Path $capie '..\build\capie'
if (-not (Test-Path -LiteralPath (Join-Path $database 'compile_commands.json'))) { throw 'Run plugin/capie/build.ps1 first (it writes compile_commands.json).' }
# Formatting: every CAPIE source and the new tests. tests/legacy and tests/compat are byte-for-byte
# copies of the accepted suites (only include lines changed) and stay in their accepted form.
$formatted = @(Get-ChildItem -LiteralPath (Join-Path $capie 'src') -Recurse -Include '*.cpp', '*.hpp') +
    @(Get-ChildItem -LiteralPath (Join-Path $capie 'tests') -Recurse -Include '*.cpp', '*.hpp' |
        Where-Object { $_.FullName -notmatch '[\\/](legacy|compat)[\\/]' })
$failed = $false
foreach ($file in $formatted) {
    & $format --dry-run --Werror --style=file $file.FullName
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}
# clang-tidy: every translation unit of compile_commands.json (debug flavor), plus the release
# flavor of the editor composition root (different hook set and #if branch).
$units = @(Get-Content -LiteralPath (Join-Path $database 'compile_commands.json') -Raw | ConvertFrom-Json | ForEach-Object file)
foreach ($unit in $units) {
    & $tidy -p $database --quiet $unit
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}
& $tidy -p $database --quiet '--extra-arg=/UCAPIE_DEBUG_DIAGNOSTICS' '--extra-arg=/DCAPIE_DEBUG_DIAGNOSTICS=0' `
    (Join-Path $capie 'src\dll\capie_editor.cpp')
if ($LASTEXITCODE -ne 0) { $failed = $true }
if ($failed) { Write-Output 'clang-tidy/clang-format: findings above'; exit 1 }
Write-Output "clang-tidy ($($units.Count) units + release editor) and clang-format ($($formatted.Count) files): clean"
