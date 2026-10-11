<#
.SYNOPSIS
    Builds the isolated inline editor focus bridge probe (DLL, injector, transport test).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-editor-focus-bridge/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_editor_focus_bridge.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-editor-focus-bridge.ps1 -MinHookDir C:\src\minhook
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
        Source  = 'inline_editor_focus_bridge.cpp'
        Out     = 'rename_anything_inline_editor_focus_bridge.dll'
        Failure = 'Inline editor focus bridge compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_editor_focus_bridge.cpp'
        Out     = 'inject_inline_editor_focus_bridge.exe'
        Failure = 'Inline editor focus bridge injector compilation failed'
    },
    @{
        Kind       = 'HookedTest'
        Source     = 'inline_focus_transport_test.cpp'
        Out        = 'inline_focus_transport_test.exe'
        Failure    = 'Inline focus transport test compilation failed'
        RunFailure = 'Inline focus transport tests failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-editor-focus-bridge'
    DllName         = 'rename_anything_inline_editor_focus_bridge.dll'
    LoadedMessage   = 'Inline editor focus bridge DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated inline editor focus bridge: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)