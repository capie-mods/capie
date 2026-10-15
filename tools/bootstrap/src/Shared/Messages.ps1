# ---------------------------------------------------------------------------
# Messages (user-facing text). English only and pure ASCII, so the output looks the
# same in every console code page and in redirected output.
# ---------------------------------------------------------------------------
$script:RaMessages = @{
    Title                      = 'CAPIE (Cyprian''s API Extender) setup for Hearts of Iron IV'
    Intro                      = @(
        'The default launcher method leaves the game folder unchanged and adds the "CAPIE" switch mod to your ',
        'mod folder. The Proxy method adds one version.dll to the game folder. Saves are not touched. Log: {0}'
    ) -join ''
    Usage                      = @(
        "No action was given and this console cannot answer questions, so nothing was changed.`n",
        "Run it again with one action:`n",
        "  -Install              install CAPIE (add -SteamLaunchOption to start it from Steam)`n",
        "  -Install -Latest      install the newest signed release from GitHub Releases`n",
        "  -Update               install a newer signed release; -Rollback returns to the previous one`n",
        "  -Disable              remove the CAPIE launcher from the Steam launch options; files stay`n",
        "  -Uninstall [-Purge]   remove CAPIE; -Purge also removes the log and Steam config backups`n",
        "  -CheckUpdate          report the newest release (one request to GitHub)`n",
        "Add -Yes to run without questions, -DryRun to see the plan first. Interactive console: run with no`n",
        'switches for the menu, or with -Plain for plain questions.'
    ) -join ''
    AskLoadMethod              = @(
        'Loading method: 1 = CAPIE launcher (recommended: no files in the game folder, switched on and off with the ',
        '"CAPIE" mod in the playset); 2 = proxy version.dll in the game folder (needs -capie). Choice [{0}]'
    ) -join ''
    AskModeProxy               = @(
        'Proxy start: 1 = add -capie to the Steam launch options; 4 = install only (add -capie yourself). ',
        "Shortcuts need the launcher method.`nChoice [1,4]"
    ) -join ''
    AskInstall                 = 'Install or update CAPIE in {0}? [Y/n]'
    AskMode                    = @(
        "How should the game start with CAPIE?`n  1 = Steam launch option (the usual Play button)`n  2 ",
        "= Shortcut in a folder you choose`n  3 = Both`n  4 = Neither (install files only)`nChoice [1-4]"
    ) -join ''
    AskShortcutFolder          = @(
        "Where should the shortcut go?`n  1 = Desktop ({0}) [default]`n  2 = Choose a folder in a window`n  or ",
        "type a full folder path`nChoice"
    ) -join ''
    MpWarning                  = @(
        'WARNING: while CAPIE is enabled, Hearts of Iron IV runs with native DLLs. This setup is NOT declared ',
        'multiplayer-compatible: multiplayer has not been tested and desyncs are possible. Undo the change before ',
        "you play multiplayer.`nTo undo: run this script with -Disable (Steam entry only) or -Uninstall (everything), ",
        'or in Steam open Library > Hearts of Iron IV > Properties > General > Launch Options and delete -capie ',
        '(proxy method) or the quoted ...\RenameAnythingLauncher.exe part. With Config activation also delete ',
        '%LOCALAPPDATA%\RenameAnything\proxy.enabled or run -Install -ProxyActivation Flag. Keep your own options.'
    ) -join ''
    AskConfirmSteam            = 'Add the selected loading method to the HOI4 Steam launch options? [y/N]'
    AskConfirmShortcut         = 'Create the shortcut? [y/N]'
    SteamRunning               = @(
        'Steam is running. Close Steam completely (Steam menu > Exit), then press Enter. Type Q to skip the Steam ',
        'step. This script never closes Steam.'
    ) -join ''
    SteamRunningNonInteractive = 'Steam is running. Close it and run the script again. This script never closes Steam.'
    AskAccount                 = 'Several Steam accounts found. Type a number, several numbers separated by commas, or A for all:'
    Done                       = 'Done.'
    DryRunNote                 = 'DRY RUN: nothing will be written.'
    ShortcutComment            = 'HOI4 with CAPIE, the native extension used by Rename Anything. Not declared multiplayer-compatible.'
    NotInstalled               = 'The launcher is not installed yet. Run with -Install first.'
    UninstallDone              = 'Uninstalled. Workshop mods and saves were not touched. The log was kept: {0}'
    Failed                     = 'FAILED: {0}'
    UpdateStatus               = 'Installed version: {0}. Latest release: {1}.'
    UpdateAvailable            = 'Version {0} is available. Run with -Update to install it; the current version stays for -Rollback.'
    UpToDate                   = 'You have the latest version.'
    GameSupportedInstalled     = 'The installed version supports your hoi4.exe build.'
    GameSupportedLatest        = 'The installed version does NOT support your hoi4.exe build. Version {0} does: run with -Update.'
    GameNotSupported           = @(
        'Your hoi4.exe build ({0}...) is not supported yet. The launcher starts the game without CAPIE. Check again ',
        'later (release notes, versions.json).'
    ) -join ''
    DisableDone                = 'Disabled. Steam starts the game without CAPIE. Files stay installed; run -SteamLaunchOption to turn it on again.'
    PurgeDone                  = 'Uninstalled. No files of this setup are left, and the Steam launch options are as they were before.'
    ReleaseVerified            = 'Release {0}: signed manifest verified.'
    WorkshopModPresent         = @(
        'The CAPIE mod from the Workshop is subscribed ({0}), so no local copy is installed. Keep only one: ',
        'with two mods of the same name the game skips the Workshop one.'
    ) -join ''
    TuiTagline                 = "Built on CAPIE (Cyprian's API Extender)"
    TuiMenuTitle               = 'What should this setup do?'
    TuiInstall                 = 'Install'
    TuiInstallHint             = 'CAPIE launcher in your user folder, started by Steam'
    TuiUpdate                  = 'Update'
    TuiUpdateHint              = 'newest signed release; the current one stays for rollback'
    TuiDisable                 = 'Disable'
    TuiDisableHint             = 'Steam starts the game without CAPIE; files stay'
    TuiUninstall               = 'Uninstall'
    TuiUninstallHint           = 'remove CAPIE and restore the earlier state'
    TuiQuit                    = 'Quit'
    TuiQuitHint                = 'change nothing'
    TuiKeys                    = 'Up and Down or 1-5 to choose, Enter to confirm, Esc to quit.'
    TuiStatusNone              = 'CAPIE is not installed.'
    TuiStatusInstalled         = 'Installed: CAPIE {0}, {1}.'
    TuiSteamOn                 = 'started by Steam'
    TuiSteamOff                = 'not in the Steam launch options'
    TuiMpNote                  = @(
        'While CAPIE is loaded, the game is not declared multiplayer-compatible. Before you play multiplayer, ',
        'disable the "CAPIE" mod in your playset or choose Disable here.'
    ) -join ''
    TuiAskSteam                = 'Add the CAPIE launcher to the HOI4 Steam launch options? [Y/n]'
    TuiAskDisable              = 'Remove the CAPIE launcher from the Steam launch options? Files stay. [Y/n]'
    TuiAskUninstall            = 'Remove CAPIE and restore the Steam launch options you had before? [y/N]'
    TuiAskPurge                = 'Also delete the setup log and the Steam config backups made by this setup? [y/N]'
    TuiAskSteamClosed          = 'Steam is running. Close it completely (Steam > Exit), then press Enter. Q skips the Steam step.'
    TuiWorking                 = 'working'
    TuiSummary                 = 'Summary'
    TuiRowInstalled            = 'Installed'
    TuiRowSteam                = 'Steam'
    TuiRowGame                 = 'Game folder'
    TuiRowSwitch               = 'Switch'
    TuiRowUndo                 = 'Undo'
    TuiRowLog                  = 'Log'
    TuiRowMod                  = 'CAPIE mod'
    TuiRowResult               = 'Result'
    TuiGameUnchanged           = 'unchanged'
    TuiNone                    = 'nothing'
    TuiSwitchValue             = 'enable the "CAPIE" mod in your playset to load CAPIE; disable it to play without'
    TuiUndoValue               = 'run this setup again and choose Disable or Uninstall'
    TuiFailed                  = 'The step failed. Details are in the log: {0}'
    TuiYes                     = 'yes'
    TuiNo                      = 'no'
    TuiDone                    = 'Done.'
    TuiCancelled               = 'Cancelled. Nothing was changed.'
    TuiRemovedAll              = 'removed; no files of this setup are left'
    TuiRemovedKeptLog          = 'removed; the log was kept'
}

function Get-RaText {
    <#
    .SYNOPSIS
        Returns a message, optionally formatted.
    .DESCRIPTION
        Looks the key up in $script:RaMessages (English, ASCII). An unknown key throws, so a
        typo cannot reach the user as an empty line.
    .PARAMETER Key
        Message key in $script:RaMessages.
    .PARAMETER FormatArgs
        Values for the {0}, {1}... placeholders.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaText 'UpdateStatus' @('0.1.0', '0.2.0')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Key,
        [Parameter(Position = 1)][object[]]$FormatArgs = @()
    )
    if (-not $script:RaMessages.ContainsKey($Key)) { throw "Unknown message key: $Key" }
    $text = [string]$script:RaMessages[$Key]
    if ($FormatArgs.Count -gt 0) { return ($text -f $FormatArgs) }
    return $text
}
