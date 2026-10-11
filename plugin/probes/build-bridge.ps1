<#
.SYNOPSIS
    Builds the isolated editor bridge probe (DLL + injector).

.DESCRIPTION
    Development probe build. Output: plugin/build/editor-bridge/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_editor_bridge.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-bridge.ps1 -MinHookDir C:\src\minhook
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$MinHookDir,
    [string]$VisualStudioDir = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'NativeBuild.psm1') -Force

$steps = @(
    @{
        Kind    = 'HookedDll'
        Source  = 'editor_bridge.cpp'
        Out     = 'rename_anything_editor_bridge.dll'
        Failure = 'Bridge compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_bridge.cpp'
        Out     = 'inject_editor_bridge.exe'
        Failure = 'Bridge injector compilation failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'editor-bridge'
    DllName         = 'rename_anything_editor_bridge.dll'
    LoadedMessage   = 'Bridge DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated bridge: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)