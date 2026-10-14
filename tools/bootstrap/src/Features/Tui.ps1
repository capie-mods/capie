# ---------------------------------------------------------------------------
# Slice: the terminal interface (irm | iex without switches on an interactive console).
# Splash, one menu (install, update, disable, uninstall), the questions each action needs,
# progress as status lines, and a summary. The actions themselves are the same code paths
# as the switches (Invoke-RaAction), so the interface adds no second implementation.
# ---------------------------------------------------------------------------

$script:RaTuiActiveMode = $null   # mode of the running interface (read by the log sink)

function Test-RaUpdateChannel {
    <#
    .SYNOPSIS
        Tests whether this script can install from GitHub Releases.
    .DESCRIPTION
        True when the repository and the update signing key are configured (not placeholders).
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (Test-RaUpdateChannel) { 'signed releases available' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    return ($script:RaGitHubRepo -notmatch $script:RaRepoPlaceholderMarker) -and ($script:RaUpdatePublicKeyXml -match $script:RaRsaKeyXmlMarker)
}

function Write-RaTuiStatus {
    <#
    .SYNOPSIS
        Writes one status line of the running interface.
    .DESCRIPTION
        Shortcut for Get-RaTuiStatusModel + Write-RaTuiModel with the active mode.
    .PARAMETER Word
        OK, ASK, SKIP, NOTE or FAIL.
    .PARAMETER Text
        Message.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaTuiStatus 'NOTE' 'CAPIE is not installed.'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][string]$Word, [Parameter(Position = 1)][AllowEmptyString()][string]$Text)
    Write-RaTuiModel (Get-RaTuiStatusModel -Word $Word -Text $Text -Mode $script:RaTuiActiveMode) $script:RaTuiActiveMode
}

function Wait-RaTuiSteamClosed {
    <#
    .SYNOPSIS
        Asks the player to close Steam until it is closed or the Steam step is skipped.
    .DESCRIPTION
        Enter checks again, Q or Esc skips. The script never closes Steam itself.
    .OUTPUTS
        System.Boolean ($true when Steam is closed)
    .EXAMPLE
        if (-not (Wait-RaTuiSteamClosed)) { 'skip the Steam step' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    while (Test-RaSteamRunning) {
        Write-RaTuiStatus 'ASK' (Get-RaText 'TuiAskSteamClosed')
        $key = Read-RaTuiKey
        if ($key -eq $script:RaQuitAnswer -or $key -eq 'Escape') { return $false }
    }
    return $true
}

function Get-RaTuiStatusText {
    <#
    .SYNOPSIS
        One sentence about the current installation for the menu screen.
    .DESCRIPTION
        Installed version and whether Steam starts the game with the launcher.
    .PARAMETER State
        State object.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaTuiStatusText (Read-RaState)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if (-not $State.InstalledVersion) { return (Get-RaText 'TuiStatusNone') }
    $steam = if (@($State.SteamEntries).Count -gt 0) { Get-RaText 'TuiSteamOn' } else { Get-RaText 'TuiSteamOff' }
    return (Get-RaText 'TuiStatusInstalled' @($State.InstalledVersion, $steam))
}

function Get-RaTuiPlan {
    <#
    .SYNOPSIS
        Asks what an action needs and returns the option table for Invoke-RaAction, or $null.
    .DESCRIPTION
        Install: the multiplayer note, then whether to add the launcher to Steam (default
        yes), Steam closed, account choice. Installs into the user folder with the launcher
        method (nothing in the game folder; the "CAPIE" switch mod goes to the player's mod
        folder) from the newest signed release when the update
        channel is configured, else the release pinned in this script.
        An existing installation keeps its loading method.
        Update: no question. Disable: confirm (default yes). Uninstall: confirm (default no),
        then whether to purge the log and Steam backups (default no).
        $null means cancelled.
    .PARAMETER Action
        Install, Update, Disable or Uninstall.
    .PARAMETER Options
        Options of this run (DryRun, SteamRoot, PackagePath, ManifestPath carry over).
    .PARAMETER SteamRootPath
        Steam folder or empty.
    .OUTPUTS
        System.Collections.Hashtable
    .EXAMPLE
        $run = Get-RaTuiPlan -Action 'Install' -Options $options -SteamRootPath $steam
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateSet('Install', 'Update', 'Disable', 'Uninstall')][string]$Action,
        [Parameter(Mandatory, Position = 1)][hashtable]$Options,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    $mode = $script:RaTuiActiveMode
    $run = $Options.Clone()
    switch ($Action) {
        'Install' {
            $run.Install = $true
            $run.Latest = (Test-RaUpdateChannel) -and -not $Options.PackagePath
            if (-not $run.LoadMethod -and -not (Read-RaState).InstalledVersion) { $run.LoadMethod = 'Launcher' }
            Write-RaTuiStatus 'NOTE' (Get-RaText 'TuiMpNote')
            $script:RaMpWarningShown = $true
            $steam = $false
            if ($SteamRootPath) { $steam = Confirm-RaTui -Prompt (Get-RaText 'TuiAskSteam') -Default $true -Mode $mode }
            if ($steam -and -not (Wait-RaTuiSteamClosed)) { $steam = $false }
            if ($steam -and @(Get-RaSteamAccount $SteamRootPath).Count -gt 1 -and -not $run.SteamAccount) {
                Write-RaTuiStatus 'NOTE' (Format-RaAccountList @(Get-RaSteamAccount $SteamRootPath))
                $run.SteamAccount = $script:RaAllAccountsSelector
            }
            $run.SteamLaunchOption = $steam
        }
        'Update' { $run.Update = $true }
        'Disable' {
            if (-not (Confirm-RaTui -Prompt (Get-RaText 'TuiAskDisable') -Default $true -Mode $mode)) { return $null }
            if (-not (Wait-RaTuiSteamClosed)) { return $null }
            $run.Disable = $true
        }
        'Uninstall' {
            if (-not (Confirm-RaTui -Prompt (Get-RaText 'TuiAskUninstall') -Default $false -Mode $mode)) { return $null }
            $run.Purge = Confirm-RaTui -Prompt (Get-RaText 'TuiAskPurge') -Default $false -Mode $mode
            if (-not (Wait-RaTuiSteamClosed)) { return $null }
            $run.Uninstall = $true
        }
    }
    return $run
}

function Get-RaTuiSummaryRow {
    <#
    .SYNOPSIS
        Rows of the summary after an action.
    .DESCRIPTION
        What is installed now, what Steam does, that the game folder is unchanged in launcher
        mode, how to switch CAPIE on and off, how to undo, and where the log is.
    .PARAMETER Action
        Action that ran.
    .PARAMETER ExitCode
        Its exit code.
    .PARAMETER Purged
        Whether -Purge removed everything (no log left).
    .OUTPUTS
        System.Object[] (two-element arrays)
    .EXAMPLE
        Get-RaTuiSummaryRow -Action 'Install' -ExitCode 0 -Purged $false
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Action,
        [Parameter(Mandatory, Position = 1)][int]$ExitCode,
        [Parameter(Position = 2)][bool]$Purged = $false
    )
    $state = Read-RaState
    $rows = @()
    $result = if ($ExitCode -eq $script:RaExitSuccess) { Get-RaText 'TuiDone' } else { Get-RaText 'TuiFailed' @((Get-RaLogPath)) }
    $rows += , @((Get-RaText 'TuiRowResult'), $result)
    if ($Action -eq 'Uninstall') {
        $removed = if ($Purged) { Get-RaText 'TuiRemovedAll' } else { Get-RaText 'TuiRemovedKeptLog' }
        $rows += , @((Get-RaText 'TuiRowInstalled'), $removed)
    } else {
        $installed = Get-RaText 'TuiNone'
        if ($state.InstalledVersion) { $installed = "CAPIE $($state.InstalledVersion), $(Get-RaVersionDir $state.InstalledVersion)" }
        $rows += , @((Get-RaText 'TuiRowInstalled'), $installed)
        $steam = if (@($state.SteamEntries).Count -gt 0) { Get-RaText 'TuiSteamOn' } else { Get-RaText 'TuiSteamOff' }
        $rows += , @((Get-RaText 'TuiRowSteam'), $steam)
        if (-not $state.Proxy) { $rows += , @((Get-RaText 'TuiRowGame'), (Get-RaText 'TuiGameUnchanged')) }
        $modFiles = @($state.ModFiles | Where-Object { $_ })
        if ($modFiles.Count -gt 0) { $rows += , @((Get-RaText 'TuiRowMod'), (Join-Path (Get-RaPlaysetModRoot) 'capie')) }
        if ($state.InstalledVersion) { $rows += , @((Get-RaText 'TuiRowSwitch'), (Get-RaText 'TuiSwitchValue')) }
        if ($state.InstalledVersion) { $rows += , @((Get-RaText 'TuiRowUndo'), (Get-RaText 'TuiUndoValue')) }
    }
    if (-not $Purged) { $rows += , @((Get-RaText 'TuiRowLog'), (Get-RaLogPath)) }
    return , $rows
}

function Invoke-RaTui {
    <#
    .SYNOPSIS
        Runs the terminal interface and returns the exit code.
    .DESCRIPTION
        Splash, current status, menu, the questions of the chosen action, the action itself
        through Invoke-RaAction (its log lines become status lines), then the summary. While
        it runs, every answer comes from the interface (-Yes semantics for the shared code);
        console adapters are restored afterwards, also under irm | iex.
    .PARAMETER Options
        Normalized option table.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Int32
    .EXAMPLE
        Invoke-RaTui $options (Get-RaTuiMode $options)
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $savedHost = $script:RaWriteHost
    $savedSink = $script:RaLogSink
    $savedYes = $script:RaAssumeYes
    $script:RaTuiActiveMode = $Mode
    try {
        $script:RaLogSink = { param([string]$Level, [string]$Message) Write-RaTuiStatus $script:RaTuiLevelStatus[$Level] $Message }
        $script:RaWriteHost = {
            param([string]$Message)
            if ($Message) { Write-RaTuiModel (Get-RaTuiTextModel -Text $Message -Style 'khaki' -Mode $script:RaTuiActiveMode) $script:RaTuiActiveMode }
        }
        [void](Show-RaTuiSplash $Mode)
        if ($script:RaDry) { Write-RaTuiStatus 'SKIP' (Get-RaText 'DryRunNote') }
        Write-RaTuiStatus 'NOTE' (Get-RaTuiStatusText (Read-RaState))
        Write-RaTuiBlank $Mode
        Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiMenuTitle') -Style 'parchment' -Mode $Mode) $Mode
        Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiKeys') -Style 'ash' -Mode $Mode) $Mode
        Write-RaTuiBlank $Mode
        $action = Select-RaTuiAction $Mode
        Write-RaTuiBlank $Mode
        if ($action -eq 'Quit') {
            Write-RaTuiStatus 'SKIP' (Get-RaText 'TuiCancelled')
            return $script:RaExitSuccess
        }
        $steamRootPath = $null
        try { $steamRootPath = Get-RaSteamRoot $Options.SteamRoot } catch { Write-RaLog $_.Exception.Message -Level WARN -NoConsole }
        $script:RaAssumeYes = $true
        $run = Get-RaTuiPlan -Action $action -Options $Options -SteamRootPath $steamRootPath
        if ($null -eq $run) {
            Write-RaTuiStatus 'SKIP' (Get-RaText 'TuiCancelled')
            return $script:RaExitSuccess
        }
        Write-RaTuiBlank $Mode
        $working = (Get-RaText "Tui$action") + ': ' + (Get-RaText 'TuiWorking')
        Write-RaTuiModel (Get-RaTuiTextModel -Text $working -Style 'parchment' -Mode $Mode) $Mode
        $code = $script:RaExitFailure
        try { $code = [int]@(Invoke-RaAction $run)[-1] } catch {
            Write-RaLog $_.Exception.Message -Level ERROR
            $code = $script:RaExitFailure
        }
        $purged = [bool]$run.Purge -and $code -eq $script:RaExitSuccess -and -not (Test-Path -LiteralPath $script:RaRoot)
        Write-RaTuiBlank $Mode
        Write-RaTuiModel (Get-RaTuiSummaryModel (Get-RaTuiSummaryRow -Action $action -ExitCode $code -Purged $purged) $Mode) $Mode
        Write-RaTuiBlank $Mode
        return $code
    } finally {
        $script:RaWriteHost = $savedHost
        $script:RaLogSink = $savedSink
        $script:RaAssumeYes = $savedYes
        $script:RaTuiActiveMode = $null
        $script:RaMpWarningShown = $false
    }
}
