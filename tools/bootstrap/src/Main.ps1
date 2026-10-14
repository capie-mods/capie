# ---------------------------------------------------------------------------
# Main: option handling, action dispatch and the script entry point.
# ---------------------------------------------------------------------------

$script:RaOptionNames = @(
    'Install', 'Shortcut', 'ShortcutPath', 'SteamLaunchOption', 'SteamAccount', 'RemoveSteamLaunchOption',
    'Uninstall', 'DryRun', 'Yes', 'PackagePath', 'SteamRoot', 'CheckUpdate', 'Update', 'Rollback', 'ManifestPath',
    'LoadMethod', 'ProxyActivation', 'Latest', 'Purge', 'Disable', 'NoAnimation', 'Plain'
)

function Write-RaDone {
    <#
    .SYNOPSIS
        Prints the localized "Done." line.
    .DESCRIPTION
        Shared closing line of every successful action.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaDone
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param()
    Write-RaHost (Get-RaText 'Done') -Color Green
}

function Initialize-RaRun {
    <#
    .SYNOPSIS
        Applies the options to the runtime switches and prints the banner.
    .DESCRIPTION
        Sets the root folder (unless a test already set it), dry-run and -Yes,
        prints title/intro (unless -NoBanner: the terminal interface draws its own) and logs
        the start line with the switches that were given.
    .PARAMETER Options
        Normalized option table (every known key present).
    .PARAMETER NoBanner
        Do not print the title and intro lines.
    .OUTPUTS
        None.
    .EXAMPLE
        Initialize-RaRun $options
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options, [switch]$NoBanner)
    if (-not $script:RaRoot) { $script:RaRoot = Join-Path $env:LOCALAPPDATA $script:RaRootDirName }
    $script:RaDry = [bool]$Options.DryRun
    $script:RaAssumeYes = [bool]$Options.Yes
    $script:RaMpWarningShown = $false
    if (-not $NoBanner) {
        Write-RaHost (Get-RaText 'Title') -Color White
        Write-RaHost (Get-RaText 'Intro' @((Get-RaLogPath)))
        if ($script:RaDry) { Write-RaHost (Get-RaText 'DryRunNote') -Color Cyan }
    }
    $given = ($Options.GetEnumerator() | Where-Object { $_.Value } | ForEach-Object { "-$($_.Key)" }) -join ' '
    Write-RaLog ('Start: PS {0}, args: {1}' -f $PSVersionTable.PSVersion, $given) -NoConsole
}

function Get-RaSetupPlan {
    <#
    .SYNOPSIS
        Decides what the install/integration run does, asking when no switch was given.
    .DESCRIPTION
        Without -Install/-SteamLaunchOption/-Shortcut the user is asked (install? which
        start mode?). The multiplayer warning is shown before any Steam or shortcut step,
        each of which must then be confirmed (-Yes confirms).
    .PARAMETER Options
        Normalized option table.
    .PARAMETER State
        Existing installation preferences, or null for a fresh install.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Install, Steam, Shortcut, ShortcutFolder)
    .EXAMPLE
        $plan = Get-RaSetupPlan $options
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][hashtable]$Options,
        [Parameter(Position = 1)][AllowNull()][psobject]$State
    )
    $plan = [pscustomobject]@{
        Install        = [bool]$Options.Install
        Steam          = [bool]$Options.SteamLaunchOption
        Shortcut       = [bool]$Options.Shortcut -or [bool]$Options.ShortcutPath
        ShortcutFolder = $null
        LoadMethod     = if ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod) { $Options.LoadMethod } else { 'Launcher' }
        ProxyActivation = if ($Options.ContainsKey('ProxyActivation') -and $Options.ProxyActivation) { $Options.ProxyActivation } else { 'Flag' }
    }
    if (-not ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod) -and $State -and $State.InstalledVersion) {
        $plan.LoadMethod = $State.LoadMethod
    }
    if (-not ($plan.Install -or $plan.Steam -or $plan.Shortcut)) {
        if ($script:RaAssumeYes) { throw 'With -Yes choose at least one of -Install, -SteamLaunchOption, -Shortcut, -Update, -Disable, -Uninstall.' }
        $plan.Install = Confirm-Ra (Get-RaText 'AskInstall' @($script:RaRoot)) $true
        if (-not ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod)) {
            $defaultLoader = if ($plan.LoadMethod -eq 'Proxy') { '2' } else { '1' }
            do { $loader = Read-RaAnswer (Get-RaText 'AskLoadMethod' @($defaultLoader)) } until ($loader -in @('', '1', '2'))
            if ($loader) { $plan.LoadMethod = if ($loader -eq '2') { 'Proxy' } else { 'Launcher' } }
        }
        $modeText = if ($plan.LoadMethod -eq 'Proxy') { 'AskModeProxy' } else { 'AskMode' }
        do { $mode = Read-RaAnswer (Get-RaText $modeText) } until (
            $script:RaStartModes.ContainsKey($mode) -and ($plan.LoadMethod -eq 'Launcher' -or $mode -in @('1', '4'))
        )
        $plan.Steam = $script:RaStartModes[$mode].Steam
        $plan.Shortcut = $script:RaStartModes[$mode].Shortcut
    }
    if ($plan.Shortcut -and $plan.LoadMethod -eq 'Proxy') {
        throw 'Shortcuts use the launcher alternative. Choose -LoadMethod Launcher -Shortcut, or use proxy mode through Steam with -capie.'
    }
    if (($plan.Steam -or $plan.Shortcut) -and -not $script:RaMpWarningShown) {
        Write-RaHost ''
        Write-RaHost (Get-RaText 'MpWarning') -Color Yellow
        Write-RaHost ''
    }
    if ($plan.Steam -and -not (Confirm-Ra (Get-RaText 'AskConfirmSteam') $false)) {
        $plan.Steam = $false
        Write-RaLog 'Steam step declined.'
    }
    if ($plan.Shortcut) {
        $plan.ShortcutFolder = Resolve-RaShortcutFolder $Options.ShortcutPath
        if (-not $Options.ShortcutPath -and -not (Confirm-Ra (Get-RaText 'AskConfirmShortcut') $false)) {
            $plan.Shortcut = $false
            Write-RaLog 'Shortcut step declined.'
        }
    }
    return $plan
}

function Invoke-RaSetupAction {
    <#
    .SYNOPSIS
        Runs the install / Steam launch option / shortcut flow.
    .DESCRIPTION
        Steps run in order install, Steam, shortcut, prune. A failed Steam step is
        logged and makes the exit code 1 but does not stop the shortcut step.
    .PARAMETER Options
        Normalized option table.
    .PARAMETER State
        State object.
    .PARAMETER SteamRootPath
        Steam folder or empty.
    .OUTPUTS
        System.Int32
    .EXAMPLE
        Invoke-RaSetupAction $options $state $steam
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory, Position = 0)][hashtable]$Options,
        [Parameter(Mandatory, Position = 1)][psobject]$State,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    $plan = Get-RaSetupPlan -Options $Options -State $State
    if (-not $Options.ProxyActivation -and $State.LoadMethod -eq 'Proxy') {
        $plan.ProxyActivation = $State.ProxyActivation
    }
    $gameDir = if ($SteamRootPath) { Get-RaGameDir $SteamRootPath } else { $null }
    if ($plan.LoadMethod -eq 'Proxy' -and -not $gameDir) { throw 'HOI4 installation not found through Steam libraries; proxy was not installed.' }
    if ($plan.LoadMethod -eq 'Launcher') {
        Remove-RaProxy $State
        if (-not $script:RaDry) { $State.LoadMethod = 'Launcher' }
        Save-RaState $State
    }
    if ($plan.Install) {
        if ($Options.Latest) { [void](Install-RaLatest -State $State -LocalManifestPath $Options.ManifestPath -LocalZip $Options.PackagePath) }
        else { Install-RaPackage $State $Options.PackagePath }
        Save-RaState $State
        Install-RaPlaysetMod $State
        Save-RaState $State
    }
    if ($plan.LoadMethod -eq 'Proxy') {
        Install-RaProxy $State $gameDir
        if (-not $script:RaDry) { $State.LoadMethod = 'Proxy' }
        Save-RaState $State
        Set-RaProxyActivation $State $plan.ProxyActivation
    }
    if (-not $script:RaDry) { $State.LoadMethod = $plan.LoadMethod }
    Save-RaState $State
    $launcher = Get-RaLauncherPath
    if (($plan.Steam -or $plan.Shortcut) -and -not $script:RaDry -and -not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
        throw (Get-RaText 'NotInstalled')
    }
    $steamStepFailed = $false
    if ($plan.Steam) {
        if (-not $SteamRootPath) { throw 'Steam not found.' }
        try {
            Set-RaSteamLaunchOption -State $State -SteamRootPath $SteamRootPath -AccountSelector $Options.SteamAccount `
                -LauncherPath $launcher -LoadMethod $plan.LoadMethod
            Save-RaState $State
        } catch {
            $steamStepFailed = $true
            Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
        }
    }
    if ($plan.Shortcut) {
        $gameDir = if ($SteamRootPath) { Get-RaGameDir $SteamRootPath } else { $null }
        [void](New-RaShortcut -State $State -Folder $plan.ShortcutFolder -LauncherPath $launcher -GameDir $gameDir)
        Save-RaState $State
    }
    if ($plan.Install) { Remove-RaUnusedVersion $State }
    if ($steamStepFailed) { return $script:RaExitFailure }
    Write-RaDone
    return $script:RaExitSuccess
}

function Invoke-RaAction {
    <#
    .SYNOPSIS
        Dispatches to the requested action and returns its exit code.
    .DESCRIPTION
        Priority: -Uninstall, -RemoveSteamLaunchOption, -Disable, -CheckUpdate, -Rollback,
        -Update, then the install/integration flow. Exceptions propagate to Invoke-RaMain.
    .PARAMETER Options
        Normalized option table.
    .OUTPUTS
        System.Int32
    .EXAMPLE
        Invoke-RaAction $options
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options)
    $state = Read-RaState
    $steamRootPath = $null
    try { $steamRootPath = Get-RaSteamRoot $Options.SteamRoot } catch {
        if ($Options.SteamRoot) { throw }
        Write-RaLog $_.Exception.Message -Level WARN -NoConsole
    }
    $gameDir = { if ($steamRootPath) { Get-RaGameDir $steamRootPath } else { $null } }

    if ($Options.Uninstall) {
        Invoke-RaUninstall $state $steamRootPath
        if (-not $script:RaDry -and (Test-Path -LiteralPath (Get-RaStatePath))) { Remove-Item -LiteralPath (Get-RaStatePath) -Force }
        if ($Options.Purge -and (Invoke-RaPurge $steamRootPath)) {
            Write-RaHost (Get-RaText 'PurgeDone') -Color Green
            return $script:RaExitSuccess
        }
        Write-RaHost (Get-RaText 'UninstallDone' @((Get-RaLogPath))) -Color Green
        return $script:RaExitSuccess
    }
    if ($Options.Disable) {
        Invoke-RaDisable $state $steamRootPath
        Save-RaState $state
        Write-RaHost (Get-RaText 'DisableDone') -Color Green
        return $script:RaExitSuccess
    }
    if ($Options.RemoveSteamLaunchOption) {
        if (-not $steamRootPath) { throw 'Steam not found.' }
        if (-not (Remove-RaSteamLaunchOption $state $steamRootPath)) { throw 'Steam is running; launch option not changed.' }
        Save-RaState $state
        Write-RaDone
        return $script:RaExitSuccess
    }
    if ($Options.CheckUpdate) {
        [void](Invoke-RaCheckUpdate -State $state -GameDir (& $gameDir) -LocalManifestPath $Options.ManifestPath)
        return $script:RaExitSuccess
    }
    if ($Options.Rollback) {
        Invoke-RaRollback $state
        Save-RaState $state
        Install-RaPlaysetMod $state
        Save-RaState $state
        Write-RaDone
        return $script:RaExitSuccess
    }
    if ($Options.Update) {
        if ((Invoke-RaUpdate -State $state -GameDir (& $gameDir) -LocalManifestPath $Options.ManifestPath -LocalZip $Options.PackagePath)) {
            Save-RaState $state
            Install-RaPlaysetMod $state
            Save-RaState $state
            Remove-RaUnusedVersion $state
        }
        Write-RaDone
        return $script:RaExitSuccess
    }
    return (Invoke-RaSetupAction -Options $Options -State $state -SteamRootPath $steamRootPath)
}

function Invoke-RaMain {
    <#
    .SYNOPSIS
        Runs the bootstrap with the given options and returns the exit code.
    .DESCRIPTION
        Enables strict mode and Stop error handling for this call only (never for the
        caller's session under irm | iex), then dispatches. Any error is logged as
        FAILED and turned into exit code 1. Without an action switch on a console that
        cannot answer questions (redirected input or output), it prints the usage text and
        returns exit code 2 without writing anything; -Plain forces the questions anyway.
    .PARAMETER P
        Option table; keys are the script's parameter names, missing keys mean "not given".
    .OUTPUTS
        System.Int32
    .EXAMPLE
        Invoke-RaMain @{ Install = $true; Yes = $true }
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$P)
    Set-StrictMode -Version Latest
    $ErrorActionPreference = 'Stop'
    $options = $P.Clone()
    foreach ($k in $script:RaOptionNames) { if (-not $options.ContainsKey($k)) { $options[$k] = $null } }
    if ($options.Purge -and -not $options.Uninstall) { $options.Purge = $null }
    if ($options.Latest -and -not ($options.Install -or $options.Update)) { $options.Install = $true }
    $mode = Get-RaTuiMode $options
    if (-not $mode.Interactive -and -not (Test-RaActionOption $options) -and -not $options.Yes -and -not $options.Plain) {
        # Redirected or missing console: a question would wait forever. Explain and stop, writing nothing.
        Write-RaHost (Get-RaText 'Title')
        Write-RaHost (Get-RaText 'Usage')
        return $script:RaExitUsage
    }
    if ($mode.Tui) {
        Initialize-RaRun $options -NoBanner
        try { return (Invoke-RaTui -Options $options -Mode $mode) } catch {
            Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
            return $script:RaExitFailure
        }
    }
    Initialize-RaRun $options
    try {
        return (Invoke-RaAction $options)
    } catch {
        Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
        return $script:RaExitFailure
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    $raExit = Invoke-RaMain @{
        Install = $Install; Shortcut = $Shortcut; ShortcutPath = $ShortcutPath; SteamLaunchOption = $SteamLaunchOption
        SteamAccount = $SteamAccount; RemoveSteamLaunchOption = $RemoveSteamLaunchOption; Uninstall = $Uninstall
        DryRun = $DryRun; Yes = $Yes; PackagePath = $PackagePath; SteamRoot = $SteamRoot
        CheckUpdate = $CheckUpdate; Update = $Update; Rollback = $Rollback; ManifestPath = $ManifestPath
        LoadMethod = $LoadMethod; ProxyActivation = $ProxyActivation; Latest = $Latest; Purge = $Purge; Disable = $Disable
        NoAnimation = $NoAnimation; Plain = $Plain
    }
    if ($MyInvocation.MyCommand.CommandType -eq 'ExternalScript') { exit ([int]@($raExit)[-1]) }
    # Under `irm | iex` never call exit (it would close the user's terminal).
}
