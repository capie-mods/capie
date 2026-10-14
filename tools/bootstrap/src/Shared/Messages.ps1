# ---------------------------------------------------------------------------
# Messages (user-facing text). PL text is \u-escaped to keep this file ASCII and
# is decoded at run time by ConvertFrom-RaEscapedText.
# ---------------------------------------------------------------------------
$script:RaMessages = @{
    en = @{
        Title                      = 'Rename Anything for Hearts of Iron IV - bootstrap'
        Intro                      = @(
            'The CAPIE launcher (default) keeps the game folder unchanged and adds the "CAPIE" switch mod to your ',
            'mod folder; the Proxy alternative adds one version.dll to the game folder. Saves are untouched. Log: {0}'
        ) -join ''
        AskLoadMethod              = @(
            'Loading method: 1 = CAPIE launcher (recommended; switch CAPIE with the "CAPIE" mod in the playset, ',
            'no game files added); 2 = proxy version.dll in the game folder (requires -capie). Choice [{0}]'
        ) -join ''
        AskModeProxy               = @(
            'Proxy activation: 1 = add -capie to Steam launch options; 4 = install only (add -capie manually). ',
            "Shortcuts require Launcher mode.`nChoice [1,4]"
        ) -join ''
        AskInstall                 = 'Install/update the Rename Anything launcher and native DLLs into {0}? [Y/n]'
        AskMode                    = @(
            "How should the game start WITH the extension?`n  1 = Steam launch option (normal 'Play' button in Steam)`n  2 ",
            "= Shortcut in a folder you choose`n  3 = Both`n  4 = Neither (only install files)`nChoice [1-4]"
        ) -join ''
        AskShortcutFolder          = @(
            "Where should the shortcut be created?`n  1 = Desktop ({0}) [default]`n  2 = Choose a folder in a window`n  or ",
            "type a full folder path`nChoice"
        ) -join ''
        MpWarning                  = @(
            'WARNING: while the extension is enabled, Hearts of Iron IV runs with CAPIE native DLLs. This setup is ',
            'NOT declared multiplayer-compatible (multiplayer has not been verified; desyncs are possible). Play ',
            "multiplayer only after undoing the change.`nHow to undo: run this script with -RemoveSteamLaunchOption (Steam ",
            'entry only) or -Uninstall (everything), or in Steam: Library > Hearts of Iron IV > Properties > General > ',
            'Launch Options and delete -capie (proxy mode) or the quoted ...\RenameAnythingLauncher.exe part. For Config ',
            'activation remove %LOCALAPPDATA%\RenameAnything\proxy.enabled or run -ProxyActivation Flag -Install. Keep your own options.'
        ) -join ''
        AskConfirmSteam            = 'Enable the selected loading method in the HOI4 Steam launch options? [y/N]'
        AskConfirmShortcut         = 'Create the shortcut? [y/N]'
        SteamRunning               = @(
            'Steam is running. Close Steam completely (Steam menu > Exit), then press Enter. Type Q to skip the Steam ',
            'step. The script never closes Steam itself.'
        ) -join ''
        SteamRunningNonInteractive = 'Steam is running; close it and run again. Steam is never closed by this script.'
        AskAccount                 = 'Several Steam accounts found. Type a number, several numbers separated by commas, or A for all:'
        Done                       = 'Done.'
        DryRunNote                 = 'DRY RUN: nothing will be written.'
        ShortcutComment            = 'HOI4 with the Rename Anything native extension (DLL). Not declared multiplayer-compatible.'
        NotInstalled               = 'The launcher is not installed yet. Run with -Install first.'
        UninstallDone              = 'Uninstalled. Workshop mod and saves were not touched. The log file was kept: {0}'
        Failed                     = 'FAILED: {0}'
        UpdateStatus               = 'Installed version: {0}; latest release: {1}.'
        UpdateAvailable            = 'A new version {0} is available. Run with -Update to install it (the previous version is kept for -Rollback).'
        UpToDate                   = 'You have the latest version.'
        GameSupportedInstalled     = 'Your hoi4.exe build is supported by the installed version.'
        GameSupportedLatest        = 'Your hoi4.exe build is NOT supported by the installed version, but version {0} supports it: run with -Update.'
        GameNotSupported           = @(
            'Your hoi4.exe build ({0}...) is not supported yet. The launcher starts the game without the DLL; check again ',
            'later (release notes / versions.json).'
        ) -join ''
        DisableDone                = 'Disabled: Steam starts the clean game. Files stay installed; run -SteamLaunchOption to enable again.'
        PurgeDone                  = 'Uninstalled. Nothing of this setup is left; the Steam launch options are as before the installation.'
        ReleaseVerified            = 'Release {0}: signed manifest verified.'
        TuiTagline                 = "Built on CAPIE (Cyprian's API Extender)"
        TuiMenuTitle               = 'What should this setup do?'
        TuiInstall                 = 'Install'
        TuiInstallHint             = 'CAPIE launcher in your user folder, started by Steam'
        TuiUpdate                  = 'Update'
        TuiUpdateHint              = 'newest signed release; the previous one stays for rollback'
        TuiDisable                 = 'Disable'
        TuiDisableHint             = 'Steam starts the clean game; files stay'
        TuiUninstall               = 'Uninstall'
        TuiUninstallHint           = 'restore the state before the installation'
        TuiQuit                    = 'Quit'
        TuiQuitHint                = 'change nothing'
        TuiKeys                    = 'Up and Down or 1-5 to choose, Enter to confirm, Esc to quit.'
        TuiStatusNone              = 'CAPIE is not installed.'
        TuiStatusInstalled         = 'Installed: CAPIE {0}, {1}.'
        TuiSteamOn                 = 'started by Steam'
        TuiSteamOff                = 'not in the Steam launch options'
        TuiMpNote                  = @(
            'While CAPIE is loaded, the game is not declared multiplayer-compatible. Before multiplayer, ',
            'disable the "CAPIE" mod in your playset or choose Disable here.'
        ) -join ''
        TuiAskSteam                = 'Add the CAPIE launcher to the HOI4 Steam launch options? [Y/n]'
        TuiAskDisable              = 'Remove the CAPIE launcher from the Steam launch options? Files stay. [Y/n]'
        TuiAskUninstall            = 'Remove CAPIE and restore the Steam launch options from before the installation? [y/N]'
        TuiAskPurge                = 'Also delete the setup log and the Steam config backups this setup made? [y/N]'
        TuiAskSteamClosed          = 'Steam is running. Close it completely (Steam > Exit), then press Enter. Q skips the Steam step.'
        TuiWorking                 = 'Working'
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
        TuiSwitchValue             = 'enable the "CAPIE" mod in your playset to load CAPIE; disable it for the clean game'
        TuiUndoValue               = 'run this setup again and choose Disable or Uninstall'
        TuiNothingChanged          = 'Nothing was changed.'
        TuiFailed                  = 'The step failed. Details in the log: {0}'
        TuiYes                     = 'yes'
        TuiNo                      = 'no'
        TuiDone                    = 'Done.'
        TuiCancelled               = 'Cancelled. Nothing was changed.'
        TuiRemovedAll              = 'removed; no files of this setup are left'
        TuiRemovedKeptLog          = 'removed; the log was kept'
    }
    pl = @{
        Title                      = 'Rename Anything dla Hearts of Iron IV \u2014 bootstrap'
        Intro                      = @(
            'Launcher CAPIE (domy\u015Blnie) nie zmienia folderu gry i dodaje mod-prze\u0142\u0105cznik \u201ECAPIE\u201D ',
            'do folderu mod\u00F3w; alternatywa Proxy dodaje version.dll do folderu gry. Zapisy bez zmian. Log: {0}'
        ) -join ''
        AskLoadMethod              = @(
            'Metoda: 1 = launcher CAPIE (zalecane; CAPIE w\u0142\u0105czasz modem \u201ECAPIE\u201D w zestawie, ',
            'bez plik\u00F3w w folderze gry); 2 = proxy version.dll w folderze gry (wymaga -capie). Wyb\u00F3r [{0}]'
        ) -join ''
        AskModeProxy               = @(
            'Aktywacja Proxy: 1 = dodaj -capie w opcjach Steam; 4 = sama instalacja (dodaj -capie r\u0119cznie). ',
            "Skr\u00F3ty wymagaj\u0105 trybu Launcher.`nWyb\u00F3r [1,4]"
        ) -join ''
        AskInstall                 = 'Zainstalowa\u0107/zaktualizowa\u0107 launcher Rename Anything i natywne DLL w {0}? [T/n]'
        AskMode                    = @(
            "Jak uruchamia\u0107 gr\u0119 Z rozszerzeniem?`n  1 = opcja uruchamiania w Steam (zwyk\u0142y przycisk ",
            "\u201EGraj\u201D)`n  2 = skr\u00F3t w wybranym folderze`n  3 = oba`n  4 = \u017Cadne (tylko instalacja ",
            "plik\u00F3w)`nWyb\u00F3r [1-4]"
        ) -join ''
        AskShortcutFolder          = @(
            "Gdzie utworzy\u0107 skr\u00F3t?`n  1 = Pulpit ({0}) [domy\u015Blnie]`n  2 = wybierz folder w oknie`n  albo ",
            "wpisz pe\u0142n\u0105 \u015Bcie\u017Ck\u0119 folderu`nWyb\u00F3r"
        ) -join ''
        MpWarning                  = @(
            'OSTRZE\u017BENIE: przy w\u0142\u0105czonym rozszerzeniu Hearts of Iron IV dzia\u0142a ze ',
            'natywnymi bibliotekami DLL CAPIE. Taka konfiguracja NIE jest deklarowana jako zgodna z ',
            'trybem wieloosobowym (MP nie by\u0142 weryfikowany; mo\u017Cliwe desynchronizacje). Gr\u0119 ',
            "wieloosobow\u0105 uruchamiaj dopiero po cofni\u0119ciu zmiany.`nJak cofn\u0105\u0107: uruchom ten skrypt z ",
            '-RemoveSteamLaunchOption (tylko wpis Steam) albo -Uninstall (wszystko), albo w Steam: Biblioteka > Hearts of ',
            'Iron IV > W\u0142a\u015Bciwo\u015Bci > Og\u00F3lne > Opcje uruchamiania i usu\u0144 fragment w ',
            '-capie (Proxy) albo w cudzys\u0142owie ...\RenameAnythingLauncher.exe. Przy aktywacji Config usu\u0144 ',
            '%LOCALAPPDATA%\RenameAnything\proxy.enabled lub uruchom -ProxyActivation Flag -Install. W\u0142asne opcje zostaw.'
        ) -join ''
        AskConfirmSteam            = 'W\u0142\u0105czy\u0107 wybran\u0105 metod\u0119 w opcjach uruchamiania HOI4 w Steam? [t/N]'
        AskConfirmShortcut         = 'Utworzy\u0107 skr\u00F3t? [t/N]'
        SteamRunning               = @(
            'Steam jest uruchomiony. Zamknij Steam ca\u0142kowicie (menu Steam > Zako\u0144cz), potem naci\u015Bnij Enter. ',
            'Wpisz Q, aby pomin\u0105\u0107 krok Steam. Skrypt nigdy sam nie zamyka Steam.'
        ) -join ''
        SteamRunningNonInteractive = 'Steam jest uruchomiony; zamknij go i uruchom ponownie. Skrypt nigdy nie zamyka Steam.'
        AskAccount                 = 'Znaleziono kilka kont Steam. Wpisz numer, kilka numer\u00F3w po przecinku albo A dla wszystkich:'
        Done                       = 'Gotowe.'
        DryRunNote                 = 'TRYB PR\u00D3BNY: nic nie zostanie zapisane.'
        ShortcutComment            = 'HOI4 z natywnym rozszerzeniem Rename Anything (DLL). Niedeklarowane jako zgodne z trybem wieloosobowym.'
        NotInstalled               = 'Launcher nie jest jeszcze zainstalowany. Najpierw uruchom z -Install.'
        UninstallDone              = 'Odinstalowano. Mod z Workshop i zapisy nie zosta\u0142y ruszone. Plik logu zachowano: {0}'
        Failed                     = 'B\u0141\u0104D: {0}'
        UpdateStatus               = 'Zainstalowana wersja: {0}; najnowsze wydanie: {1}.'
        UpdateAvailable            = @(
            'Dost\u0119pna jest nowa wersja {0}. Uruchom z -Update, aby j\u0105 zainstalowa\u0107 (poprzednia zostaje do ',
            '-Rollback).'
        ) -join ''
        UpToDate                   = 'Masz najnowsz\u0105 wersj\u0119.'
        GameSupportedInstalled     = 'Twoja wersja hoi4.exe jest wspierana przez zainstalowan\u0105 wersj\u0119.'
        GameSupportedLatest        = @(
            'Twoja wersja hoi4.exe NIE jest wspierana przez zainstalowan\u0105 wersj\u0119, ale wspiera j\u0105 wersja ',
            '{0}: uruchom z -Update.'
        ) -join ''
        GameNotSupported           = @(
            'Twoja wersja hoi4.exe ({0}...) nie jest jeszcze wspierana. Launcher uruchomi gr\u0119 bez DLL; sprawd\u017A ',
            'p\u00F3\u017Aniej (release notes / versions.json).'
        ) -join ''
        DisableDone                = @(
            'Wy\u0142\u0105czono: Steam uruchamia czyst\u0105 gr\u0119. Pliki zostaj\u0105; aby w\u0142\u0105czy\u0107 ',
            'ponownie, uruchom z -SteamLaunchOption.'
        ) -join ''
        PurgeDone                  = 'Odinstalowano. Nic z tej instalacji nie zosta\u0142o; opcje uruchamiania w Steam s\u0105 jak przed instalacj\u0105.'
        ReleaseVerified            = 'Wydanie {0}: podpis manifestu sprawdzony.'
        TuiTagline                 = "Zbudowane na CAPIE (Cyprian's API Extender)"
        TuiMenuTitle               = 'Co ma zrobi\u0107 instalator?'
        TuiInstall                 = 'Zainstaluj'
        TuiInstallHint             = 'launcher CAPIE w folderze u\u017Cytkownika, start przez Steam'
        TuiUpdate                  = 'Aktualizuj'
        TuiUpdateHint              = 'najnowsze podpisane wydanie; poprzednie zostaje'
        TuiDisable                 = 'Wy\u0142\u0105cz'
        TuiDisableHint             = 'Steam uruchamia czyst\u0105 gr\u0119; pliki zostaj\u0105'
        TuiUninstall               = 'Odinstaluj'
        TuiUninstallHint           = 'przywr\u00F3\u0107 stan sprzed instalacji'
        TuiQuit                    = 'Wyjd\u017A'
        TuiQuitHint                = 'nic nie zmieniaj'
        TuiKeys                    = 'Strza\u0142ki albo 1-5 wybieraj\u0105, Enter zatwierdza, Esc ko\u0144czy.'
        TuiStatusNone              = 'CAPIE nie jest zainstalowane.'
        TuiStatusInstalled         = 'Zainstalowano: CAPIE {0}, {1}.'
        TuiSteamOn                 = 'uruchamiane przez Steam'
        TuiSteamOff                = 'poza opcjami uruchamiania Steam'
        TuiMpNote                  = @(
            'Gdy CAPIE jest za\u0142adowane, gra nie jest deklarowana jako zgodna z trybem wieloosobowym. Przed gr\u0105 ',
            'wieloosobow\u0105 wy\u0142\u0105cz mod \u201ECAPIE\u201D w zestawie mod\u00F3w albo wybierz tutaj \u201EWy\u0142\u0105cz\u201D.'
        ) -join ''
        TuiAskSteam                = 'Doda\u0107 launcher CAPIE do opcji uruchamiania HOI4 w Steam? [T/n]'
        TuiAskDisable              = 'Usun\u0105\u0107 launcher CAPIE z opcji uruchamiania Steam? Pliki zostaj\u0105. [T/n]'
        TuiAskUninstall            = 'Usun\u0105\u0107 CAPIE i przywr\u00F3ci\u0107 opcje uruchamiania Steam sprzed instalacji? [t/N]'
        TuiAskPurge                = 'Usun\u0105\u0107 te\u017C log instalatora i kopie konfiguracji Steam zrobione przez instalator? [t/N]'
        TuiAskSteamClosed          = @(
            'Steam jest uruchomiony. Zamknij go ca\u0142kowicie (Steam > Zako\u0144cz), potem naci\u015Bnij Enter. ',
            'Q pomija krok Steam.'
        ) -join ''
        TuiWorking                 = 'W toku'
        TuiSummary                 = 'Podsumowanie'
        TuiRowInstalled            = 'Zainstalowano'
        TuiRowSteam                = 'Steam'
        TuiRowGame                 = 'Folder gry'
        TuiRowSwitch               = 'Prze\u0142\u0105cznik'
        TuiRowUndo                 = 'Cofni\u0119cie'
        TuiRowLog                  = 'Log'
        TuiRowMod                  = 'Mod CAPIE'
        TuiRowResult               = 'Wynik'
        TuiGameUnchanged           = 'bez zmian'
        TuiNone                    = 'nic'
        TuiSwitchValue             = @(
            'w\u0142\u0105cz mod \u201ECAPIE\u201D w zestawie mod\u00F3w, aby \u0142adowa\u0107 CAPIE; ',
            'wy\u0142\u0105cz go dla czystej gry'
        ) -join ''
        TuiUndoValue               = 'uruchom instalator ponownie i wybierz \u201EWy\u0142\u0105cz\u201D albo \u201EOdinstaluj\u201D'
        TuiNothingChanged          = 'Nic nie zmieniono.'
        TuiFailed                  = 'Krok si\u0119 nie powi\u00F3d\u0142. Szczeg\u00F3\u0142y w logu: {0}'
        TuiYes                     = 'tak'
        TuiNo                      = 'nie'
        TuiDone                    = 'Gotowe.'
        TuiCancelled               = 'Anulowano. Nic nie zmieniono.'
        TuiRemovedAll              = 'usuni\u0119to; nie zosta\u0142y \u017Cadne pliki tej instalacji'
        TuiRemovedKeptLog          = 'usuni\u0119to; log zachowano'
    }
}

function ConvertFrom-RaEscapedText {
    <#
    .SYNOPSIS
        Decodes \uXXXX escapes into characters.
    .DESCRIPTION
        Lets the script stay pure ASCII while showing Polish text correctly on
        Windows PowerShell 5.1 and under irm | iex.
    .PARAMETER Text
        Text with optional \uXXXX escapes; $null is returned unchanged.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertFrom-RaEscapedText 'Gotowe \u2014 OK'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Text)
    if ($null -eq $Text) { return $null }
    return [regex]::Replace($Text, $script:RaUnicodeEscapePattern, {
            param($m) [string][char][Convert]::ToInt32($m.Groups[1].Value, 16)
        })
}

function Get-RaText {
    <#
    .SYNOPSIS
        Returns a localized message, optionally formatted.
    .DESCRIPTION
        Looks the key up in the current language ($script:RaLang) and falls back to
        English for unknown languages or keys.
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
    $table = $script:RaMessages[$script:RaLang]
    if (-not $table -or -not $table.ContainsKey($Key)) { $table = $script:RaMessages[$script:RaDefaultLanguage] }
    $text = ConvertFrom-RaEscapedText $table[$Key]
    if ($FormatArgs.Count -gt 0) { return ($text -f $FormatArgs) }
    return $text
}

function Get-RaDefaultLanguage {
    <#
    .SYNOPSIS
        Picks the message language from the Windows UI language.
    .DESCRIPTION
        Polish UI -> 'pl', anything else (or an error) -> 'en'.
    .OUTPUTS
        System.String
    .EXAMPLE
        $script:RaLang = Get-RaDefaultLanguage
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    try {
        if ((& $script:RaGetUiLanguage) -eq $script:RaPolishLanguage) { return $script:RaPolishLanguage }
    } catch { Write-Verbose "UI language unavailable: $($_.Exception.Message)" }
    return $script:RaDefaultLanguage
}
