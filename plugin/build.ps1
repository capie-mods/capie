<#
.SYNOPSIS
    Builds and tests the country capability: rename_anything_country.dll, the injector and the protocol test.

.DESCRIPTION
    Output: -OutputDirectory (default plugin/build). Refuses to run while hoi4.exe has the
    country DLL from that folder loaded. Compiles MinHook (third-party C, no /WX), then
    our C++ sources with /WX, runs protocol_test.exe and copies the MinHook licence
    (required for redistribution) next to the binaries. Flag sets: plugin/NativeBuild.psm1.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER OutputDirectory
    Build output folder.

.OUTPUTS
    None. Prints "Built and tested in <folder>".

.EXAMPLE
    .\plugin\build.ps1 -MinHookDir 'C:\path\to\minhook'
#>
[CmdletBinding()]
[OutputType([void])]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$MinHookDir,
    [string]$VisualStudioDir = '',
    [ValidateNotNullOrEmpty()][string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'NativeBuild.psm1') -Force

$script:CountryDllName = 'rename_anything_country.dll'
$script:InjectorExeName = 'rename_anything_injector.exe'
$script:ProtocolTestExeName = 'protocol_test.exe'

$buildDir = Initialize-NbOutputDirectory -Path $OutputDirectory
Assert-NbModuleNotLoaded -ModulePath (Join-Path $buildDir $script:CountryDllName) -Message 'Country DLL is loaded. Exit the game before rebuilding this target.'
[void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
$minHook = Resolve-NbMinHook -MinHookDir $MinHookDir -MissingMessage 'MinHook.h and LICENSE.txt (required for redistribution) are required.'
$flags = Get-NbFlagSet Country
Push-Location $buildDir
try {
    Invoke-NbMinHookCompile -MinHook $minHook
    $dllArgs = New-NbCompilerArgument -Flags $flags -Dll -Source (Join-Path $PSScriptRoot 'src\country.cpp') -IncludeDir $minHook.Include `
        -LinkObject @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj') -OutFile $script:CountryDllName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $dllArgs -FailureMessage 'Country DLL compilation failed'
    $injectorArgs = New-NbCompilerArgument -Flags $flags -Source (Join-Path $PSScriptRoot 'src\injector.cpp') -OutFile $script:InjectorExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $injectorArgs -FailureMessage 'Injector compilation failed'
    $testArgs = New-NbCompilerArgument -Flags $flags -PreFlags '/UNDEBUG' -Source (Join-Path $PSScriptRoot 'tests\protocol_test.cpp') `
        -LinkLibrary @() -OutFile $script:ProtocolTestExeName
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $testArgs -FailureMessage 'Protocol test compilation failed'
    Invoke-NbTool -FilePath ('.\' + $script:ProtocolTestExeName) -FailureMessage 'Protocol tests failed'
} finally { Pop-Location }
Copy-Item -LiteralPath $minHook.License -Destination (Join-Path $buildDir 'LICENSE.MinHook.txt') -Force
Write-Information -MessageData "Built and tested in $buildDir" -InformationAction Continue
