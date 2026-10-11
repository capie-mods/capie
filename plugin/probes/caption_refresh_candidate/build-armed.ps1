param(
    [Parameter(Mandatory = $true)][string]$MinHookDir,
    [string]$VisualStudioDir = '',
    [string]$Python = 'C:\Users\user\miniconda3\python.exe'
)
# Builds the ARMED caption refresh bridge (generated copy of the accepted debug bridge with the
# caption core on the same 7 hooks) into plugin/build/caption-refresh-bridge, plus its injector,
# the inherited debug-bridge suite (re-targeted at the generated source) and the integration suite.
# Never inspects, starts, injects into or reads memory of a game process.
$ErrorActionPreference = 'Stop'
& $Python -I (Join-Path $PSScriptRoot 'make_armed_bridge.py') --check
if ($LASTEXITCODE -ne 0) { throw 'Generated bridge is stale: run make_armed_bridge.py and review the diff' }
$outputDirectory = Join-Path $PSScriptRoot '..\..\build\caption-refresh-bridge'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$outputDirectory = (Resolve-Path -LiteralPath $outputDirectory).Path
$dllPath = Join-Path $outputDirectory 'rename_anything_caption_refresh_bridge.dll'
if (Test-Path -LiteralPath $dllPath) {
    try { [System.IO.File]::Open($dllPath, 'Open', 'ReadWrite', 'None').Dispose() }
    catch { throw 'Armed bridge DLL is locked (possibly loaded). Do not rebuild a loaded module.' }
}
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
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
    $cFlags = $common | Where-Object { $_ -ne '/WX' }  # third-party C; /WX enforced on our C++
    & cl.exe @cFlags /c /TC @sources ('/I' + $includeDirectory)
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
    $minhook = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj')
    & cl.exe @common /std:c++20 /EHsc /LD (Join-Path $PSScriptRoot 'caption_refresh_bridge.cpp') ('/I' + $includeDirectory) @minhook /link bcrypt.lib /OUT:rename_anything_caption_refresh_bridge.dll
    if ($LASTEXITCODE -ne 0) { throw 'Armed bridge compilation failed' }
    & cl.exe @common /std:c++20 /EHsc (Join-Path $PSScriptRoot 'inject_caption_refresh_bridge.cpp') /link bcrypt.lib /OUT:inject_caption_refresh_bridge.exe
    if ($LASTEXITCODE -ne 0) { throw 'Injector compilation failed' }
    & cl.exe @common /UNDEBUG /std:c++20 /EHsc (Join-Path $PSScriptRoot 'caption_refresh_bridge_inherited_test.cpp') ('/I' + $includeDirectory) @minhook /link bcrypt.lib /OUT:caption_refresh_bridge_inherited_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Inherited suite compilation failed' }
    & .\caption_refresh_bridge_inherited_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Inherited debug-bridge suite failed on the armed bridge' }
    & cl.exe @common /UNDEBUG /std:c++20 /EHsc (Join-Path $PSScriptRoot 'caption_refresh_bridge_test.cpp') ('/I' + $includeDirectory) @minhook /link bcrypt.lib /OUT:caption_refresh_bridge_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Integration suite compilation failed' }
    & .\caption_refresh_bridge_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Integration suite failed' }
    & cl.exe @common /std:c++20 /EHsc (Join-Path $PSScriptRoot '..\inline_debug_trace_test.cpp') /I (Join-Path $PSScriptRoot '..') /link /OUT:inline_debug_trace_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Debug trace test compilation failed' }
    & .\inline_debug_trace_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Debug trace tests failed' }
    Copy-Item -LiteralPath $licensePath -Destination (Join-Path $outputDirectory 'LICENSE.MinHook.txt') -Force
} finally { Pop-Location }
Write-Output "Built ARMED caption refresh bridge (not injected): $dllPath"
