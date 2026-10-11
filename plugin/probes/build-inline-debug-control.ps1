param([string]$VisualStudioDir = '')
$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path $PSScriptRoot '..\build\inline-editor-debug-bridge'
$outputDirectory = (Resolve-Path -LiteralPath $outputDirectory).Path
$dllPath = Join-Path $outputDirectory 'rename_anything_inline_editor_debug_bridge.dll'
if (!(Test-Path -LiteralPath $dllPath -PathType Leaf)) { throw 'Build the reviewed debug DLL first.' }
$dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
if (!$VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (!$VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$manifest = 'inline constexpr wchar_t rn_debug_dll_name[]=L"rename_anything_inline_editor_debug_bridge.dll";' + "`n" +
    'inline constexpr wchar_t rn_debug_dll_sha256[]=L"' + $dllHash + '";' + "`n"
[IO.File]::WriteAllText((Join-Path $outputDirectory 'inline_debug_manifest.hpp'), $manifest, [Text.UTF8Encoding]::new($false))
Push-Location $outputDirectory
try {
    & cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /std:c++20 /EHsc (Join-Path $PSScriptRoot 'inline_debug_control.cpp') ('/I' + $outputDirectory) /link bcrypt.lib /OUT:inline_debug_control.exe
    if ($LASTEXITCODE -ne 0) { throw 'Debug controller compilation failed' }
    if ((Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dllHash) {
        throw 'Debug DLL changed during controller build; do not use this output.'
    }
    & .\inline_debug_control.exe --help
    if ($LASTEXITCODE -ne 0) { throw 'Debug controller help check failed' }
} finally { Pop-Location }
Write-Output "Built fixed controller for debug DLL SHA256 $dllHash"
