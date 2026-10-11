<#
.SYNOPSIS
    Builds the isolated development debug bridge probe (DLL with asm listing, injector, tests).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-editor-debug-bridge/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_editor_debug_bridge.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-editor-debug-bridge.ps1 -MinHookDir C:\src\minhook
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
        Kind       = 'HookedDll'
        Source     = 'inline_editor_debug_bridge.cpp'
        Out        = 'rename_anything_inline_editor_debug_bridge.dll'
        AsmListing = 'inline_editor_debug_bridge.asm'
        Failure    = 'Inline editor key logger compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_editor_debug_bridge.cpp'
        Out     = 'inject_inline_editor_debug_bridge.exe'
        Failure = 'Inline editor key logger injector compilation failed'
    },
    @{
        Kind       = 'HookedTest'
        Source     = 'inline_debug_bridge_test.cpp'
        Out        = 'inline_debug_bridge_test.exe'
        Failure    = 'Inline key logger test compilation failed'
        RunFailure = 'Inline key logger tests failed'
    },
    @{
        Kind       = 'PlainTest'
        Source     = 'inline_debug_trace_test.cpp'
        Out        = 'inline_debug_trace_test.exe'
        Failure    = 'Debug trace test compilation failed'
        RunFailure = 'Debug trace tests failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-editor-debug-bridge'
    DllName         = 'rename_anything_inline_editor_debug_bridge.dll'
    LoadedMessage   = 'Inline editor key logger DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated development debug bridge: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)