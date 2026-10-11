param(
    [Parameter(Mandatory = $true)][string]$MinHookDir,
    [string]$VisualStudioDir = ''
)
# SEPARATE candidate: country DLL with the CAPIE status text. Stages a copy of plugin/src, applies
# country_status.patch (the accepted plugin/src/country.cpp is never edited), builds the DLL into
# plugin/build/country-status-candidate and runs the offline tests. Never touches a game process.
$ErrorActionPreference = 'Stop'
$pinnedSource = '689924a8faa5ec24e3f66c1bdf119e358f9eefa4da2ca815f5b7b3cb27e71ccf'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$src = Join-Path $repo 'plugin\src'
if ((Get-FileHash -Algorithm SHA256 (Join-Path $src 'country.cpp')).Hash.ToLower() -ne $pinnedSource) {
    throw 'plugin/src/country.cpp changed since the patch was made; re-review before rebuilding'
}
$out = Join-Path $repo 'plugin\build\country-status-candidate'
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$dllPath = Join-Path $out 'rename_anything_country.dll'
if (Test-Path -LiteralPath $dllPath) {
    try { [System.IO.File]::Open($dllPath, 'Open', 'ReadWrite', 'None').Dispose() }
    catch { throw 'Candidate DLL is locked (possibly loaded). Do not rebuild a loaded module.' }
}
foreach ($f in 'country.cpp', 'image_guard.hpp', 'protocol.hpp', 'country_capability.hpp') {
    Copy-Item -LiteralPath (Join-Path $src $f) -Destination $stage -Force
}
Push-Location $stage
try {
    & git apply --unsafe-paths -p3 --directory=. (Join-Path $PSScriptRoot 'country_status.patch')
    if ($LASTEXITCODE -ne 0) { throw 'country_status.patch does not apply' }
} finally { Pop-Location }
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$MinHookDir = (Resolve-Path -LiteralPath $MinHookDir).Path
$include = Join-Path $MinHookDir 'include'
Push-Location $out
try {
    $common = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
    $cFlags = $common | Where-Object { $_ -ne '/WX' }
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    & cl.exe @cFlags /c /TC @sources ('/I' + $include)
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
    & cl.exe @common /std:c++20 /EHsc /LD (Join-Path $stage 'country.cpp') ('/I' + $include) buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:rename_anything_country.dll
    if ($LASTEXITCODE -ne 0) { throw 'Candidate country DLL compilation failed' }
    & cl.exe @common /std:c++20 /EHsc (Join-Path $PSScriptRoot 'country_status_test.cpp') ('/I' + $stage) ('/I' + $include) buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:country_status_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Country status test compilation failed' }
    & (Join-Path $out 'country_status_test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Country status tests failed' }
    & cl.exe @common /std:c++20 /EHsc /UNDEBUG (Join-Path $repo 'plugin\tests\protocol_test.cpp') ('/I' + $stage) /link /OUT:protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & (Join-Path $out 'protocol_test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
} finally { Pop-Location }
Write-Output "Built SEPARATE country status candidate (not deployed): $dllPath"
