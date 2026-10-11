<#
.SYNOPSIS
    Builds the isolated inline prefill probe (DLL + injector).

.DESCRIPTION
    Development probe build. Output: plugin/build/inline-prefill/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_inline_prefill.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-inline-prefill.ps1 -MinHookDir C:\src\minhook
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
        Source  = 'inline_prefill.cpp'
        Out     = 'rename_anything_inline_prefill.dll'
        Failure = 'Inline prefill compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_inline_prefill.cpp'
        Out     = 'inject_inline_prefill.exe'
        Failure = 'Inline prefill injector compilation failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'inline-prefill'
    DllName         = 'rename_anything_inline_prefill.dll'
    LoadedMessage   = 'Inline prefill DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated inline prefill: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)