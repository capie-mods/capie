param(
    [Parameter(Mandatory = $true)][string]$MinHookDir,
    [string]$VisualStudioDir = '',
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe'
)
# Builds the INACTIVE caption refresh candidate and runs its offline tests.
# Does not inspect, start, inject into or read memory of any game process.
# The EXE is only read as a file for the static fingerprint check.
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path $PSScriptRoot '..\..\build\caption-refresh-candidate'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$outputDirectory = (Resolve-Path -LiteralPath $outputDirectory).Path
$dllPath = Join-Path $outputDirectory 'rename_anything_caption_refresh_candidate.dll'
if (Test-Path -LiteralPath $dllPath) {
    # File-lock probe only (no process access): a loaded DLL cannot be opened for exclusive write.
    try { [System.IO.File]::Open($dllPath, 'Open', 'ReadWrite', 'None').Dispose() }
    catch { throw 'Candidate DLL is locked (possibly loaded). Do not rebuild a loaded module.' }
}
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set')
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$MinHookDir = (Resolve-Path -LiteralPath $MinHookDir).Path
$includeDirectory = Join-Path $MinHookDir 'include'
if (!(Test-Path -LiteralPath (Join-Path $includeDirectory 'MinHook.h'))) { throw 'MinHook headers are required.' }
Push-Location $outputDirectory
try {
    $common = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/std:c++20', '/EHsc', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
    # 1. Shipped INACTIVE DLL: verification only, returns 41, installs no hook, links no MinHook.
    & cl.exe @common /LD (Join-Path $PSScriptRoot 'caption_refresh_candidate.cpp') /link bcrypt.lib /OUT:rename_anything_caption_refresh_candidate.dll
    if ($LASTEXITCODE -ne 0) { throw 'Inactive candidate DLL compilation failed' }
    # 2. Armed variant: compile-only syntax/warning check, never linked or shipped.
    & cl.exe @common /c /DRN_CAPTION_REFRESH_ARM_HOOKS (Join-Path $PSScriptRoot 'caption_refresh_candidate.cpp') ('/I' + $includeDirectory) /Foarmed_compile_check.obj
    if ($LASTEXITCODE -ne 0) { throw 'Armed compile-only check failed' }
    Remove-Item -LiteralPath (Join-Path $outputDirectory 'armed_compile_check.obj') -Force
    # 3. Offline tests (fake memory) + static file check of the pinned EXE.
    & cl.exe @common (Join-Path $PSScriptRoot 'caption_refresh_test.cpp') /link /OUT:caption_refresh_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
    & .\caption_refresh_test.exe $GameExe
    if ($LASTEXITCODE -ne 0) { throw 'Caption refresh tests failed' }
    # 4. Focus-release unit suite (fake memory) + static focus fingerprint check of the EXE file.
    & cl.exe @common (Join-Path $PSScriptRoot 'focus_release_test.cpp') /link /OUT:focus_release_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus release test compilation failed' }
    & (Join-Path (Get-Location) focus_release_test.exe) $GameExe
    if ($LASTEXITCODE -ne 0) { throw 'Focus release tests failed' }
} finally { Pop-Location }
Write-Output "Built INACTIVE caption refresh candidate: $dllPath"
