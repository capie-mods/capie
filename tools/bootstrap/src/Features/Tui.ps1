# ---------------------------------------------------------------------------
# Slice: the terminal interface (irm | iex without switches on an interactive console).
# Splash, one menu (install or update, older version, disable, uninstall), the questions each action needs,
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
        Makes sure Steam is closed before the Steam step, or reports that the step is skipped.
    .DESCRIPTION
        When Steam runs, offers to close it (steam.exe -shutdown, default yes; see Stop-RaSteam).
        When that is declined or Steam does not exit in time, asks the player to close Steam:
        Enter checks again, Q or Esc skips. Steam is never killed.
    .OUTPUTS
        System.Boolean ($true when Steam is closed)
    .EXAMPLE
        if (-not (Wait-RaTuiSteamClosed)) { 'skip the Steam step' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    if (-not (Test-RaSteamRunning)) { return $true }
    if ((Confirm-RaTui -Prompt (Get-RaText 'AskCloseSteam') -Default $true -Mode $script:RaTuiActiveMode) -and (Stop-RaSteam)) {
        return $true
    }
    while (Test-RaSteamRunning) {
        Write-RaTuiStatus 'ASK' (Get-RaText 'TuiAskSteamClosed')
        $key = Read-RaTuiKey
        if ($key -eq $script:RaQuitAnswer -or $key -eq 'Escape') { return $false }
    }
    return $true
}

function Request-RaTuiSteamStep {
    <#
    .SYNOPSIS
        Asks whether the Steam launch option should be set, and prepares Steam for it.
    .DESCRIPTION
        Without a Steam entry yet the question defaults to yes; with one (a repair) it
        defaults to no. A yes needs Steam closed (Wait-RaTuiSteamClosed); with several Steam
        accounts all of them are changed and listed.
    .PARAMETER Run
        Option table of the action (SteamLaunchOption and SteamAccount are set).
    .PARAMETER SteamRootPath
        Steam folder or empty (then there is no Steam step).
    .OUTPUTS
        None.
    .EXAMPLE
        Request-RaTuiSteamStep $run $steam
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][hashtable]$Run,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    $mode = $script:RaTuiActiveMode
    $steam = $false
    if ($SteamRootPath) {
        if (@((Read-RaState).SteamEntries).Count -gt 0) {
            $steam = Confirm-RaTui -Prompt (Get-RaText 'TuiAskSteamRepair') -Default $false -Mode $mode
        } else {
            $steam = Confirm-RaTui -Prompt (Get-RaText 'TuiAskSteam') -Default $true -Mode $mode
        }
    }
    if ($steam -and -not (Wait-RaTuiSteamClosed)) { $steam = $false }
    if ($steam -and @(Get-RaSteamAccount $SteamRootPath).Count -gt 1 -and -not $Run.SteamAccount) {
        Write-RaTuiStatus 'NOTE' (Format-RaAccountList @(Get-RaSteamAccount $SteamRootPath))
        $Run.SteamAccount = $script:RaAllAccountsSelector
    }
    $Run.SteamLaunchOption = $steam
}

function Select-RaTuiRelease {
    <#
    .SYNOPSIS
        Lets the player pick a signed release; returns its version, or $null to go back.
    .DESCRIPTION
        One GitHub request (Get-RaReleaseRowList). Each row shows version, API level, game
        version with the start of its hoi4.exe SHA-256, date, the installed/latest marks,
        up to three changes and a warning when the release is not for the player's hoi4.exe.
        Esc goes back to the menu; a pick is confirmed (default yes).
    .PARAMETER SteamRootPath
        Steam folder or empty (then game support is not shown).
    .OUTPUTS
        System.String
    .EXAMPLE
        $version = Select-RaTuiRelease $steam
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$SteamRootPath)
    $mode = $script:RaTuiActiveMode
    Write-RaTuiStatus 'NOTE' (Get-RaText 'TuiOlderLoading')
    $gameDir = $null
    if ($SteamRootPath) { try { $gameDir = Get-RaGameDir $SteamRootPath } catch { Write-RaLog $_.Exception.Message -Level WARN -NoConsole } }
    try { $rows = Get-RaReleaseRowList (Read-RaState) $gameDir } catch {
        Write-RaTuiStatus 'FAIL' $_.Exception.Message
        return $null
    }
    if ($rows.Count -eq 0) {
        Write-RaTuiStatus 'NOTE' (Get-RaText 'VersionsNone')
        return $null
    }
    Write-RaTuiBlank $mode
    Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiOlderTitle') -Style 'parchment' -Mode $mode) $mode
    Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiOlderKeys') -Style 'ash' -Mode $mode) $mode
    Write-RaTuiBlank $mode
    $index = Select-RaTuiIndex @($rows | ForEach-Object { $_.Row }) $mode
    if ($index -lt 0) { return $null }
    $version = [string]$rows[$index].Release.Version
    Write-RaTuiBlank $mode
    if (-not (Confirm-RaTui -Prompt (Get-RaText 'TuiOlderInstall' @($version)) -Default $true -Mode $mode)) { return $null }
    return $version
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
        Setup (install or update): not installed yet, it installs into the user folder with
        the launcher method (nothing in the game folder; the "CAPIE" switch mod goes to the
        player's mod folder) from the newest signed release when the update channel is
        configured, else the release pinned in this script. Installed, it updates to the
        newest signed release, or repairs when that is already active. Both show the
        multiplayer note and ask about the Steam launch option (Request-RaTuiSteamStep).
        Older: installs the chosen release (-Version) the same way.
        An existing installation keeps its loading method.
        Disable: confirm (default yes). Uninstall: confirm (default no), then whether to purge
        the log and Steam backups (default no). $null means cancelled.
    .PARAMETER Action
        Setup, Older, Disable or Uninstall.
    .PARAMETER Options
        Options of this run (DryRun, SteamRoot, PackagePath, ManifestPath carry over).
    .PARAMETER SteamRootPath
        Steam folder or empty.
    .PARAMETER Release
        With Older: the chosen version.
    .OUTPUTS
        System.Collections.Hashtable
    .EXAMPLE
        $run = Get-RaTuiPlan -Action 'Setup' -Options $options -SteamRootPath $steam
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateSet('Setup', 'Older', 'Disable', 'Uninstall')][string]$Action,
        [Parameter(Mandatory, Position = 1)][hashtable]$Options,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$SteamRootPath,
        [Parameter(Position = 3)][AllowEmptyString()][AllowNull()][string]$Release
    )
    $mode = $script:RaTuiActiveMode
    $run = $Options.Clone()
    switch ($Action) {
        { $_ -in @('Setup', 'Older') } {
            $installed = [bool](Read-RaState).InstalledVersion
            if ($Action -eq 'Older') {
                $run.Install = $true
                $run.Version = $Release
            } elseif ($installed) {
                $run.Update = $true
            } else {
                $run.Install = $true
                $run.Latest = (Test-RaUpdateChannel) -and -not $Options.PackagePath
            }
            if (-not $run.LoadMethod -and -not $installed) { $run.LoadMethod = 'Launcher' }
            Write-RaTuiStatus 'NOTE' (Get-RaText 'TuiMpNote')
            $script:RaMpWarningShown = $true
            Request-RaTuiSteamStep $run $SteamRootPath
        }
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
        $steamRootPath = $null
        try { $steamRootPath = Get-RaSteamRoot $Options.SteamRoot } catch { Write-RaLog $_.Exception.Message -Level WARN -NoConsole }
        $release = $null
        while ($true) {
            Write-RaTuiBlank $Mode
            Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiMenuTitle') -Style 'parchment' -Mode $Mode) $Mode
            Write-RaTuiModel (Get-RaTuiTextModel -Text (Get-RaText 'TuiKeys') -Style 'ash' -Mode $Mode) $Mode
            Write-RaTuiBlank $Mode
            $action = Select-RaTuiAction $Mode
            Write-RaTuiBlank $Mode
            if ($action -ne 'Older') { break }
            $release = Select-RaTuiRelease $steamRootPath
            if ($release) { break }
        }
        if ($action -eq 'Quit') {
            Write-RaTuiStatus 'SKIP' (Get-RaText 'TuiCancelled')
            return $script:RaExitSuccess
        }
        $script:RaAssumeYes = $true
        $run = Get-RaTuiPlan -Action $action -Options $Options -SteamRootPath $steamRootPath -Release $release
        if ($null -eq $run) {
            Write-RaTuiStatus 'SKIP' (Get-RaText 'TuiCancelled')
            if ($script:RaSteamClosedByUs -and (Confirm-RaTui -Prompt (Get-RaText 'AskRestartSteam') -Default $true -Mode $Mode)) {
                Start-RaSteamAgain
            }
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
        if ($script:RaSteamClosedByUs) {
            Write-RaTuiBlank $Mode
            if (Confirm-RaTui -Prompt (Get-RaText 'AskRestartSteam') -Default $true -Mode $Mode) { Start-RaSteamAgain }
            else { $script:RaSteamClosedByUs = $false }
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
