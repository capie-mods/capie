<#
.SYNOPSIS
    Shared building blocks for the native build scripts in plugin/ (MSVC x64 + MinHook).

.DESCRIPTION
    Every plugin build script used to repeat the same ~40 lines: refuse to rebuild a DLL
    that a running hoi4.exe has loaded, locate Visual Studio with vswhere, import the
    vcvars64 environment, validate the MinHook checkout, compile MinHook, compile our
    sources with fixed flags and check every exit code. This module holds that logic once.

    The compiler command lines are assembled from named flag sets (constants below) in
    exactly the order the scripts used before, so the produced binaries and file names do
    not change. Everything outside the process goes through adapters
    ($script:NbGetGameProcess, $script:NbFindVisualStudio, $script:NbReadVcVarsEnvironment,
    $script:NbInvokeTool), which tests replace with Set-NbAdapter.

    The scripts in plugin/probes/caption_refresh_candidate do not use this module yet.
#>
Set-StrictMode -Version Latest

# --- Constants ---------------------------------------------------------------
$script:NbGameProcessName = 'hoi4'
$script:NbVsWhereRelPath = 'Microsoft Visual Studio\Installer\vswhere.exe'
$script:NbVcToolsComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$script:NbVcVarsRelPath = 'VC\Auxiliary\Build\vcvars64.bat'
$script:NbCompiler = 'cl.exe'
$script:NbMinHookHeaderRelPath = 'include\MinHook.h'
$script:NbMinHookIncludeRelPath = 'include'
$script:NbMinHookLicenseName = 'LICENSE.txt'
$script:NbMinHookLicenseOutputName = 'LICENSE.MinHook.txt'
$script:NbMinHookSourceNames = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c')
$script:NbMinHookObjects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj')
$script:NbDefaultLinkLibraries = @('bcrypt.lib')

# Flag sets (order matters for byte-identical command lines).
# Probe builds: our C++ code always with /WX right after /W4.
$script:NbProbeFlags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
# Third-party C (MinHook) is compiled without /WX.
$script:NbThirdPartyCFlags = @('/nologo', '/O2', '/MT', '/W4', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
# plugin/build.ps1 (country capability): /WX appended after the common set.
$script:NbCountryFlags = $script:NbThirdPartyCFlags + @('/WX')
$script:NbCppFlags = @('/std:c++20', '/EHsc')
$script:NbCompileOnlyCFlags = @('/c', '/TC')
$script:NbReleaseAssertFlag = '/UNDEBUG'
$script:NbDllFlag = '/LD'

# --- Adapters (test seams) ---------------------------------------------------
# GetGameProcess() -> processes whose loaded modules are checked.
$script:NbGetGameProcess = { @(Get-Process -Name $script:NbGameProcessName -ErrorAction SilentlyContinue) }
# FindVisualStudio() -> installation path or empty.
$script:NbFindVisualStudio = {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} $script:NbVsWhereRelPath
    & $vswhere -latest -products * -requires $script:NbVcToolsComponent -property installationPath
}
# ReadVcVarsEnvironment(VcVarsPath) -> @{ ExitCode; Lines } ("set" output after vcvars64.bat).
$script:NbReadVcVarsEnvironment = {
    param([string]$VcVarsPath)
    $lines = & $env:ComSpec /d /c ('call "' + $VcVarsPath + '" >nul && set')
    @{ ExitCode = $LASTEXITCODE; Lines = @($lines) }
}
# InvokeTool(FilePath, Arguments, LogPath, AppendLog) -> exit code. Tool output goes to the host
# (and, with LogPath, also to the log file).
$script:NbInvokeTool = {
    param([string]$FilePath, [string[]]$ArgumentList, [string]$LogPath, [bool]$AppendLog)
    if ($LogPath) {
        & $FilePath @ArgumentList 2>&1 | Tee-Object -FilePath $LogPath -Append:$AppendLog | Out-Host
    } else {
        & $FilePath @ArgumentList | Out-Host
    }
    $LASTEXITCODE
}

function Set-NbAdapter {
    <#
    .SYNOPSIS
        Replaces one adapter of this module (tests only).
    .DESCRIPTION
        Names: GetGameProcess, FindVisualStudio, ReadVcVarsEnvironment, InvokeTool.
    .PARAMETER Name
        Adapter name.
    .PARAMETER ScriptBlock
        Replacement with the same signature.
    .OUTPUTS
        System.Management.Automation.ScriptBlock (the previous adapter, to restore it)
    .EXAMPLE
        $old = Set-NbAdapter InvokeTool { param($f, $a) 0 }
    #>
    [CmdletBinding()]
    [OutputType([scriptblock])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateSet('GetGameProcess', 'FindVisualStudio', 'ReadVcVarsEnvironment', 'InvokeTool')][string]$Name,
        [Parameter(Mandatory, Position = 1)][scriptblock]$ScriptBlock
    )
    $variable = "Nb$Name"
    $previous = Get-Variable -Name $variable -Scope Script -ValueOnly
    Set-Variable -Name $variable -Scope Script -Value $ScriptBlock
    return $previous
}

function Initialize-NbOutputDirectory {
    <#
    .SYNOPSIS
        Creates the output folder if needed and returns its full path.
    .DESCRIPTION
        Same as New-Item -Force followed by Resolve-Path.
    .PARAMETER Path
        Output folder (relative paths are resolved against the current location).
    .OUTPUTS
        System.String
    .EXAMPLE
        Initialize-NbOutputDirectory -Path (Join-Path $PSScriptRoot '..\build\editor-bridge')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-NbModuleNotLoaded {
    <#
    .SYNOPSIS
        Refuses to rebuild a DLL that a running game process has loaded.
    .DESCRIPTION
        Compares every module path of every hoi4 process with -ModulePath.
    .PARAMETER ModulePath
        Full path of the DLL that is about to be rebuilt.
    .PARAMETER Message
        Error message when it is loaded.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-NbModuleNotLoaded -ModulePath $dllPath -Message 'Bridge DLL is loaded. Exit the game before rebuilding it.'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][string]$ModulePath,
        [Parameter(Mandatory)][string]$Message
    )
    foreach ($gameProcess in @(& $script:NbGetGameProcess)) {
        foreach ($module in $gameProcess.Modules) {
            if ($module.FileName -eq $ModulePath) { throw $Message }
        }
    }
}

function Import-NbVisualStudioEnvironment {
    <#
    .SYNOPSIS
        Finds Visual Studio (unless given) and imports the vcvars64 environment into this process.
    .DESCRIPTION
        The environment is applied silently; it is never printed because it may contain
        private data.
    .PARAMETER VisualStudioDir
        Installation folder; empty means "ask vswhere".
    .PARAMETER MissingMessage
        Error message when no Visual Studio with the C++ x64 tools is found.
    .PARAMETER FailureMessage
        Error message when vcvars64.bat fails.
    .OUTPUTS
        System.String (the Visual Studio folder that was used)
    .EXAMPLE
        Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [AllowEmptyString()][string]$VisualStudioDir = '',
        [string]$MissingMessage = 'Visual Studio C++ x64 tools are required.',
        [string]$FailureMessage = 'vcvars64 failed'
    )
    if (-not $VisualStudioDir) { $VisualStudioDir = & $script:NbFindVisualStudio }
    if (-not $VisualStudioDir) { throw $MissingMessage }
    $result = & $script:NbReadVcVarsEnvironment (Join-Path $VisualStudioDir $script:NbVcVarsRelPath)
    if ($result.ExitCode -ne 0) { throw $FailureMessage }
    foreach ($line in $result.Lines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    return [string]$VisualStudioDir
}

function Resolve-NbMinHook {
    <#
    .SYNOPSIS
        Validates a MinHook checkout and returns the paths the builds need.
    .DESCRIPTION
        MinHook.h and the full LICENSE.txt (required for redistribution) must exist.
    .PARAMETER MinHookDir
        MinHook source folder.
    .PARAMETER MissingMessage
        Error message when the header or the licence is missing.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Root, Include, License, Sources)
    .EXAMPLE
        $minHook = Resolve-NbMinHook -MinHookDir $MinHookDir
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$MinHookDir,
        [string]$MissingMessage = 'MinHook headers and full LICENSE.txt are required.'
    )
    $root = (Resolve-Path -LiteralPath $MinHookDir).Path
    $license = Join-Path $root $script:NbMinHookLicenseName
    if (-not (Test-Path -LiteralPath (Join-Path $root $script:NbMinHookHeaderRelPath)) -or -not (Test-Path -LiteralPath $license)) {
        throw $MissingMessage
    }
    return [pscustomobject]@{
        Root    = $root
        Include = Join-Path $root $script:NbMinHookIncludeRelPath
        License = $license
        Sources = @($script:NbMinHookSourceNames | ForEach-Object { Join-Path $root ('src\' + $_) })
    }
}

function Invoke-NbTool {
    <#
    .SYNOPSIS
        Runs a build tool through the tool adapter and throws when it fails.
    .DESCRIPTION
        Exit code 0 is success. Optionally tees the output into a log file.
    .PARAMETER FilePath
        Executable (cl.exe, mt.exe or .\some_test.exe).
    .PARAMETER ArgumentList
        Arguments, passed one by one.
    .PARAMETER FailureMessage
        Error message for a non-zero exit code.
    .PARAMETER LogPath
        Optional log file.
    .PARAMETER AppendLog
        Append to the log instead of replacing it.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-NbTool -FilePath '.\protocol_test.exe' -FailureMessage 'Protocol tests failed'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$FilePath,
        [AllowEmptyCollection()][string[]]$ArgumentList = @(),
        [Parameter(Mandatory)][string]$FailureMessage,
        [string]$LogPath = '',
        [switch]$AppendLog
    )
    $exitCode = & $script:NbInvokeTool $FilePath $ArgumentList $LogPath ([bool]$AppendLog)
    if (@($exitCode)[-1] -ne 0) { throw $FailureMessage }
}

function New-NbCompilerArgument {
    <#
    .SYNOPSIS
        Builds one cl.exe argument list in the fixed order used by all plugin builds.
    .DESCRIPTION
        Order: Flags, PreFlags, CppFlags, ExtraFlags, /LD (with -Dll), Source, /I<IncludeDir>,
        LinkObjects, /link, LinkLibraries, LinkOptions, /OUT:<OutFile>.
    .PARAMETER Flags
        Base flag set (e.g. $script:NbProbeFlags via Get-NbFlagSet).
    .PARAMETER PreFlags
        Flags between the base set and the C++ flags (e.g. /UNDEBUG).
    .PARAMETER CppFlags
        Language flags; default /std:c++20 /EHsc.
    .PARAMETER ExtraFlags
        Flags after the language flags (e.g. /FAs /Fa<file>).
    .PARAMETER Dll
        Build a DLL (/LD).
    .PARAMETER Source
        Translation unit.
    .PARAMETER IncludeDir
        Optional include folder.
    .PARAMETER LinkObject
        Object files to link (e.g. the MinHook objects).
    .PARAMETER LinkLibrary
        Libraries after /link (default bcrypt.lib).
    .PARAMETER LinkOption
        Linker options after the libraries.
    .PARAMETER OutFile
        Output file name.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        New-NbCompilerArgument -Flags (Get-NbFlagSet Probe) -Source $cpp -OutFile 'x.exe'
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Mandatory)][string[]]$Flags,
        [AllowEmptyCollection()][string[]]$PreFlags = @(),
        [AllowEmptyCollection()][string[]]$CppFlags = $script:NbCppFlags,
        [AllowEmptyCollection()][string[]]$ExtraFlags = @(),
        [switch]$Dll,
        [Parameter(Mandatory)][string]$Source,
        [string]$IncludeDir = '',
        [AllowEmptyCollection()][string[]]$LinkObject = @(),
        [AllowEmptyCollection()][string[]]$LinkLibrary = $script:NbDefaultLinkLibraries,
        [AllowEmptyCollection()][string[]]$LinkOption = @(),
        [Parameter(Mandatory)][string]$OutFile
    )
    $arguments = @($Flags) + @($PreFlags) + @($CppFlags) + @($ExtraFlags)
    if ($Dll) { $arguments += $script:NbDllFlag }
    $arguments += $Source
    if ($IncludeDir) { $arguments += ('/I' + $IncludeDir) }
    $arguments += @($LinkObject) + @('/link') + @($LinkLibrary) + @($LinkOption) + @("/OUT:$OutFile")
    if (@($arguments | Where-Object { [string]::IsNullOrEmpty($_) }).Count -gt 0) { throw 'Empty compiler argument; check the flag sets passed in.' }
    return [string[]]$arguments
}

function Get-NbFlagSet {
    <#
    .SYNOPSIS
        Returns a named compiler flag set.
    .DESCRIPTION
        Probe (our code, /WX after /W4), ThirdPartyC (MinHook, no /WX) or Country
        (plugin/build.ps1: /WX appended).
    .PARAMETER Name
        Probe, ThirdPartyC or Country.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Get-NbFlagSet -Name Probe
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateSet('Probe', 'ThirdPartyC', 'Country')][string]$Name)
    switch ($Name) {
        'Probe' { return [string[]]$script:NbProbeFlags }
        'ThirdPartyC' { return [string[]]$script:NbThirdPartyCFlags }
        default { return [string[]]$script:NbCountryFlags }
    }
}

function Invoke-NbMinHookCompile {
    <#
    .SYNOPSIS
        Compiles the MinHook C sources to object files in the current folder.
    .DESCRIPTION
        Third-party C code: compiled without /WX (/c /TC).
    .PARAMETER MinHook
        Object from Resolve-NbMinHook.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-NbMinHookCompile -MinHook $minHook
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory)][psobject]$MinHook)
    $arguments = @($script:NbThirdPartyCFlags) + @($script:NbCompileOnlyCFlags) + @($MinHook.Sources) + @('/I' + $MinHook.Include)
    Invoke-NbTool -FilePath $script:NbCompiler -ArgumentList $arguments -FailureMessage 'MinHook compilation failed'
}

function Invoke-NbProbeStep {
    <#
    .SYNOPSIS
        Runs one step of a probe build: compile, and for tests also execute.
    .DESCRIPTION
        Step kinds (all with the Probe flag set):
          HookedDll   /LD, MinHook include and objects, bcrypt.lib
          Injector    plain executable, bcrypt.lib
          HookedTest  /UNDEBUG, MinHook include and objects, bcrypt.lib; then runs .\<Out>
          PlainTest   no extra libraries; then runs .\<Out>
        Optional AsmListing adds /FAs /Fa<AsmListing> (HookedDll only).
    .PARAMETER Step
        Hashtable: Kind, Source (full path), Out, Failure, optional RunFailure and AsmListing.
    .PARAMETER MinHook
        Object from Resolve-NbMinHook.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-NbProbeStep -Step @{ Kind = 'Injector'; Source = $cpp; Out = 'inject.exe'; Failure = 'Injector compilation failed' } -MinHook $m
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][hashtable]$Step,
        [Parameter(Mandatory)][psobject]$MinHook
    )
    $flags = Get-NbFlagSet Probe
    # Note: "$x = if (...) { @() }" would assign $null, which binds as one empty argument; start from an array.
    [string[]]$extra = @()
    if ($Step.ContainsKey('AsmListing')) { $extra = @('/FAs', ('/Fa' + $Step.AsmListing)) }
    $common = @{ Flags = $flags; Source = $Step.Source; OutFile = $Step.Out }
    switch ($Step.Kind) {
        'HookedDll' { $arguments = New-NbCompilerArgument @common -ExtraFlags $extra -Dll -IncludeDir $MinHook.Include -LinkObject $script:NbMinHookObjects }
        'Injector' { $arguments = New-NbCompilerArgument @common }
        'HookedTest' {
            $arguments = New-NbCompilerArgument @common -PreFlags $script:NbReleaseAssertFlag -IncludeDir $MinHook.Include -LinkObject $script:NbMinHookObjects
        }
        'PlainTest' { $arguments = New-NbCompilerArgument @common -LinkLibrary @() }
        default { throw "Unknown build step kind: $($Step.Kind)" }
    }
    Invoke-NbTool -FilePath $script:NbCompiler -ArgumentList $arguments -FailureMessage $Step.Failure
    if ($Step.Kind -like '*Test') { Invoke-NbTool -FilePath ('.\' + $Step.Out) -FailureMessage $Step.RunFailure }
}

function Invoke-NbProbeBuild {
    <#
    .SYNOPSIS
        Builds one isolated probe (MinHook-based DLL, its injector and optional tests).
    .DESCRIPTION
        Shared flow of plugin/probes/build-*.ps1: create plugin/build/<OutputName>, refuse
        when the game has the DLL loaded, import the Visual Studio environment, validate
        MinHook, compile MinHook and every step in that folder, copy the MinHook licence
        next to the binaries, and return the "built" message.
    .PARAMETER ProbeRoot
        Folder of the calling script (plugin/probes).
    .PARAMETER OutputName
        Sub-folder of plugin/build.
    .PARAMETER DllName
        Main DLL file name (checked against running game processes).
    .PARAMETER LoadedMessage
        Error message when the DLL is loaded by the game.
    .PARAMETER MinHookDir
        MinHook source folder.
    .PARAMETER VisualStudioDir
        Visual Studio folder or empty.
    .PARAMETER Steps
        Build steps for Invoke-NbProbeStep (source names relative to ProbeRoot).
    .PARAMETER DoneMessage
        Format string for the final message; {0} is the DLL path.
    .OUTPUTS
        System.String
    .EXAMPLE
        Invoke-NbProbeBuild -ProbeRoot $PSScriptRoot -OutputName 'editor-observer' -DllName 'x.dll' -LoadedMessage 'm' `
            -MinHookDir $MinHookDir -Steps $steps -DoneMessage 'Built isolated observer: {0}'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][string]$ProbeRoot,
        [Parameter(Mandatory)][string]$OutputName,
        [Parameter(Mandatory)][string]$DllName,
        [Parameter(Mandatory)][string]$LoadedMessage,
        [Parameter(Mandatory)][string]$MinHookDir,
        [AllowEmptyString()][string]$VisualStudioDir = '',
        [Parameter(Mandatory)][hashtable[]]$Steps,
        [Parameter(Mandatory)][string]$DoneMessage
    )
    $outputDirectory = Initialize-NbOutputDirectory -Path (Join-Path $ProbeRoot "..\build\$OutputName")
    $dllPath = Join-Path $outputDirectory $DllName
    Assert-NbModuleNotLoaded -ModulePath $dllPath -Message $LoadedMessage
    [void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
    $minHook = Resolve-NbMinHook -MinHookDir $MinHookDir
    Push-Location $outputDirectory
    try {
        Invoke-NbMinHookCompile -MinHook $minHook
        foreach ($step in $Steps) {
            $resolved = $step.Clone()
            $resolved.Source = Join-Path $ProbeRoot $step.Source
            Invoke-NbProbeStep -Step $resolved -MinHook $minHook
        }
        Copy-Item -LiteralPath $minHook.License -Destination (Join-Path $outputDirectory $script:NbMinHookLicenseOutputName) -Force
    } finally { Pop-Location }
    return ($DoneMessage -f $dllPath)
}

Export-ModuleMember -Function @(
    'Set-NbAdapter', 'Initialize-NbOutputDirectory', 'Assert-NbModuleNotLoaded', 'Import-NbVisualStudioEnvironment',
    'Resolve-NbMinHook', 'Invoke-NbTool', 'New-NbCompilerArgument', 'Get-NbFlagSet', 'Invoke-NbMinHookCompile',
    'Invoke-NbProbeStep', 'Invoke-NbProbeBuild'
)
