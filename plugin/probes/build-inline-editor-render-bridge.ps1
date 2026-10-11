<#
.SYNOPSIS
    Builds the isolated inline editor render bridge probe (DLL, injector, transport test).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-editor-render-bridge/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_editor_bridge.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-editor-render-bridge.ps1 -MinHookDir C:\src\minhook
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
        Source  = 'inline_editor_render_bridge.cpp'
        Out     = 'rename_anything_inline_editor_bridge.dll'
        Failure = 'Inline editor render bridge compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_editor_render_bridge.cpp'
        Out     = 'inject_inline_editor_render_bridge.exe'
        Failure = 'Inline editor render bridge injector compilation failed'
    },
    @{
        Kind       = 'HookedTest'
        Source     = 'inline_render_transport_test.cpp'
        Out        = 'inline_render_transport_test.exe'
        Failure    = 'Inline render transport test compilation failed'
        RunFailure = 'Inline render transport tests failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-editor-render-bridge'
    DllName         = 'rename_anything_inline_editor_bridge.dll'
    LoadedMessage   = 'Inline editor render bridge DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated inline editor render bridge: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)