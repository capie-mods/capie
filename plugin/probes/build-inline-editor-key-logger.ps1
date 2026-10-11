param(
    [Parameter(Mandatory = $true)][string]$MinHookDir,
    [string]$VisualStudioDir = ''
)
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path $PSScriptRoot '..\build\inline-editor-key-logger'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$outputDirectory = (Resolve-Path -LiteralPath $outputDirectory).Path
$dllPath = Join-Path $outputDirectory 'rename_anything_inline_editor_key_logger.dll'
foreach ($gameProcess in @(Get-Process -Name hoi4 -ErrorAction SilentlyContinue)) {
    foreach ($module in $gameProcess.Modules) {
        if ($module.FileName -eq $dllPath) {
            throw 'Inline editor key logger DLL is loaded. Exit the game before rebuilding it.'
        }
    }
}
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$MinHookDir = (Resolve-Path -LiteralPath $MinHookDir).Path
$includeDirectory = Join-Path $MinHookDir 'include'
$licensePath = Join-Path $MinHookDir 'LICENSE.txt'
if (!(Test-Path -LiteralPath (Join-Path $includeDirectory 'MinHook.h')) -or !(Test-Path -LiteralPath $licensePath)) {
    throw 'MinHook headers and full LICENSE.txt are required.'
}
Push-Location $outputDirectory
try {
    $common = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    # MinHook is third-party C; enforce /WX on our own C++ sources.
    $cFlags = $common | Where-Object { $_ -ne '/WX' }
    & cl.exe @cFlags /c /TC @sources ('/I' + $includeDirectory)
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
    & cl.exe @common /std:c++20 /EHsc /FAs /Fainline_editor_key_logger.asm /LD (Join-Path $PSScriptRoot 'inline_editor_key_logger.cpp') ('/I' + $includeDirectory) buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:rename_anything_inline_editor_key_logger.dll
    if ($LASTEXITCODE -ne 0) { throw 'Inline editor key logger compilation failed' }
    & cl.exe @common /std:c++20 /EHsc (Join-Path $PSScriptRoot 'inject_inline_editor_key_logger.cpp') /link bcrypt.lib /OUT:inject_inline_editor_key_logger.exe
    if ($LASTEXITCODE -ne 0) { throw 'Inline editor key logger injector compilation failed' }
    & cl.exe @common /UNDEBUG /std:c++20 /EHsc (Join-Path $PSScriptRoot 'inline_key_logger_test.cpp') ('/I' + $includeDirectory) buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:inline_key_logger_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Inline key logger test compilation failed' }
    & .\inline_key_logger_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Inline key logger tests failed' }
    Copy-Item -LiteralPath $licensePath -Destination (Join-Path $outputDirectory 'LICENSE.MinHook.txt') -Force
} finally { Pop-Location }
Write-Output "Built isolated inline editor key logger: $dllPath"
