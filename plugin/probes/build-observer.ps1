<#
.SYNOPSIS
    Builds the isolated editor observer probe (DLL + injector).

.DESCRIPTION
    Development probe build. Output: plugin/build/editor-observer/ (nothing else is written).
    Refuses to run while hoi4.exe has rename_anything_editor_observer.dll from that folder loaded. Compiler flags,
    file names and step order are defined in plugin/NativeBuild.psm1 and this file.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.OUTPUTS
    System.String. The path of the built DLL.

.EXAMPLE
    .\plugin\probes\build-observer.ps1 -MinHookDir C:\src\minhook
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
        Source  = 'editor_observer.cpp'
        Out     = 'rename_anything_editor_observer.dll'
        Failure = 'Observer compilation failed'
    },
    @{
        Kind    = 'Injector'
        Source  = 'inject_observer.cpp'
        Out     = 'inject_editor_observer.exe'
        Failure = 'Observer injector compilation failed'
    }
)
$buildArgs = @{
    ProbeRoot       = $PSScriptRoot
    OutputName      = 'editor-observer'
    DllName         = 'rename_anything_editor_observer.dll'
    LoadedMessage   = 'Observer DLL is loaded. Exit the game before rebuilding it.'
    MinHookDir      = $MinHookDir
    VisualStudioDir = $VisualStudioDir
    Steps           = $steps
    DoneMessage     = 'Built isolated observer: {0}'
}
Write-Output (Invoke-NbProbeBuild @buildArgs)