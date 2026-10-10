param(
    [Parameter(Mandatory=$true)][string]$MinHookDir,
    [string]$VisualStudioDir = ''
)
$ErrorActionPreference = 'Stop'
$pluginDir = $PSScriptRoot
$buildDir = Join-Path $pluginDir 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
# Import the compiler environment without printing it (it may contain private data).
$environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$MinHookDir = (Resolve-Path -LiteralPath $MinHookDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $MinHookDir 'include\MinHook.h'))) { throw 'MinHook.h not found' }
Push-Location $buildDir
try {
    $common = @('/nologo', '/O2', '/MT', '/W4', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
    $minhookSources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    & cl.exe @common /c /TC @minhookSources ('/I' + (Join-Path $MinHookDir 'include'))
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
    & cl.exe @common /std:c++20 /EHsc /LD (Join-Path $pluginDir 'src\country.cpp') ('/I' + (Join-Path $MinHookDir 'include')) buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:rename_anything_country.dll
    if ($LASTEXITCODE -ne 0) { throw 'Country DLL compilation failed' }
    & cl.exe @common /std:c++20 /EHsc (Join-Path $pluginDir 'src\injector.cpp') /link bcrypt.lib /OUT:rename_anything_injector.exe
    if ($LASTEXITCODE -ne 0) { throw 'Injector compilation failed' }
    & cl.exe @common /UNDEBUG /std:c++20 /EHsc (Join-Path $pluginDir 'tests\protocol_test.cpp') /link /OUT:protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & .\protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
} finally { Pop-Location }
Write-Host "Built and tested in $buildDir"
