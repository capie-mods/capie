<#
.SYNOPSIS
    Builds the isolated inline leader bridge probe (DLL + injector).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-leader-bridge/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_leader_bridge.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-leader-bridge.ps1 -MinHookDir C:\src\minhook
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
        Source  = 'inline_leader_bridge.cpp'
        Out     = 'rename_anything_inline_leader_bridge.dll'
        Failure = 'Inline leader bridge compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_leader_bridge.cpp'
        Out     = 'inject_inline_leader_bridge.exe'
        Failure = 'Inline leader bridge injector compilation failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-leader-bridge'
    DllName         = 'rename_anything_inline_leader_bridge.dll'
    LoadedMessage   = 'Inline leader bridge DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated inline leader bridge: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)