<#
.SYNOPSIS
    Rename Anything (HOI4) bootstrap: installs the native launcher bundle and
    optionally wires it into Steam launch options and/or a game shortcut.

.DESCRIPTION
    Runs on Windows PowerShell 5.1 and PowerShell 7, without administrator rights
    and without external modules. It never modifies the HOI4 installation, Steam
    Workshop content or saves. See tools/bootstrap/README.md.

    Interactive (no action switches):
        irm <release-url>/install.ps1 | iex
    Noninteractive:
        & ([scriptblock]::Create((irm <release-url>/install.ps1))) -Install -SteamLaunchOption -Yes
        .\install.ps1 -Install -Shortcut -ShortcutPath "$env:USERPROFILE\Desktop" -Yes

    This file is intentionally pure ASCII (Polish text uses \uXXXX escapes decoded at
    runtime), because Windows PowerShell 5.1 and `irm` may decode non-ASCII scripts
    with a legacy code page.
#>
[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Shortcut,
    [string]$ShortcutPath,
    [switch]$SteamLaunchOption,
    [string]$SteamAccount,
    [switch]$RemoveSteamLaunchOption,
    [switch]$Uninstall,
    [Alias('WhatIf')][switch]$DryRun,
    [switch]$Yes,
    [string]$PackagePath,
    [string]$SteamRoot,
    [switch]$CheckUpdate,
    [switch]$Update,
    [switch]$Rollback,
    [string]$ManifestPath,
    [ValidateSet('', 'pl', 'en')][string]$Language = ''
)

# ---------------------------------------------------------------------------
# Configuration (single place to change when the repository becomes public)
# ---------------------------------------------------------------------------
# GitHub "owner/repo". Release downloads and the (on-demand only) update check derive from it.
$script:RaGitHubRepo = 'PLACEHOLDER-OWNER/hoi4-rename-anything'
# RSA public key (XML, >= 2048 bit) that signs release-manifest.json of every later release.
# Placeholder until the maintainer generates the key (tools/bootstrap/sign-release-manifest.ps1).
$script:RaUpdatePublicKeyXml = 'TO-BE-GENERATED'
$script:RaAppId = '394360'
$script:RaLauncherExeName = 'RenameAnythingLauncher.exe'
$script:RaShortcutFileName = 'Hearts of Iron IV + Rename Anything.lnk'

# Pinned release manifest. The ZIP hash is checked BEFORE extraction and every
# extracted file is checked against its own pinned hash; any mismatch, missing or
# extra file aborts the install (fail-closed). File hashes below are the current
# reviewed launcher-candidate bundle; ZipSha256 must be filled when the release ZIP
# is built. Until then the installer refuses to install anything.
$script:RaPinnedRelease = [ordered]@{
    Version   = '0.1.0'
    ZipName   = 'RenameAnything-0.1.0-win64.zip'
    ZipSha256 = 'TO-BE-PINNED-AT-RELEASE'
    Files     = [ordered]@{
        'RenameAnythingLauncher.exe'                     = '1AEAC3C9B0717491E239A5C263AB27ECF53DDD536317DEA69CCCADC26B9C785A'
        'LICENSE.MinHook.txt'                            = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/rename_anything_country.dll'            = '18841A6D7552224F0D34BBC05983916912969735109A548C16DE4581845E10B5'
        'runtime/rename_anything_injector.exe'           = 'DF8D60E9F1C403193DAC79FE5229793569E30D01D17F5172C5B11F1CB14063C7'
        'runtime/rename_anything_inline_editor_bridge.dll' = 'B20FBE88756F7F22D989230D6D51953D82BCCD2349CD00BDCC545C382DF8BAF6'
        'runtime/inject_inline_editor_render_bridge.exe' = '31B510BECD5DCBA3588231C8312C32B1F2C2CC49B0CAFC8BBB2A51B18F455AE4'
    }
    # hoi4.exe builds this release's DLLs support (for -CheckUpdate messages; the launcher enforces it).
    SupportedGameSha256 = @('7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5')
}

# Test seams (tests override these after dot-sourcing; normal runs never change them).
$script:RaRoot = $null
$script:RaLang = 'en'
$script:RaDry = $false
$script:RaAssumeYes = $false
$script:RaReadHost = { param([string]$Prompt) Read-Host -Prompt $Prompt }
$script:RaGetSteamProcess = { @(Get-Process -Name 'steam' -ErrorAction SilentlyContinue) }
$script:RaDesktopPath = { [Environment]::GetFolderPath('Desktop') }
$script:RaFolderPicker = $null   # $null = real FolderBrowserDialog
$script:RaAfterWriteHook = $null # tests: simulate a bad write before verification
$script:RaQuiet = $false
# Only network access of this script. Called solely by -Install (ZIP download), -CheckUpdate and
# -Update. Sends no identifiers: just a fixed User-Agent required by the GitHub API.
$script:RaHttpGet = {
    param([string]$Url)
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
    $wc = New-Object System.Net.WebClient
    try {
        $wc.Headers['User-Agent'] = 'RenameAnything-Bootstrap'
        $wc.Headers['Accept'] = 'application/vnd.github+json, application/octet-stream'
        return , $wc.DownloadData($Url)
    } finally { $wc.Dispose() }
}

# ---------------------------------------------------------------------------
# Messages (PL text is \u-escaped to keep this file ASCII)
# ---------------------------------------------------------------------------
$script:RaMessages = @{
    en = @{
        Title             = 'Rename Anything for Hearts of Iron IV - bootstrap'
        Intro             = 'This script does not modify the game installation, Workshop content or saves. Log: {0}'
        AskInstall        = 'Install/update the Rename Anything launcher and native DLLs into {0}? [Y/n]'
        AskMode           = "How should the game start WITH the extension?`n  1 = Steam launch option (normal 'Play' button in Steam)`n  2 = Shortcut in a folder you choose`n  3 = Both`n  4 = Neither (only install files)`nChoice [1-4]"
        AskShortcutFolder = "Where should the shortcut be created?`n  1 = Desktop ({0}) [default]`n  2 = Choose a folder in a window`n  or type a full folder path`nChoice"
        MpWarning         = "WARNING: while the extension is enabled, Hearts of Iron IV runs with an injected native DLL. This setup is NOT declared multiplayer-compatible (multiplayer has not been verified; desyncs are possible). Play multiplayer only after undoing the change.`nHow to undo: run this script with -RemoveSteamLaunchOption (Steam entry only) or -Uninstall (everything), or in Steam: Library > Hearts of Iron IV > Properties > General > Launch Options and delete the quoted ...\RenameAnythingLauncher.exe part (keep your own options). Deleting the shortcut is enough for the shortcut mode."
        AskConfirmSteam   = 'Add the launcher to the HOI4 Steam launch options? [y/N]'
        AskConfirmShortcut = 'Create the shortcut? [y/N]'
        SteamRunning      = 'Steam is running. Close Steam completely (Steam menu > Exit), then press Enter. Type Q to skip the Steam step. The script never closes Steam itself.'
        SteamRunningNonInteractive = 'Steam is running; close it and run again. Steam is never closed by this script.'
        AskAccount        = 'Several Steam accounts found. Type a number, several numbers separated by commas, or A for all:'
        Done              = 'Done.'
        DryRunNote        = 'DRY RUN: nothing will be written.'
        ShortcutComment   = 'HOI4 with the Rename Anything native extension (DLL). Not declared multiplayer-compatible.'
        NotInstalled      = 'The launcher is not installed yet. Run with -Install first.'
        UninstallDone     = 'Uninstalled. Workshop mod and saves were not touched. The log file was kept: {0}'
        Failed            = 'FAILED: {0}'
        UpdateStatus      = 'Installed version: {0}; latest release: {1}.'
        UpdateAvailable   = 'A new version {0} is available. Run with -Update to install it (the previous version is kept for -Rollback).'
        UpToDate          = 'You have the latest version.'
        GameSupportedInstalled = 'Your hoi4.exe build is supported by the installed version.'
        GameSupportedLatest = 'Your hoi4.exe build is NOT supported by the installed version, but version {0} supports it: run with -Update.'
        GameNotSupported  = 'Your hoi4.exe build ({0}...) is not supported yet. The launcher starts the game without the DLL; check again later (release notes / versions.json).'
    }
    pl = @{
        Title             = 'Rename Anything dla Hearts of Iron IV \u2014 bootstrap'
        Intro             = 'Skrypt nie zmienia instalacji gry, zawarto\u015Bci Workshop ani zapis\u00F3w. Log: {0}'
        AskInstall        = 'Zainstalowa\u0107/zaktualizowa\u0107 launcher Rename Anything i natywne DLL w {0}? [T/n]'
        AskMode           = "Jak uruchamia\u0107 gr\u0119 Z rozszerzeniem?`n  1 = opcja uruchamiania w Steam (zwyk\u0142y przycisk \u201EGraj\u201D)`n  2 = skr\u00F3t w wybranym folderze`n  3 = oba`n  4 = \u017Cadne (tylko instalacja plik\u00F3w)`nWyb\u00F3r [1-4]"
        AskShortcutFolder = "Gdzie utworzy\u0107 skr\u00F3t?`n  1 = Pulpit ({0}) [domy\u015Blnie]`n  2 = wybierz folder w oknie`n  albo wpisz pe\u0142n\u0105 \u015Bcie\u017Ck\u0119 folderu`nWyb\u00F3r"
        MpWarning         = "OSTRZE\u017BENIE: przy w\u0142\u0105czonym rozszerzeniu Hearts of Iron IV dzia\u0142a ze wstrzykni\u0119t\u0105 natywn\u0105 bibliotek\u0105 DLL. Taka konfiguracja NIE jest deklarowana jako zgodna z trybem wieloosobowym (MP nie by\u0142 weryfikowany; mo\u017Cliwe desynchronizacje). Gr\u0119 wieloosobow\u0105 uruchamiaj dopiero po cofni\u0119ciu zmiany.`nJak cofn\u0105\u0107: uruchom ten skrypt z -RemoveSteamLaunchOption (tylko wpis Steam) albo -Uninstall (wszystko), albo w Steam: Biblioteka > Hearts of Iron IV > W\u0142a\u015Bciwo\u015Bci > Og\u00F3lne > Opcje uruchamiania i usu\u0144 fragment w cudzys\u0142owie ...\RenameAnythingLauncher.exe (w\u0142asne opcje zostaw). W trybie skr\u00F3tu wystarczy usun\u0105\u0107 skr\u00F3t."
        AskConfirmSteam   = 'Doda\u0107 launcher do opcji uruchamiania HOI4 w Steam? [t/N]'
        AskConfirmShortcut = 'Utworzy\u0107 skr\u00F3t? [t/N]'
        SteamRunning      = 'Steam jest uruchomiony. Zamknij Steam ca\u0142kowicie (menu Steam > Zako\u0144cz), potem naci\u015Bnij Enter. Wpisz Q, aby pomin\u0105\u0107 krok Steam. Skrypt nigdy sam nie zamyka Steam.'
        SteamRunningNonInteractive = 'Steam jest uruchomiony; zamknij go i uruchom ponownie. Skrypt nigdy nie zamyka Steam.'
        AskAccount        = 'Znaleziono kilka kont Steam. Wpisz numer, kilka numer\u00F3w po przecinku albo A dla wszystkich:'
        Done              = 'Gotowe.'
        DryRunNote        = 'TRYB PR\u00D3BNY: nic nie zostanie zapisane.'
        ShortcutComment   = 'HOI4 z natywnym rozszerzeniem Rename Anything (DLL). Niedeklarowane jako zgodne z trybem wieloosobowym.'
        NotInstalled      = 'Launcher nie jest jeszcze zainstalowany. Najpierw uruchom z -Install.'
        UninstallDone     = 'Odinstalowano. Mod z Workshop i zapisy nie zosta\u0142y ruszone. Plik logu zachowano: {0}'
        Failed            = 'B\u0141\u0104D: {0}'
        UpdateStatus      = 'Zainstalowana wersja: {0}; najnowsze wydanie: {1}.'
        UpdateAvailable   = 'Dost\u0119pna jest nowa wersja {0}. Uruchom z -Update, aby j\u0105 zainstalowa\u0107 (poprzednia zostaje do -Rollback).'
        UpToDate          = 'Masz najnowsz\u0105 wersj\u0119.'
        GameSupportedInstalled = 'Twoja wersja hoi4.exe jest wspierana przez zainstalowan\u0105 wersj\u0119.'
        GameSupportedLatest = 'Twoja wersja hoi4.exe NIE jest wspierana przez zainstalowan\u0105 wersj\u0119, ale wspiera j\u0105 wersja {0}: uruchom z -Update.'
        GameNotSupported  = 'Twoja wersja hoi4.exe ({0}...) nie jest jeszcze wspierana. Launcher uruchomi gr\u0119 bez DLL; sprawd\u017A p\u00F3\u017Aniej (release notes / versions.json).'
    }
}

function ConvertFrom-RaEscapedText {
    param([string]$Text)
    if ($null -eq $Text) { return $null }
    return [regex]::Replace($Text, '\\u([0-9A-Fa-f]{4})', {
            param($m) [string][char][Convert]::ToInt32($m.Groups[1].Value, 16)
        })
}

function Get-RaText {
    param([string]$Key, [object[]]$FormatArgs = @())
    $table = $script:RaMessages[$script:RaLang]
    if (-not $table -or -not $table.ContainsKey($Key)) { $table = $script:RaMessages['en'] }
    $text = ConvertFrom-RaEscapedText $table[$Key]
    if ($FormatArgs.Count -gt 0) { return ($text -f $FormatArgs) }
    return $text
}

function Get-RaDefaultLanguage {
    try {
        if ((Get-UICulture).TwoLetterISOLanguageName -eq 'pl') { return 'pl' }
    } catch { }
    return 'en'
}

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
function Get-RaLogPath { return (Join-Path $script:RaRoot 'bootstrap.log') }

function Write-RaLog {
    param([string]$Message, [ValidateSet('INFO', 'WARN', 'ERROR', 'PLAN')][string]$Level = 'INFO', [switch]$NoConsole)
    $line = '{0} [{1}] {2}' -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss.fffK'), $Level, $Message
    if (-not $NoConsole -and -not $script:RaQuiet) {
        $color = switch ($Level) { 'WARN' { 'Yellow' } 'ERROR' { 'Red' } 'PLAN' { 'Cyan' } default { 'Gray' } }
        Write-Host $Message -ForegroundColor $color
    }
    if ($script:RaDry) { return }
    try {
        if (-not (Test-Path -LiteralPath $script:RaRoot)) { New-Item -ItemType Directory -Path $script:RaRoot -Force | Out-Null }
        $enc = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::AppendAllText((Get-RaLogPath), $line + [Environment]::NewLine, $enc)
    } catch { }
}

# ---------------------------------------------------------------------------
# Prompts
# ---------------------------------------------------------------------------
function Read-RaAnswer {
    param([string]$Prompt)
    $answer = & $script:RaReadHost (ConvertFrom-RaEscapedText $Prompt)
    if ($null -eq $answer) { return '' }
    return ([string]$answer).Trim()
}

function Confirm-Ra {
    param([string]$Prompt, [bool]$Default = $false)
    if ($script:RaAssumeYes) { return $true }
    while ($true) {
        $a = (Read-RaAnswer $Prompt).ToLowerInvariant()
        if ($a -eq '') { return $Default }
        if ($a -in @('y', 'yes', 't', 'tak')) { return $true }
        if ($a -in @('n', 'no', 'nie')) { return $false }
    }
}

# ---------------------------------------------------------------------------
# Text VDF (Valve KeyValues) parser with source offsets
# ---------------------------------------------------------------------------
$script:RaVdfTokenPattern = '"(?:[^"\\]|\\.)*"|\{|\}|//[^\r\n]*|[^\s{}"]+'

function ConvertFrom-RaVdfString {
    param([string]$Raw)
    $sb = New-Object System.Text.StringBuilder
    $i = 0
    while ($i -lt $Raw.Length) {
        $c = $Raw[$i]
        if ($c -eq '\' -and $i + 1 -lt $Raw.Length) {
            $n = $Raw[$i + 1]
            switch -CaseSensitive ($n) {
                '\' { [void]$sb.Append('\') }
                '"' { [void]$sb.Append('"') }
                'n' { [void]$sb.Append("`n") }
                't' { [void]$sb.Append("`t") }
                default { [void]$sb.Append('\').Append($n) }
            }
            $i += 2
        } else {
            [void]$sb.Append($c); $i++
        }
    }
    return $sb.ToString()
}

function ConvertTo-RaVdfString {
    param([string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`t", '\t')
}

function New-RaVdfNode {
    param([string]$Key, [int]$KeyStart, [bool]$IsBlock)
    return [pscustomobject]@{
        Key = $Key; KeyStart = $KeyStart; IsBlock = $IsBlock
        Value = $null; ValueStart = -1; ValueEnd = -1
        OpenIndex = -1; CloseIndex = -1
        Children = (New-Object System.Collections.ArrayList)
    }
}

function ConvertFrom-RaVdf {
    <# Returns a synthetic root node. Throws on any structural problem. #>
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)
    $root = New-RaVdfNode -Key '<root>' -KeyStart -1 -IsBlock $true
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    $pendingKey = $null; $pendingKeyStart = -1
    $pos = 0
    foreach ($m in [regex]::Matches($Text, $script:RaVdfTokenPattern)) {
        $gap = $Text.Substring($pos, $m.Index - $pos)
        if ($gap -notmatch '^\s*$') { throw "VDF parse error near offset ${pos}: unexpected characters" }
        $pos = $m.Index + $m.Length
        $tok = $m.Value
        if ($tok.StartsWith('//')) { continue }
        if ($tok -eq '{') {
            if ($null -eq $pendingKey) { throw "VDF parse error at offset $($m.Index): block without key" }
            $node = New-RaVdfNode -Key $pendingKey -KeyStart $pendingKeyStart -IsBlock $true
            $node.OpenIndex = $m.Index
            [void]$stack.Peek().Children.Add($node)
            $stack.Push($node)
            $pendingKey = $null
            continue
        }
        if ($tok -eq '}') {
            if ($null -ne $pendingKey) { throw "VDF parse error at offset $($m.Index): key without value" }
            if ($stack.Count -le 1) { throw "VDF parse error at offset $($m.Index): unbalanced closing brace" }
            $node = $stack.Pop()
            $node.CloseIndex = $m.Index
            continue
        }
        $isQuoted = $tok.StartsWith('"')
        if (-not $isQuoted -and $null -eq $pendingKey -and $tok -match '^\[.*\]$') { continue } # conditional
        $value = if ($isQuoted) { ConvertFrom-RaVdfString $tok.Substring(1, $tok.Length - 2) } else { $tok }
        if ($null -eq $pendingKey) {
            $pendingKey = $value; $pendingKeyStart = $m.Index
        } else {
            $node = New-RaVdfNode -Key $pendingKey -KeyStart $pendingKeyStart -IsBlock $false
            $node.Value = $value; $node.ValueStart = $m.Index; $node.ValueEnd = $m.Index + $m.Length
            [void]$stack.Peek().Children.Add($node)
            $pendingKey = $null
        }
    }
    $tail = $Text.Substring($pos)
    if ($tail -notmatch '^\s*$') { throw 'VDF parse error: unterminated string or garbage at end of file' }
    if ($null -ne $pendingKey) { throw 'VDF parse error: dangling key at end of file' }
    if ($stack.Count -ne 1) { throw 'VDF parse error: unclosed block at end of file' }
    return $root
}

function Find-RaVdfChildren {
    param($Node, [string]$Key)
    return @($Node.Children | Where-Object { $_.Key -ieq $Key })
}

function Find-RaVdfPath {
    <# Returns all nodes reached by the case-insensitive key path. #>
    param($Root, [string[]]$Path)
    $current = @($Root)
    foreach ($k in $Path) {
        $next = @()
        foreach ($n in $current) { if ($n.IsBlock) { $next += Find-RaVdfChildren $n $k } }
        $current = $next
    }
    return $current
}

# ---------------------------------------------------------------------------
# Byte-exact text file I/O
# ---------------------------------------------------------------------------
function Read-RaTextFile {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $offset = if ($hasBom) { 3 } else { 0 }
    $strict = New-Object System.Text.UTF8Encoding($false, $true)
    try { $text = $strict.GetString($bytes, $offset, $bytes.Length - $offset) }
    catch { throw "File is not valid UTF-8, refusing to edit: $Path" }
    $round = $strict.GetBytes($text)
    if ($round.Length -ne ($bytes.Length - $offset)) { throw "UTF-8 round trip mismatch, refusing to edit: $Path" }
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    return [pscustomobject]@{ Text = $text; HasBom = $hasBom; Newline = $newline; Bytes = $bytes }
}

function ConvertTo-RaFileBytes {
    param([string]$Text, [bool]$HasBom)
    $body = (New-Object System.Text.UTF8Encoding($false)).GetBytes($Text)
    if (-not $HasBom) { return , $body }
    return , ([byte[]](@(0xEF, 0xBB, 0xBF) + $body))
}

function Test-RaBytesEqual {
    param([byte[]]$A, [byte[]]$B)
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}

function Get-RaSha256OfBytes {
    param([byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') } finally { $sha.Dispose() }
}

# ---------------------------------------------------------------------------
# Launch options composition
# ---------------------------------------------------------------------------
$script:RaWrapperPattern = '(?:"[^"]*\\RenameAnythingLauncher\.exe"|(?<!\S)[^\s"]*\\RenameAnythingLauncher\.exe)[ \t]*'

function Test-RaHasWrapper {
    param([AllowNull()][string]$Value)
    if ([string]::IsNullOrEmpty($Value)) { return $false }
    return [regex]::IsMatch($Value, $script:RaWrapperPattern, 'IgnoreCase')
}

function Remove-RaWrapperFragment {
    param([AllowNull()][string]$Value)
    if ($null -eq $Value) { return $null }
    return [regex]::Replace($Value, $script:RaWrapperPattern, '', 'IgnoreCase')
}

function Add-RaWrapper {
    <# Returns the new launch options string with our wrapper directly before %command%. #>
    param([AllowNull()][string]$Current, [Parameter(Mandatory)][string]$LauncherPath)
    if ($LauncherPath.Contains('"')) { throw 'Launcher path must not contain quotes' }
    if ([IO.Path]::GetFileName($LauncherPath) -ne $script:RaLauncherExeName) { throw 'Unexpected launcher file name' }
    $v = if ($null -eq $Current) { '' } else { Remove-RaWrapperFragment $Current }
    $fragment = '"' + $LauncherPath + '"'
    $matches_ = [regex]::Matches($v, '%command%', 'IgnoreCase')
    if ($matches_.Count -gt 1) { throw 'Existing launch options contain %command% more than once; refusing to guess. Edit them manually.' }
    if ($matches_.Count -eq 1) {
        $idx = $matches_[0].Index
        return $v.Substring(0, $idx) + $fragment + ' ' + $v.Substring($idx)
    }
    $rest = $v.Trim()
    if ($rest -eq '') { return "$fragment %command%" }
    # Without %command% Steam appends the options to the game command; keep that meaning.
    return "$fragment %command% $rest"
}

function Remove-RaWrapper {
    <# Removes only our fragment. "%command%" left alone at the start is collapsed, which is
       equivalent in Steam to the options without it. #>
    param([AllowNull()][string]$Current)
    if (-not (Test-RaHasWrapper $Current)) { return $Current }
    $t = (Remove-RaWrapperFragment $Current).Trim()
    if ($t -ieq '%command%') { return '' }
    if ($t -match '^(?i)%command%\s+(.*)$') {
        $rest = $Matches[1]
        if ($rest -notmatch '(?i)%command%') { return $rest.Trim() }
    }
    return $t
}

# ---------------------------------------------------------------------------
# localconfig.vdf editing (pure text functions)
# ---------------------------------------------------------------------------
$script:RaAppsPath = @('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps')

function Get-RaAppLaunchInfo {
    <# Parses localconfig text, returns state of the HOI4 app block. Throws if ambiguous/corrupt. #>
    param([string]$Text)
    $root = ConvertFrom-RaVdf $Text
    $apps = @(Find-RaVdfPath $root $script:RaAppsPath | Where-Object { $_.IsBlock })
    if ($apps.Count -eq 0) { throw 'No UserLocalConfigStore/Software/Valve/Steam/apps block in localconfig.vdf (start the game once from Steam, or set the launch option manually).' }
    if ($apps.Count -gt 1) { throw 'Multiple apps blocks in localconfig.vdf; refusing to edit.' }
    $app = @(Find-RaVdfChildren $apps[0] $script:RaAppId)
    if ($app.Count -gt 1) { throw "Multiple $($script:RaAppId) blocks; refusing to edit." }
    if ($app.Count -eq 1 -and -not $app[0].IsBlock) { throw "$($script:RaAppId) is not a block; refusing to edit." }
    $lo = @()
    if ($app.Count -eq 1) { $lo = @(Find-RaVdfChildren $app[0] 'LaunchOptions') }
    if ($lo.Count -gt 1) { throw 'Multiple LaunchOptions keys; refusing to edit.' }
    if ($lo.Count -eq 1 -and $lo[0].IsBlock) { throw 'LaunchOptions is a block; refusing to edit.' }
    return [pscustomobject]@{
        Apps      = $apps[0]
        App       = if ($app.Count -eq 1) { $app[0] } else { $null }
        Option    = if ($lo.Count -eq 1) { $lo[0] } else { $null }
        Value     = if ($lo.Count -eq 1) { $lo[0].Value } else { $null }
    }
}

function Get-RaLineIndentBefore {
    param([string]$Text, [int]$Index)
    $nl = if ($Index -gt 0) { $Text.LastIndexOf("`n", $Index - 1) } else { -1 }
    $lineStart = $nl + 1
    $prefix = $Text.Substring($lineStart, $Index - $lineStart)
    if ($prefix -notmatch '^[\t ]*$') { throw 'Closing brace is not on its own line; refusing to edit.' }
    return [pscustomobject]@{ LineStart = $lineStart; Indent = $prefix }
}

function Set-RaLaunchOptionsInText {
    param([string]$Text, [string]$NewValue, [string]$Newline = "`n")
    $info = Get-RaAppLaunchInfo $Text
    $escaped = '"' + (ConvertTo-RaVdfString $NewValue) + '"'
    if ($null -ne $info.Option) {
        $o = $info.Option
        $new = $Text.Substring(0, $o.ValueStart) + $escaped + $Text.Substring($o.ValueEnd)
        return [pscustomobject]@{ Text = $new; Action = 'ReplacedValue'; OldValue = $info.Value }
    }
    if ($null -ne $info.App) {
        $pos = Get-RaLineIndentBefore $Text $info.App.CloseIndex
        $line = $pos.Indent + "`t" + '"LaunchOptions"' + "`t`t" + $escaped + $Newline
        $new = $Text.Insert($pos.LineStart, $line)
        return [pscustomobject]@{ Text = $new; Action = 'InsertedKey'; OldValue = $null }
    }
    $pos = Get-RaLineIndentBefore $Text $info.Apps.CloseIndex
    $ind = $pos.Indent + "`t"
    $block = $ind + '"' + $script:RaAppId + '"' + $Newline + $ind + '{' + $Newline +
    $ind + "`t" + '"LaunchOptions"' + "`t`t" + $escaped + $Newline + $ind + '}' + $Newline
    $new = $Text.Insert($pos.LineStart, $block)
    return [pscustomobject]@{ Text = $new; Action = 'InsertedBlock'; OldValue = $null }
}

# ---------------------------------------------------------------------------
# Steam discovery
# ---------------------------------------------------------------------------
function Get-RaSteamRoot {
    param([string]$Override)
    if ($Override) {
        if (-not (Test-Path -LiteralPath $Override -PathType Container)) { throw "Steam folder not found: $Override" }
        return (Resolve-Path -LiteralPath $Override).ProviderPath
    }
    $p = $null
    try { $p = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name 'SteamPath' -ErrorAction Stop).SteamPath } catch { }
    if (-not $p) { throw 'Steam not found (HKCU\Software\Valve\Steam\SteamPath missing).' }
    $p = [IO.Path]::GetFullPath(($p -replace '/', '\'))
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw "Steam folder from registry does not exist: $p" }
    return $p
}

function Get-RaSteamAccounts {
    param([string]$SteamRootPath)
    $names = @{}
    $login = Join-Path $SteamRootPath 'config\loginusers.vdf'
    if (Test-Path -LiteralPath $login) {
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $login).Text
            foreach ($u in @(Find-RaVdfPath $root @('users'))) {
                foreach ($c in $u.Children) {
                    if ($c.IsBlock -and $c.Key -match '^\d+$') {
                        $acc = [string]([uint64]$c.Key - [uint64]76561197960265728)
                        $pn = @(Find-RaVdfChildren $c 'PersonaName')
                        if ($pn.Count -eq 1) { $names[$acc] = $pn[0].Value }
                    }
                }
            }
        } catch { }
    }
    $result = @()
    $ud = Join-Path $SteamRootPath 'userdata'
    if (Test-Path -LiteralPath $ud) {
        foreach ($d in (Get-ChildItem -LiteralPath $ud -Directory | Sort-Object Name)) {
            if ($d.Name -notmatch '^\d+$' -or $d.Name -eq '0') { continue }
            $cfg = Join-Path $d.FullName 'config\localconfig.vdf'
            if (Test-Path -LiteralPath $cfg -PathType Leaf) {
                $result += [pscustomobject]@{ Id = $d.Name; Name = $names[$d.Name]; ConfigPath = $cfg }
            }
        }
    }
    return $result
}

function Test-RaSteamRunning { return (@(& $script:RaGetSteamProcess).Count -gt 0) }

function Wait-RaSteamClosed {
    <# Returns $true when Steam is not running. Never stops the process. #>
    if (-not (Test-RaSteamRunning)) { return $true }
    if ($script:RaAssumeYes) {
        Write-RaLog (Get-RaText 'SteamRunningNonInteractive') -Level WARN
        return $false
    }
    while (Test-RaSteamRunning) {
        $a = Read-RaAnswer (Get-RaText 'SteamRunning')
        if ($a -ieq 'q') { return $false }
    }
    return $true
}

function Select-RaSteamAccounts {
    param([object[]]$Accounts, [string]$Selector)
    if ($Accounts.Count -eq 0) { throw 'No Steam account with config\localconfig.vdf was found.' }
    if ($Selector) {
        if ($Selector -ieq 'all') { return $Accounts }
        $ids = $Selector -split '[,; ]+' | Where-Object { $_ }
        $sel = @($Accounts | Where-Object { $ids -contains $_.Id })
        if ($sel.Count -ne @($ids).Count) { throw "Unknown Steam account id in -SteamAccount '$Selector'. Known: $(($Accounts | ForEach-Object Id) -join ', ')" }
        return $sel
    }
    if ($Accounts.Count -eq 1) { return $Accounts }
    if ($script:RaAssumeYes) { throw "Several Steam accounts found; pass -SteamAccount <id>|all. Known: $(($Accounts | ForEach-Object Id) -join ', ')" }
    for ($i = 0; $i -lt $Accounts.Count; $i++) {
        $n = if ($Accounts[$i].Name) { " ($($Accounts[$i].Name))" } else { '' }
        Write-Host ("  {0} = {1}{2}" -f ($i + 1), $Accounts[$i].Id, $n)
    }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskAccount')
        if ($a -ieq 'a') { return $Accounts }
        $nums = @($a -split '[,; ]+' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        if ($nums.Count -gt 0 -and -not ($nums | Where-Object { $_ -lt 1 -or $_ -gt $Accounts.Count })) {
            return @($nums | Sort-Object -Unique | ForEach-Object { $Accounts[$_ - 1] })
        }
    }
}

function Get-RaGameDir {
    param([string]$SteamRootPath)
    $libs = @($SteamRootPath)
    $lf = Join-Path $SteamRootPath 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $lf) {
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $lf).Text
            foreach ($top in $root.Children) {
                foreach ($entry in $top.Children) {
                    if ($entry.IsBlock) {
                        $p = @(Find-RaVdfChildren $entry 'path')
                        if ($p.Count -eq 1) { $libs += $p[0].Value }
                    } elseif ($entry.Key -match '^\d+$') { $libs += $entry.Value } # legacy format
                }
            }
        } catch { Write-RaLog "libraryfolders.vdf unreadable: $($_.Exception.Message)" -Level WARN -NoConsole }
    }
    foreach ($lib in ($libs | Select-Object -Unique)) {
        $acf = Join-Path $lib "steamapps\appmanifest_$($script:RaAppId).acf"
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $acf).Text
            $dir = @(Find-RaVdfPath $root @('AppState', 'installdir'))
            if ($dir.Count -ne 1) { continue }
            $game = Join-Path $lib ("steamapps\common\" + $dir[0].Value)
            if (Test-Path -LiteralPath (Join-Path $game 'hoi4.exe') -PathType Leaf) { return $game }
        } catch { }
    }
    return $null
}

# ---------------------------------------------------------------------------
# State file
# ---------------------------------------------------------------------------
function Get-RaStatePath { return (Join-Path $script:RaRoot 'state.json') }

function Read-RaState {
    $p = Get-RaStatePath
    $state = [pscustomobject]@{ InstalledVersion = $null; PreviousVersion = $null; Shortcuts = @(); SteamEntries = @() }
    if (Test-Path -LiteralPath $p) {
        try {
            $j = Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($j.PSObject.Properties['InstalledVersion']) { $state.InstalledVersion = $j.InstalledVersion }
            if ($j.PSObject.Properties['PreviousVersion']) { $state.PreviousVersion = $j.PreviousVersion }
            if ($j.PSObject.Properties['Shortcuts']) { $state.Shortcuts = @($j.Shortcuts | Where-Object { $_ }) }
            if ($j.PSObject.Properties['SteamEntries']) { $state.SteamEntries = @($j.SteamEntries | Where-Object { $_ }) }
        } catch { Write-RaLog "state.json unreadable, ignoring: $($_.Exception.Message)" -Level WARN }
    }
    return $state
}

function Save-RaState {
    param($State)
    if ($script:RaDry) { return }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { New-Item -ItemType Directory -Path $script:RaRoot -Force | Out-Null }
    $json = [pscustomobject]@{
        InstalledVersion = $State.InstalledVersion
        PreviousVersion  = $State.PreviousVersion
        Shortcuts        = @($State.Shortcuts)
        SteamEntries     = @($State.SteamEntries)
    } | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Get-RaStatePath), $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-RaCurrentLink { return (Join-Path $script:RaRoot 'current') }

function Get-RaLauncherPath {
    <# Stable path used by Steam and shortcuts: updates only re-point the 'current' junction. #>
    return (Join-Path (Get-RaCurrentLink) $script:RaLauncherExeName)
}

# ---------------------------------------------------------------------------
# Steam launch option update (file level, with backup/verify/restore)
# ---------------------------------------------------------------------------
function Update-RaLocalConfig {
    <# $Compute: scriptblock(oldValueOrNull) -> newValue or $null for "no change". #>
    param([string]$ConfigPath, [scriptblock]$Compute)
    $file = Read-RaTextFile $ConfigPath
    $info = Get-RaAppLaunchInfo $file.Text
    $newValue = & $Compute $info.Value
    if ($null -eq $newValue -or ($null -ne $info.Value -and $newValue -ceq $info.Value)) {
        return [pscustomobject]@{ Changed = $false; OldValue = $info.Value; NewValue = $info.Value; Backup = $null; Action = 'Unchanged' }
    }
    $edit = Set-RaLaunchOptionsInText $file.Text $newValue $file.Newline
    $check = Get-RaAppLaunchInfo $edit.Text
    if ($check.Value -cne $newValue) { throw 'Internal check failed: edited text does not contain the expected value.' }
    $expected = ConvertTo-RaFileBytes $edit.Text $file.HasBom
    if ($script:RaDry) {
        Write-RaLog ("[dry-run] {0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action) -Level PLAN
        return [pscustomobject]@{ Changed = $true; OldValue = $info.Value; NewValue = $newValue; Backup = $null; Action = $edit.Action }
    }
    if (Test-RaSteamRunning) { throw 'Steam started while editing; aborting before write.' }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = "$ConfigPath.renameanything-backup-$stamp"
    $n = 1
    while (Test-Path -LiteralPath $backup) { $backup = "$ConfigPath.renameanything-backup-$stamp-$n"; $n++ }
    [IO.File]::WriteAllBytes($backup, $file.Bytes)
    if (-not (Test-RaBytesEqual ([IO.File]::ReadAllBytes($backup)) $file.Bytes)) { throw "Backup verification failed: $backup" }
    Write-RaLog "Backup: $backup (sha256 $(Get-RaSha256OfBytes $file.Bytes))"
    $tmp = "$ConfigPath.renameanything-tmp"
    try {
        [IO.File]::WriteAllBytes($tmp, $expected)
        [IO.File]::Copy($tmp, $ConfigPath, $true)
        Remove-Item -LiteralPath $tmp -Force
        if ($script:RaAfterWriteHook) { & $script:RaAfterWriteHook $ConfigPath }
        $after = [IO.File]::ReadAllBytes($ConfigPath)
        if (-not (Test-RaBytesEqual $after $expected)) { throw 'Post-write verification failed: file content differs from expected.' }
        $verify = Get-RaAppLaunchInfo (Read-RaTextFile $ConfigPath).Text
        if ($verify.Value -cne $newValue) { throw 'Post-write verification failed: LaunchOptions value differs.' }
    } catch {
        $err = $_.Exception.Message
        try {
            if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
            [IO.File]::Copy($backup, $ConfigPath, $true)
            $restored = Test-RaBytesEqual ([IO.File]::ReadAllBytes($ConfigPath)) $file.Bytes
        } catch { $restored = $false }
        if ($restored) { throw "$err Original file restored from backup." }
        throw "$err RESTORE FAILED - copy the backup back manually: $backup"
    }
    Write-RaLog ("{0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action)
    return [pscustomobject]@{ Changed = $true; OldValue = $info.Value; NewValue = $newValue; Backup = $backup; Action = $edit.Action }
}

function Set-RaSteamLaunchOption {
    param($State, [string]$SteamRootPath, [string]$AccountSelector, [string]$LauncherPath)
    $raCompLauncher = $LauncherPath
    $accounts = @(Select-RaSteamAccounts @(Get-RaSteamAccounts $SteamRootPath) $AccountSelector)
    if (-not (Wait-RaSteamClosed)) { throw 'Steam is running; Steam launch option was not changed.' }
    foreach ($acc in $accounts) {
        $r = Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute { param($old) Add-RaWrapper $old $raCompLauncher }
        $existing = @($State.SteamEntries | Where-Object { $_.ConfigPath -eq $acc.ConfigPath })
        $original = if ($existing.Count -gt 0) { $existing[0].OriginalValue } else { $r.OldValue }
        if ($existing.Count -eq 0 -and (Test-RaHasWrapper $r.OldValue)) { $original = Remove-RaWrapper $r.OldValue }
        $State.SteamEntries = @($State.SteamEntries | Where-Object { $_.ConfigPath -ne $acc.ConfigPath }) + @(
            [pscustomobject]@{ AccountId = $acc.Id; ConfigPath = $acc.ConfigPath; OriginalValue = $original; WrittenValue = $r.NewValue; LauncherPath = $LauncherPath })
        $other = (Remove-RaWrapperFragment $r.NewValue) -replace '(?i)\s*%command%.*$', ''
        if ($other.Trim()) { Write-RaLog "Note: another wrapper precedes ours in account $($acc.Id): '$($other.Trim())'. Compatibility is unknown." -Level WARN }
    }
}

function Remove-RaSteamLaunchOption {
    param($State, [string]$SteamRootPath)
    $accounts = @(Get-RaSteamAccounts $SteamRootPath)
    $targets = @()
    foreach ($acc in $accounts) {
        try {
            $info = Get-RaAppLaunchInfo (Read-RaTextFile $acc.ConfigPath).Text
            if (Test-RaHasWrapper $info.Value) { $targets += $acc }
        } catch { Write-RaLog "Account $($acc.Id): cannot read localconfig.vdf ($($_.Exception.Message)); skipped." -Level WARN }
    }
    if ($targets.Count -eq 0) {
        Write-RaLog 'No Steam launch option with Rename Anything found.'
        $State.SteamEntries = @()
        return $true
    }
    if (-not (Wait-RaSteamClosed)) { return $false }
    foreach ($acc in $targets) {
        $raCompEntry = @($State.SteamEntries | Where-Object { $_.ConfigPath -eq $acc.ConfigPath }) | Select-Object -First 1
        # Restore the exact original only if the value is still what we wrote; otherwise remove just our fragment.
        $compute = {
            param($old)
            if ($null -ne $raCompEntry -and $old -ceq $raCompEntry.WrittenValue) {
                if ($null -eq $raCompEntry.OriginalValue) { return '' }
                return [string]$raCompEntry.OriginalValue
            }
            return (Remove-RaWrapper $old)
        }
        [void](Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute $compute)
        $State.SteamEntries = @($State.SteamEntries | Where-Object { $_.ConfigPath -ne $acc.ConfigPath })
    }
    return $true
}

# ---------------------------------------------------------------------------
# Package install, update channel, version switching
# ---------------------------------------------------------------------------
$script:RaVersionPattern = '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$'

function Test-RaManifest {
    param($Manifest)
    if ($Manifest.Version -notmatch $script:RaVersionPattern) { throw 'Release manifest: invalid version.' }
    if ($Manifest.ZipSha256 -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Pinned release manifest is not configured (ZipSha256 placeholder). No release is published for this script yet; refusing to install.' }
    if ($Manifest.ZipName -notmatch '^[0-9A-Za-z._-]+\.zip$') { throw 'Release manifest: invalid ZIP name.' }
    if ($Manifest.Files.Count -lt 1) { throw 'Release manifest: no files.' }
    foreach ($k in $Manifest.Files.Keys) {
        if ($Manifest.Files[$k] -notmatch '^[0-9A-Fa-f]{64}$') { throw "Release manifest: bad hash for $k" }
        if ($k -notmatch '^[0-9A-Za-z._-]+(/[0-9A-Za-z._-]+)*$' -or $k -match '(^|/)\.\.?(/|$)') { throw "Release manifest: unsafe path $k" }
    }
    if (-not $Manifest.Files.Contains($script:RaLauncherExeName)) { throw 'Release manifest: launcher missing.' }
    if (-not $Manifest.Contains('SupportedGameSha256')) { $Manifest['SupportedGameSha256'] = @() }
    foreach ($h in @($Manifest.SupportedGameSha256 | Where-Object { $_ })) { if ($h -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Release manifest: bad game hash.' } }
}

function Compare-RaVersion {
    <# -1/0/1 on the numeric x.y.z part. #>
    param([string]$A, [string]$B)
    $va = [version](($A -split '[-+]')[0]); $vb = [version](($B -split '[-+]')[0])
    return $va.CompareTo($vb)
}

function Test-RaInstalledVersion {
    param($Manifest, [string]$Dir)
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return $false }
    $files = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force)
    if ($files.Count -ne $Manifest.Files.Count) { return $false }
    foreach ($k in $Manifest.Files.Keys) {
        $p = Join-Path $Dir ($k -replace '/', '\')
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return $false }
        if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne $Manifest.Files[$k].ToUpperInvariant()) { return $false }
    }
    return $true
}

function Expand-RaVerifiedZip {
    param($Manifest, [string]$ZipPath, [string]$Destination)
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $seen = @{}
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($e in $zip.Entries) {
            $name = $e.FullName -replace '\\', '/'
            if ($name.EndsWith('/') -and $e.Length -eq 0) { continue }
            if (-not $Manifest.Files.Contains($name)) { throw "Package contains an unexpected file: $name" }
            if ($seen.ContainsKey($name)) { throw "Package contains a duplicate entry: $name" }
            $seen[$name] = $true
            $dest = Join-Path $Destination ($name -replace '/', '\')
            $parent = Split-Path -Parent $dest
            if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $false)
            $h = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
            if ($h -ne $Manifest.Files[$name].ToUpperInvariant()) { throw "SHA256 mismatch for $name (got $h)" }
        }
    } finally { $zip.Dispose() }
    foreach ($k in $Manifest.Files.Keys) { if (-not $seen.ContainsKey($k)) { throw "Package is missing $k" } }
}

function ConvertTo-RaManifestJson {
    param($Manifest)
    return ([pscustomobject]@{
            schema = 1; version = $Manifest.Version; zipName = $Manifest.ZipName; zipSha256 = $Manifest.ZipSha256
            files = [pscustomobject]$Manifest.Files; supportedGameSha256 = @($Manifest.SupportedGameSha256)
        } | ConvertTo-Json -Depth 5)
}

function ConvertFrom-RaManifestJson {
    param([string]$Json)
    $j = $Json | ConvertFrom-Json
    if ($j.schema -ne 1) { throw 'Release manifest: unsupported schema.' }
    $files = [ordered]@{}
    foreach ($p in $j.files.PSObject.Properties) { $files[$p.Name] = [string]$p.Value }
    $m = [ordered]@{
        Version = [string]$j.version; ZipName = [string]$j.zipName; ZipSha256 = [string]$j.zipSha256
        Files = $files; SupportedGameSha256 = @($j.supportedGameSha256 | ForEach-Object { [string]$_ })
    }
    Test-RaManifest $m
    return $m
}

function Get-RaManifestStorePath { param([string]$Version) return (Join-Path (Join-Path $script:RaRoot 'manifests') "$Version.json") }

function Read-RaStoredManifest {
    param([string]$Version)
    if ($Version -eq $script:RaPinnedRelease.Version) { return $script:RaPinnedRelease }
    $p = Get-RaManifestStorePath $Version
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    try { return (ConvertFrom-RaManifestJson ([IO.File]::ReadAllText($p))) } catch { return $null }
}

function Test-RaSignature {
    <# RSA PKCS#1 v1.5 / SHA-256 over the exact manifest bytes, public key pinned in this script. #>
    param([byte[]]$Data, [string]$SignatureBase64, [string]$PublicKeyXml)
    if ($PublicKeyXml -notmatch '<RSAKeyValue>') { throw 'The update signing key is not configured in this script yet; refusing to trust a downloaded manifest.' }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    try {
        $rsa.FromXmlString($PublicKeyXml)
        if ($rsa.KeySize -lt 2048) { throw 'Pinned update key is too small.' }
        try {
            $sig = [Convert]::FromBase64String($SignatureBase64.Trim())
            return [bool]$rsa.VerifyData($Data, 'SHA256', $sig)
        } catch { return $false }
    } finally { $rsa.Dispose() }
}

function Invoke-RaHttpGetBytes {
    param([string]$Url)
    if ($Url -notmatch '^https://') { throw "Refusing non-HTTPS URL: $Url" }
    return , ([byte[]](& $script:RaHttpGet $Url))
}

function Get-RaRemoteRelease {
    <# Exactly one API request plus the two small manifest assets; only on explicit user request. #>
    param([string]$LocalManifestPath)
    if ($LocalManifestPath) {
        $bytes = [IO.File]::ReadAllBytes($LocalManifestPath)
        $sig = [IO.File]::ReadAllText("$LocalManifestPath.sig")
        if (-not (Test-RaSignature $bytes $sig $script:RaUpdatePublicKeyXml)) { throw 'Release manifest signature is INVALID; nothing was changed.' }
        $m = ConvertFrom-RaManifestJson ((New-Object System.Text.UTF8Encoding($false, $true)).GetString($bytes))
        return [pscustomobject]@{ Manifest = $m; ZipUrl = $null }
    }
    if ($script:RaGitHubRepo -match 'PLACEHOLDER') { throw 'Update channel is not configured yet (repository placeholder). Use -ManifestPath and -PackagePath for an offline update.' }
    $api = "https://api.github.com/repos/$($script:RaGitHubRepo)/releases/latest"
    Write-RaLog "Checking $api (no identifiers are sent)"
    $rel = (New-Object System.Text.UTF8Encoding($false)).GetString((Invoke-RaHttpGetBytes $api)) | ConvertFrom-Json
    $assets = @{}
    foreach ($a in @($rel.assets)) { $assets[[string]$a.name] = [string]$a.browser_download_url }
    $prefix = "https://github.com/$($script:RaGitHubRepo)/releases/download/"
    foreach ($n in 'release-manifest.json', 'release-manifest.json.sig') {
        if (-not $assets.ContainsKey($n)) { throw "Latest release has no $n asset." }
        if (-not $assets[$n].StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected asset URL: $($assets[$n])" }
    }
    $mBytes = Invoke-RaHttpGetBytes $assets['release-manifest.json']
    $sigText = (New-Object System.Text.UTF8Encoding($false)).GetString((Invoke-RaHttpGetBytes $assets['release-manifest.json.sig']))
    if (-not (Test-RaSignature $mBytes $sigText $script:RaUpdatePublicKeyXml)) { throw 'Release manifest signature is INVALID; nothing was changed.' }
    $m = ConvertFrom-RaManifestJson ((New-Object System.Text.UTF8Encoding($false, $true)).GetString($mBytes))
    if (([string]$rel.tag_name).TrimStart('v') -ne $m.Version) { throw "Release tag $($rel.tag_name) does not match manifest version $($m.Version)." }
    $zipUrl = $assets[$m.ZipName]
    if (-not $zipUrl -or -not $zipUrl.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Release has no valid $($m.ZipName) asset." }
    return [pscustomobject]@{ Manifest = $m; ZipUrl = $zipUrl }
}

function Install-RaVersion {
    <# Downloads (or takes a local ZIP), verifies ZIP and every file, installs <root>\<version>. #>
    param($Manifest, [string]$LocalZip, [string]$ZipUrl)
    Test-RaManifest $Manifest
    $dir = Join-Path $script:RaRoot $Manifest.Version
    if (Test-RaInstalledVersion $Manifest $dir) {
        Write-RaLog "Version $($Manifest.Version) already present and verified: $dir"
        return $dir
    }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would verify $($Manifest.ZipName) (sha256 $($Manifest.ZipSha256)) and install to $dir" -Level PLAN
        return $dir
    }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { New-Item -ItemType Directory -Path $script:RaRoot -Force | Out-Null }
    $downloaded = $null
    if ($LocalZip) {
        $zipPath = (Resolve-Path -LiteralPath $LocalZip).ProviderPath
    } else {
        if (-not $ZipUrl) {
            if ($script:RaGitHubRepo -match 'PLACEHOLDER') { throw 'Release URL is still a placeholder; use -PackagePath with a downloaded release ZIP.' }
            $ZipUrl = "https://github.com/$($script:RaGitHubRepo)/releases/download/v$($Manifest.Version)/$($Manifest.ZipName)"
        }
        Write-RaLog "Downloading $ZipUrl"
        $downloaded = Join-Path $script:RaRoot ('download-' + [guid]::NewGuid().ToString('N') + '.zip')
        [IO.File]::WriteAllBytes($downloaded, (Invoke-RaHttpGetBytes $ZipUrl))
        $zipPath = $downloaded
    }
    $staging = Join-Path $script:RaRoot ('.staging-' + [guid]::NewGuid().ToString('N'))
    try {
        $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
        if ($hash -ne $Manifest.ZipSha256.ToUpperInvariant()) { throw "Package SHA256 mismatch: expected $($Manifest.ZipSha256), got $hash. Nothing was installed." }
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-RaVerifiedZip $Manifest $zipPath $staging
        if (-not (Test-RaInstalledVersion $Manifest $staging)) { throw 'Staged files failed verification.' }
        if (Test-Path -LiteralPath $dir) {
            $broken = "$dir.broken-$(Get-Date -Format 'yyyyMMddHHmmss')"
            Write-RaLog "Existing $dir failed verification; moved to $broken" -Level WARN
            Move-Item -LiteralPath $dir -Destination $broken
        }
        Move-Item -LiteralPath $staging -Destination $dir
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
        if ($downloaded -and (Test-Path -LiteralPath $downloaded)) { Remove-Item -LiteralPath $downloaded -Force }
    }
    $store = Get-RaManifestStorePath $Manifest.Version
    New-Item -ItemType Directory -Path (Split-Path -Parent $store) -Force | Out-Null
    [IO.File]::WriteAllText($store, (ConvertTo-RaManifestJson $Manifest), (New-Object System.Text.UTF8Encoding($false)))
    Write-RaLog "Installed version $($Manifest.Version) to $dir"
    return $dir
}

function Get-RaJunctionTarget {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item -or -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $null }
    $t = @($item.Target) | Select-Object -First 1
    if ($t) { return [IO.Path]::GetFullPath(($t -replace '^\\\\\?\\', '')) }
    return $null
}

function Remove-RaJunction {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return }
    if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "$Path is not a junction; refusing to delete it." }
    [IO.Directory]::Delete($Path, $false)   # removes the link only, never the target's content
}

function Switch-RaCurrent {
    <# Points <root>\current at <root>\<version>. Restores the previous link on failure. #>
    param([string]$Version)
    $link = Get-RaCurrentLink
    $target = Join-Path $script:RaRoot $Version
    if ($script:RaDry) { Write-RaLog "[dry-run] would point $link -> $target" -Level PLAN; return }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Version folder missing: $target" }
    $old = Get-RaJunctionTarget $link
    $tmp = "$link.new-" + [guid]::NewGuid().ToString('N')
    New-Item -ItemType Junction -Path $tmp -Target $target | Out-Null
    try {
        if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
        [IO.Directory]::Move($tmp, $link)
    } catch {
        $err = $_.Exception.Message
        if (Test-Path -LiteralPath $tmp) { [IO.Directory]::Delete($tmp, $false) }
        if ($old -and -not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $old | Out-Null }
        throw "Could not switch the current version: $err"
    }
    if ((Get-RaJunctionTarget $link) -ne [IO.Path]::GetFullPath($target)) { throw 'current junction verification failed.' }
    Write-RaLog "Active version: $Version ($link)"
}

function Set-RaActiveVersion {
    <# Records previous/current and switches the junction (no-op if already active). #>
    param($State, [string]$Version)
    $link = Get-RaCurrentLink
    $already = ($State.InstalledVersion -eq $Version) -and ((Get-RaJunctionTarget $link) -eq [IO.Path]::GetFullPath((Join-Path $script:RaRoot $Version)))
    if ($already) { return }
    Switch-RaCurrent $Version
    if ($State.InstalledVersion -and $State.InstalledVersion -ne $Version) { $State.PreviousVersion = $State.InstalledVersion }
    $State.InstalledVersion = $Version
}

function Install-RaPackage {
    <# Install of the release pinned in this script. #>
    param($State, [string]$LocalZip)
    $m = $script:RaPinnedRelease
    Test-RaManifest $m
    if ($State.InstalledVersion -and (Compare-RaVersion $State.InstalledVersion $m.Version) -gt 0) {
        $cur = Read-RaStoredManifest $State.InstalledVersion
        if ($cur -and (Test-RaInstalledVersion $cur (Join-Path $script:RaRoot $State.InstalledVersion))) {
            Write-RaLog "A newer version ($($State.InstalledVersion)) is already installed; keeping it."
            Set-RaActiveVersion $State $State.InstalledVersion
            return
        }
    }
    [void](Install-RaVersion $m $LocalZip $null)
    Set-RaActiveVersion $State $m.Version
}

function Get-RaGameSha256 {
    param([string]$GameDir)
    if (-not $GameDir) { return $null }
    $exe = Join-Path $GameDir 'hoi4.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return $null }
    return (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash   # read-only
}

function Invoke-RaCheckUpdate {
    param($State, [string]$GameDir, [string]$LocalManifestPath)
    $remote = Get-RaRemoteRelease $LocalManifestPath
    $latest = $remote.Manifest
    $installed = $State.InstalledVersion
    $newer = (-not $installed) -or ((Compare-RaVersion $latest.Version $installed) -gt 0)
    $gameSha = Get-RaGameSha256 $GameDir
    $curManifest = if ($installed) { Read-RaStoredManifest $installed } else { $null }
    $installedSupports = $curManifest -and $gameSha -and (@($curManifest.SupportedGameSha256) -contains $gameSha)
    $latestSupports = $gameSha -and (@($latest.SupportedGameSha256) -contains $gameSha)
    $iv = if ($installed) { $installed } else { '-' }
    Write-RaLog (Get-RaText 'UpdateStatus' @($iv, $latest.Version))
    if ($newer) { Write-RaLog (Get-RaText 'UpdateAvailable' @($latest.Version)) -Level WARN } else { Write-RaLog (Get-RaText 'UpToDate') }
    if ($gameSha) {
        if ($installedSupports) { Write-RaLog (Get-RaText 'GameSupportedInstalled') }
        elseif ($latestSupports) { Write-RaLog (Get-RaText 'GameSupportedLatest' @($latest.Version)) -Level WARN }
        else { Write-RaLog (Get-RaText 'GameNotSupported' @($gameSha.Substring(0, 16))) -Level WARN }
    }
    return [pscustomobject]@{ Remote = $remote; Newer = $newer; GameSha = $gameSha; InstalledSupportsGame = [bool]$installedSupports; LatestSupportsGame = [bool]$latestSupports }
}

function Invoke-RaUpdate {
    param($State, [string]$GameDir, [string]$LocalManifestPath, [string]$LocalZip)
    $check = Invoke-RaCheckUpdate $State $GameDir $LocalManifestPath
    $m = $check.Remote.Manifest
    if (-not $check.Newer) {
        $dir = Join-Path $script:RaRoot $m.Version
        if (Test-RaInstalledVersion $m $dir) { return $false }
        Write-RaLog "Installed files of $($m.Version) failed verification; reinstalling." -Level WARN
    }
    [void](Install-RaVersion $m $LocalZip $check.Remote.ZipUrl)
    Set-RaActiveVersion $State $m.Version
    return $true
}

function Invoke-RaRollback {
    param($State)
    $prev = $State.PreviousVersion
    if (-not $prev) { throw 'No previous version to roll back to.' }
    $m = Read-RaStoredManifest $prev
    if (-not $m -or -not (Test-RaInstalledVersion $m (Join-Path $script:RaRoot $prev))) { throw "Previous version $prev is missing or failed verification; cannot roll back." }
    $cur = $State.InstalledVersion
    Switch-RaCurrent $prev
    $State.InstalledVersion = $prev
    $State.PreviousVersion = $cur
    Write-RaLog "Rolled back to $prev (kept $cur as previous)."
}

# ---------------------------------------------------------------------------
# Shortcuts
# ---------------------------------------------------------------------------
function Get-RaShortcutInfo {
    param([string]$Path)
    $sh = New-Object -ComObject WScript.Shell
    try {
        $s = $sh.CreateShortcut($Path)
        return [pscustomobject]@{ TargetPath = $s.TargetPath; Arguments = $s.Arguments; Description = $s.Description; IconLocation = $s.IconLocation }
    } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($sh) }
}

function Test-RaShortcutIsOurs {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try { $t = (Get-RaShortcutInfo $Path).TargetPath } catch { return $false }
    if (-not $t) { return $false }
    $rootFull = [IO.Path]::GetFullPath($script:RaRoot).TrimEnd('\') + '\'
    return ([IO.Path]::GetFileName($t) -ieq $script:RaLauncherExeName -and
        [IO.Path]::GetFullPath($t).StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase))
}

function Select-RaFolder {
    param([string]$Default)
    if ($script:RaFolderPicker) { return (& $script:RaFolderPicker $Default) }
    $code = {
        param($start, $title)
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = $title
        $dlg.SelectedPath = $start
        $dlg.ShowNewFolderButton = $true
        $owner = New-Object System.Windows.Forms.Form
        $owner.TopMost = $true
        try {
            if ($dlg.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) { return $dlg.SelectedPath }
            return $null
        } finally { $owner.Dispose(); $dlg.Dispose() }
    }
    $title = 'Rename Anything: shortcut folder'
    if ([Threading.Thread]::CurrentThread.ApartmentState -eq 'STA') { return (& $code $Default $title) }
    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = 'STA'; $rs.Open()
    try {
        $ps = [powershell]::Create(); $ps.Runspace = $rs
        [void]$ps.AddScript($code.ToString()).AddArgument($Default).AddArgument($title)
        $r = @($ps.Invoke())
        if ($r.Count -gt 0) { return $r[-1] }
        return $null
    } finally { $rs.Dispose() }
}

function Resolve-RaShortcutFolder {
    param([string]$Requested)
    $desktop = & $script:RaDesktopPath
    if ($Requested) { return $Requested }
    if ($script:RaAssumeYes) { return $desktop }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskShortcutFolder' @($desktop))
        if ($a -eq '' -or $a -eq '1') { return $desktop }
        if ($a -eq '2') {
            $f = Select-RaFolder $desktop
            if ($f) { return $f }
            continue
        }
        $a = $a.Trim('"')
        if ([IO.Path]::IsPathRooted($a)) { return $a }
    }
}

function New-RaShortcut {
    param($State, [string]$Folder, [string]$LauncherPath, [string]$GameDir)
    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) { throw "Shortcut folder does not exist: $Folder" }
    $lnk = Join-Path ((Resolve-Path -LiteralPath $Folder).ProviderPath) $script:RaShortcutFileName
    if ((Test-Path -LiteralPath $lnk) -and -not (Test-RaShortcutIsOurs $lnk)) { throw "A different file already exists and was left untouched: $lnk" }
    if (-not $GameDir) { throw 'HOI4 installation not found through Steam libraries; cannot build the shortcut.' }
    $dowser = Join-Path $GameDir 'dowser.exe'
    if (-not (Test-Path -LiteralPath $dowser -PathType Leaf)) { throw "Paradox Launcher not found: $dowser" }
    $icon = Join-Path $GameDir 'hoi4.exe'
    $iconLocation = if (Test-Path -LiteralPath $icon) { "$icon,0" } else { "$LauncherPath,0" }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would create $lnk -> `"$LauncherPath`" `"$dowser`"" -Level PLAN
        return $lnk
    }
    $sh = New-Object -ComObject WScript.Shell
    try {
        $s = $sh.CreateShortcut($lnk)
        $s.TargetPath = $LauncherPath
        $s.Arguments = '"' + $dowser + '"'
        $s.WorkingDirectory = Split-Path -Parent $LauncherPath
        $s.IconLocation = $iconLocation
        $s.Description = Get-RaText 'ShortcutComment'
        $s.Save()
    } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($sh) }
    $State.Shortcuts = @(@($State.Shortcuts | Where-Object { $_ -ne $lnk }) + $lnk)
    Write-RaLog "Shortcut created: $lnk"
    return $lnk
}

function Remove-RaShortcuts {
    param($State)
    $candidates = @($State.Shortcuts) + @(Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName)
    $kept = @()
    foreach ($p in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $p)) { continue }
        if (Test-RaShortcutIsOurs $p) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would delete shortcut $p" -Level PLAN; continue }
            Remove-Item -LiteralPath $p -Force
            Write-RaLog "Shortcut deleted: $p"
        } else {
            Write-RaLog "Shortcut no longer points to our launcher, left untouched: $p" -Level WARN
        }
    }
    $State.Shortcuts = $kept
}

function Remove-RaUnusedVersions {
    <# Keeps only the active and the previous (rollback) version. Never fails the run. #>
    param($State)
    $keep = @($State.InstalledVersion, $State.PreviousVersion) | Where-Object { $_ }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    foreach ($d in Get-ChildItem -LiteralPath $script:RaRoot -Directory -Force) {
        if ($d.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if ($d.Name -match $script:RaVersionPattern -and $keep -notcontains $d.Name) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would remove old version $($d.FullName)" -Level PLAN; continue }
            try {
                Remove-Item -LiteralPath $d.FullName -Recurse -Force
                $mf = Get-RaManifestStorePath $d.Name
                if (Test-Path -LiteralPath $mf) { Remove-Item -LiteralPath $mf -Force }
                Write-RaLog "Removed old version $($d.FullName)"
            } catch { Write-RaLog "Could not remove $($d.FullName) (in use?): $($_.Exception.Message)" -Level WARN }
        }
    }
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------
function Invoke-RaUninstall {
    param($State, [string]$SteamRootPath)
    if ($SteamRootPath) {
        if (-not (Remove-RaSteamLaunchOption $State $SteamRootPath)) {
            throw 'Steam is running: the launch option still points to the launcher, so no files were removed. Close Steam and run -Uninstall again.'
        }
    } else {
        Write-RaLog 'Steam not found; skipping launch options.' -Level WARN
    }
    Remove-RaShortcuts $State
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    $log = Get-RaLogPath
    # Remove the junction first so nothing can be deleted through it.
    $link = Get-RaCurrentLink
    if (Test-Path -LiteralPath $link) {
        if ($script:RaDry) { Write-RaLog "[dry-run] would delete junction $link" -Level PLAN } else { Remove-RaJunction $link }
    }
    foreach ($item in Get-ChildItem -LiteralPath $script:RaRoot -Force) {
        if ($item.FullName -ieq $log -or $item.FullName -ieq $link) { continue }
        if ($script:RaDry) { Write-RaLog "[dry-run] would delete $($item.FullName)" -Level PLAN; continue }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { [IO.Directory]::Delete($item.FullName, $false); continue }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
    Write-RaLog "Removed files in $($script:RaRoot) (kept bootstrap.log)"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
function Invoke-RaMain {
    param([hashtable]$P)
    $known = 'Install', 'Shortcut', 'ShortcutPath', 'SteamLaunchOption', 'SteamAccount', 'RemoveSteamLaunchOption',
    'Uninstall', 'DryRun', 'Yes', 'PackagePath', 'SteamRoot', 'Language', 'CheckUpdate', 'Update', 'Rollback', 'ManifestPath'
    $P = $P.Clone()
    foreach ($k in $known) { if (-not $P.ContainsKey($k)) { $P[$k] = $null } }
    Set-StrictMode -Version 2.0
    $ErrorActionPreference = 'Stop'
    $script:RaLang = if ($P.Language) { $P.Language } else { Get-RaDefaultLanguage }
    if (-not $script:RaRoot) { $script:RaRoot = Join-Path $env:LOCALAPPDATA 'RenameAnything' }
    $script:RaDry = [bool]$P.DryRun
    $script:RaAssumeYes = [bool]$P.Yes

    Write-Host (Get-RaText 'Title') -ForegroundColor White
    Write-Host (Get-RaText 'Intro' @((Get-RaLogPath)))
    if ($script:RaDry) { Write-Host (Get-RaText 'DryRunNote') -ForegroundColor Cyan }
    Write-RaLog ("Start: PS {0}, args: {1}" -f $PSVersionTable.PSVersion, (($P.GetEnumerator() | Where-Object { $_.Value } | ForEach-Object { "-$($_.Key)" }) -join ' ')) -NoConsole

    try {
        $state = Read-RaState
        $steamRootPath = $null
        try { $steamRootPath = Get-RaSteamRoot $P.SteamRoot } catch { if ($P.SteamRoot) { throw } ; Write-RaLog $_.Exception.Message -Level WARN -NoConsole }
        $gameDirLazy = { if ($steamRootPath) { Get-RaGameDir $steamRootPath } else { $null } }

        if ($P.Uninstall) {
            Invoke-RaUninstall $state $steamRootPath
            if (-not $script:RaDry -and (Test-Path -LiteralPath (Get-RaStatePath))) { Remove-Item -LiteralPath (Get-RaStatePath) -Force }
            Write-Host (Get-RaText 'UninstallDone' @((Get-RaLogPath))) -ForegroundColor Green
            return 0
        }
        if ($P.RemoveSteamLaunchOption) {
            if (-not $steamRootPath) { throw 'Steam not found.' }
            if (-not (Remove-RaSteamLaunchOption $state $steamRootPath)) { throw 'Steam is running; launch option not changed.' }
            Save-RaState $state
            Write-Host (Get-RaText 'Done') -ForegroundColor Green
            return 0
        }
        if ($P.CheckUpdate) {
            [void](Invoke-RaCheckUpdate $state (& $gameDirLazy) $P.ManifestPath)
            return 0
        }
        if ($P.Rollback) {
            Invoke-RaRollback $state
            Save-RaState $state
            Write-Host (Get-RaText 'Done') -ForegroundColor Green
            return 0
        }
        if ($P.Update) {
            if ((Invoke-RaUpdate $state (& $gameDirLazy) $P.ManifestPath $P.PackagePath)) {
                Save-RaState $state
                Remove-RaUnusedVersions $state
            }
            Write-Host (Get-RaText 'Done') -ForegroundColor Green
            return 0
        }

        $doInstall = [bool]$P.Install
        $doSteam = [bool]$P.SteamLaunchOption
        $doShortcut = [bool]$P.Shortcut -or [bool]$P.ShortcutPath
        $interactive = -not ($doInstall -or $doSteam -or $doShortcut)
        if ($interactive) {
            if ($script:RaAssumeYes) { throw 'With -Yes choose at least one of -Install, -SteamLaunchOption, -Shortcut, -Update, -Uninstall.' }
            $doInstall = Confirm-Ra (Get-RaText 'AskInstall' @($script:RaRoot)) $true
            while ($true) {
                $mode = Read-RaAnswer (Get-RaText 'AskMode')
                if ($mode -in @('1', '2', '3', '4')) { break }
            }
            $doSteam = $mode -in @('1', '3')
            $doShortcut = $mode -in @('2', '3')
        }
        if ($doSteam -or $doShortcut) {
            Write-Host ''
            Write-Host (Get-RaText 'MpWarning') -ForegroundColor Yellow
            Write-Host ''
        }
        if ($doSteam -and -not (Confirm-Ra (Get-RaText 'AskConfirmSteam') $false)) { $doSteam = $false; Write-RaLog 'Steam step declined.' }
        $shortcutFolder = $null
        if ($doShortcut) {
            $shortcutFolder = Resolve-RaShortcutFolder $P.ShortcutPath
            if (-not $P.ShortcutPath -and -not (Confirm-Ra (Get-RaText 'AskConfirmShortcut') $false)) { $doShortcut = $false; Write-RaLog 'Shortcut step declined.' }
        }

        if ($doInstall) {
            Install-RaPackage $state $P.PackagePath
            Save-RaState $state
        }
        $launcher = Get-RaLauncherPath
        if (($doSteam -or $doShortcut) -and -not $script:RaDry -and -not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
            throw (Get-RaText 'NotInstalled')
        }

        $steamStepFailed = $false
        if ($doSteam) {
            if (-not $steamRootPath) { throw 'Steam not found.' }
            try { Set-RaSteamLaunchOption $state $steamRootPath $P.SteamAccount $launcher; Save-RaState $state }
            catch { $steamStepFailed = $true; Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR }
        }
        if ($doShortcut) {
            [void](New-RaShortcut $state $shortcutFolder $launcher (& $gameDirLazy))
            Save-RaState $state
        }
        if ($doInstall) { Remove-RaUnusedVersions $state }
        if ($steamStepFailed) { return 1 }
        Write-Host (Get-RaText 'Done') -ForegroundColor Green
        return 0
    } catch {
        Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
        return 1
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    $raExit = Invoke-RaMain @{
        Install = $Install; Shortcut = $Shortcut; ShortcutPath = $ShortcutPath; SteamLaunchOption = $SteamLaunchOption
        SteamAccount = $SteamAccount; RemoveSteamLaunchOption = $RemoveSteamLaunchOption; Uninstall = $Uninstall
        DryRun = $DryRun; Yes = $Yes; PackagePath = $PackagePath; SteamRoot = $SteamRoot; Language = $Language
        CheckUpdate = $CheckUpdate; Update = $Update; Rollback = $Rollback; ManifestPath = $ManifestPath
    }
    if ($MyInvocation.MyCommand.CommandType -eq 'ExternalScript') { exit ([int]@($raExit)[-1]) }
    # Under `irm | iex` never call exit (it would close the user's terminal).
}
