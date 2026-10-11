<#
.SYNOPSIS
    Builds inline_debug_control.exe pinned to the SHA-256 of the already built debug bridge DLL.

.DESCRIPTION
    Requires plugin/build/inline-editor-debug-bridge/rename_anything_inline_editor_debug_bridge.dll
    (build-inline-editor-debug-bridge.ps1). Writes inline_debug_manifest.hpp with the DLL
    name and hash, compiles the controller next to the DLL, verifies the DLL did not change
    during the build and runs the controller's --help self-check (no target or file access).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. "Built fixed controller for debug DLL SHA256 <hash>".

.EXAMPLE
    .\plugin\probes\build-inline-debug-control.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param([string]$VisualStudioDir = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'NativeBuild.psm1') -Force

$script:DebugDllName = 'rename_anything_inline_editor_debug_bridge.dll'
$script:ControllerExeName = 'inline_debug_control.exe'
$script:ManifestHeaderName = 'inline_debug_manifest.hpp'

function Get-DebugDllHash {
    <#
    .SYNOPSIS
        Lower-case SHA-256 of the debug DLL.
    .DESCRIPTION
        The controller embeds this value and refuses any other DLL.
    .PARAMETER Path
        DLL path.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-DebugDllHash -Path $dllPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$outputDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\build\inline-editor-debug-bridge')).Path
$dllPath = Join-Path $outputDirectory $script:DebugDllName
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) { throw 'Build the reviewed debug DLL first.' }
$dllHash = Get-DebugDllHash -Path $dllPath
[void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
$manifest = 'inline constexpr wchar_t rn_debug_dll_name[]=L"' + $script:DebugDllName + '";' + "`n" +
'inline constexpr wchar_t rn_debug_dll_sha256[]=L"' + $dllHash + '";' + "`n"
[IO.File]::WriteAllText((Join-Path $outputDirectory $script:ManifestHeaderName), $manifest, [Text.UTF8Encoding]::new($false))
Push-Location $outputDirectory
try {
    $controllerArgs = New-NbCompilerArgument -Flags (Get-NbFlagSet Probe) -Source (Join-Path $PSScriptRoot 'inline_debug_control.cpp') `
        -IncludeDir $outputDirectory -OutFile $script:ControllerExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $controllerArgs -FailureMessage 'Debug controller compilation failed'
    if ((Get-DebugDllHash -Path $dllPath) -ne $dllHash) {
        throw 'Debug DLL changed during controller build; do not use this output.'
    }
    Invoke-NbTool -FilePath ('.\' + $script:ControllerExeName) -ArgumentList @('--help') -FailureMessage 'Debug controller help check failed'
} finally { Pop-Location }
Write-Output "Built fixed controller for debug DLL SHA256 $dllHash"
