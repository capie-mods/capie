<#
.SYNOPSIS
    Builds the isolated inline editor key logger probe (DLL with asm listing, injector, test).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-editor-key-logger/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_editor_key_logger.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-editor-key-logger.ps1 -MinHookDir C:\src\minhook
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
        Source     = 'inline_editor_key_logger.cpp'
        Out        = 'rename_anything_inline_editor_key_logger.dll'
        AsmListing = 'inline_editor_key_logger.asm'
        Failure    = 'Inline editor key logger compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_editor_key_logger.cpp'
        Out     = 'inject_inline_editor_key_logger.exe'
        Failure = 'Inline editor key logger injector compilation failed'
    },
    @{
        Kind       = 'HookedTest'
        Source     = 'inline_key_logger_test.cpp'
        Out        = 'inline_key_logger_test.exe'
        Failure    = 'Inline key logger test compilation failed'
        RunFailure = 'Inline key logger tests failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-editor-key-logger'
    DllName         = 'rename_anything_inline_editor_key_logger.dll'
    LoadedMessage   = 'Inline editor key logger DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated inline editor key logger: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)