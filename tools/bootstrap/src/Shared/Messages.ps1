# ---------------------------------------------------------------------------
# Messages (user-facing text). PL text is \u-escaped to keep this file ASCII and
# is decoded at run time by ConvertFrom-RaEscapedText.
# ---------------------------------------------------------------------------
$script:RaMessages = @{
    en = @{
        Title                      = 'Rename Anything for Hearts of Iron IV - bootstrap'
        Intro                      = @(
            'Proxy mode adds one version.dll to the game directory; Launcher mode keeps the game directory unchanged. ',
            'Workshop and saves are untouched. Log: {0}'
        ) -join ''
        AskLoadMethod              = @(
            'Loading method: 1 = proxy version.dll (recommended, requires -capie); ',
            '2 = external launcher (no game files added). Choice [{0}]'
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
    }
    pl = @{
        Title                      = 'Rename Anything dla Hearts of Iron IV \u2014 bootstrap'
        Intro                      = @(
            'Tryb Proxy dodaje version.dll do folderu gry; tryb Launcher nie zmienia tego folderu. ',
            'Workshop i zapisy pozostaj\u0105 bez zmian. Log: {0}'
        ) -join ''
        AskLoadMethod              = @(
            'Metoda: 1 = proxy version.dll (zalecane, wymaga -capie); ',
            '2 = zewn\u0119trzny launcher (bez plik\u00F3w w folderze gry). Wyb\u00F3r [{0}]'
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
