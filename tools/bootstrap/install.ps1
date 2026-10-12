<#
.SYNOPSIS
    Rename Anything (HOI4) bootstrap: installs the native launcher bundle and
    optionally wires it into Steam launch options and/or a game shortcut.

.DESCRIPTION
    Runs on Windows PowerShell 5.1 and PowerShell 7, without administrator rights
    and without external modules. Proxy mode adds one version.dll to the HOI4
    installation. Workshop content and saves are untouched. See tools/bootstrap/README.md.

    Interactive (no action switches):
        irm <release-url>/install.ps1 | iex
    Noninteractive:
        & ([scriptblock]::Create((irm <release-url>/install.ps1))) -Install -SteamLaunchOption -Yes
        .\install.ps1 -Install -LoadMethod Launcher -Shortcut -ShortcutPath "$env:USERPROFILE\Desktop" -Yes

    This file is intentionally pure ASCII (Polish text uses \uXXXX escapes decoded at
    runtime), because Windows PowerShell 5.1 and `irm` may decode non-ASCII scripts
    with a legacy code page.

    install.ps1 is GENERATED from tools/bootstrap/src by tools/bootstrap/build-install.ps1.
    Edit the fragments in src/ and rebuild; never edit install.ps1 by hand.

.PARAMETER Install
    Install or verify the release pinned in this script.

.PARAMETER LoadMethod
    Proxy (default): game-local version.dll activated by -capie. Launcher: external launcher.

.PARAMETER ProxyActivation
    Flag (default): requires -capie. Config: explicit persistent activation fallback.

.PARAMETER Shortcut
    Create the launcher shortcut; requires -LoadMethod Launcher (Desktop by default).

.PARAMETER ShortcutPath
    Folder for the shortcut; implies -Shortcut.

.PARAMETER SteamLaunchOption
    Add the launcher to the HOI4 Steam launch options.

.PARAMETER SteamAccount
    Steam account id(s), comma separated, or "all".

.PARAMETER RemoveSteamLaunchOption
    Remove only our part of the HOI4 Steam launch options.

.PARAMETER Uninstall
    Remove the launch option part, our shortcuts and the installed files (the log is kept).

.PARAMETER DryRun
    Show what would happen and write nothing (alias -WhatIf).

.PARAMETER Yes
    Ask no questions; the actions must be given as switches.

.PARAMETER PackagePath
    Already downloaded release ZIP to install instead of downloading it.

.PARAMETER SteamRoot
    Steam folder override (default: HKCU\Software\Valve\Steam\SteamPath).

.PARAMETER CheckUpdate
    Ask the GitHub Releases API for the newest version (one request, on demand only).

.PARAMETER Update
    Install the newest signed release and switch to it.

.PARAMETER Rollback
    Switch back to the previous version kept on disk.

.PARAMETER ManifestPath
    Signed release-manifest.json for an offline -Update or -CheckUpdate.

.PARAMETER Language
    Message language override: pl or en.

.OUTPUTS
    System.Int32. Exit code 0 on success, 1 on failure (also the process exit code with -File).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1

.EXAMPLE
    .\install.ps1 -Install -SteamLaunchOption -SteamAccount all -Yes
#>
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('RaStrictMode', '',
    Justification = 'Under irm | iex the top level runs in the caller session; Invoke-RaMain enables Set-StrictMode -Version Latest instead.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSReviewUnusedParameter', '',
    Justification = 'The parameters are consumed by the entry point at the end of the generated install.ps1 (src/Main.ps1).')]
[CmdletBinding()]
[OutputType([int])]
param(
    [switch]$Install,
    [ValidateSet('Proxy', 'Launcher')][string]$LoadMethod,
    [ValidateSet('Flag', 'Config')][string]$ProxyActivation,
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

# GENERATED FILE: edit tools/bootstrap/src/* and run tools/bootstrap/build-install.ps1.

#region Shared/Configuration.ps1
# ---------------------------------------------------------------------------
# Release configuration: the ONLY values that change for a public release.
# ---------------------------------------------------------------------------

# GitHub "owner/repo". Release downloads and the (on-demand only) update check derive from it.
$script:RaGitHubRepo = 'PLACEHOLDER-OWNER/hoi4-rename-anything'

# RSA public key (XML, >= 2048 bit) that signs release-manifest.json of every later release.
# Placeholder until the maintainer generates the key (tools/bootstrap/sign-release-manifest.ps1).
$script:RaUpdatePublicKeyXml = 'TO-BE-GENERATED'

# Pinned release manifest. The ZIP hash is checked BEFORE extraction and every
# extracted file is checked against its own pinned hash; any mismatch, missing or
# extra file aborts the install (fail-closed). File hashes below are the current
# reviewed launcher-candidate bundle; ZipSha256 must be filled when the release ZIP
# is built. Until then the installer refuses to install anything.
$script:RaPinnedRelease = [ordered]@{
    Version             = '0.1.0'
    ZipName             = 'RenameAnything-0.1.0-win64.zip'
    ZipSha256           = 'TO-BE-PINNED-AT-RELEASE'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                       = '1AEAC3C9B0717491E239A5C263AB27ECF53DDD536317DEA69CCCADC26B9C785A'
        'LICENSE.MinHook.txt'                              = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/rename_anything_country.dll'              = '18841A6D7552224F0D34BBC05983916912969735109A548C16DE4581845E10B5'
        'runtime/rename_anything_injector.exe'             = 'DF8D60E9F1C403193DAC79FE5229793569E30D01D17F5172C5B11F1CB14063C7'
        'runtime/rename_anything_inline_editor_bridge.dll' = 'B20FBE88756F7F22D989230D6D51953D82BCCD2349CD00BDCC545C382DF8BAF6'
        'runtime/inject_inline_editor_render_bridge.exe'   = '31B510BECD5DCBA3588231C8312C32B1F2C2CC49B0CAFC8BBB2A51B18F455AE4'
    }
    # hoi4.exe builds this release's DLLs support (for -CheckUpdate messages; the launcher enforces it).
    SupportedGameSha256 = @('7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5')
}
#endregion Shared/Configuration.ps1

#region Shared/Constants.ps1
# ---------------------------------------------------------------------------
# Constants. Fixed names, formats, patterns and limits used by several slices.
# Never assigned at run time (tests do not change them either).
# ---------------------------------------------------------------------------

# --- Game and Steam -------------------------------------------------------
$script:RaAppId = '394360'                                  # Hearts of Iron IV Steam app id
$script:RaGameExeName = 'hoi4.exe'
$script:RaParadoxLauncherExeName = 'dowser.exe'             # what Steam's %command% starts
$script:RaSteamProcessName = 'steam'
$script:RaSteamRegistryKey = 'HKCU:\Software\Valve\Steam'
$script:RaSteamRegistryValue = 'SteamPath'
$script:RaSteamId64Base = [uint64]76561197960265728         # SteamID64 = base + account id
$script:RaSteamLoginUsersRelPath = 'config\loginusers.vdf'
$script:RaSteamUserDataDirName = 'userdata'
$script:RaSteamLocalConfigRelPath = 'config\localconfig.vdf'
$script:RaSteamLibraryFoldersRelPath = 'steamapps\libraryfolders.vdf'
$script:RaSteamAppManifestRelPathFormat = 'steamapps\appmanifest_{0}.acf'
$script:RaSteamCommonRelPath = 'steamapps\common'
$script:RaSteamAnonymousAccountId = '0'
$script:RaSteamCommandToken = '%command%'
$script:RaAppsPath = @('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps')
$script:RaAppManifestInstallDirPath = @('AppState', 'installdir')
$script:RaLaunchOptionsKey = 'LaunchOptions'
$script:RaPersonaNameKey = 'PersonaName'
$script:RaLibraryPathKey = 'path'
$script:RaLoginUsersRootKey = 'users'

# --- Our files ------------------------------------------------------------
$script:RaRootDirName = 'RenameAnything'                    # under %LOCALAPPDATA%
$script:RaLauncherExeName = 'RenameAnythingLauncher.exe'
$script:RaProxyFileName = 'version.dll'
$script:RaProxyBundlePath = 'proxy/version.dll'
$script:RaProxyConfigName = 'proxy.enabled'
$script:RaProxyConfigContent = "CAPIE_PROXY_ENABLED_V1`n"
$script:RaProxyFlag = '-capie'
$script:RaShortcutFileName = 'Hearts of Iron IV + Rename Anything.lnk'
$script:RaLogFileName = 'bootstrap.log'
$script:RaStateFileName = 'state.json'
$script:RaCurrentLinkName = 'current'
$script:RaManifestStoreDirName = 'manifests'
$script:RaBackupInfix = '.renameanything-backup-'
$script:RaWriteTempSuffix = '.renameanything-tmp'
$script:RaStagingPrefix = '.staging-'
$script:RaDownloadPrefix = 'download-'
$script:RaBrokenInfix = '.broken-'
$script:RaJunctionTempInfix = '.new-'
$script:RaIconIndexSuffix = ',0'

# --- Formats --------------------------------------------------------------
$script:RaLogTimestampFormat = 'yyyy-MM-ddTHH:mm:ss.fffK'
$script:RaBackupTimestampFormat = 'yyyyMMdd-HHmmss'
$script:RaBrokenTimestampFormat = 'yyyyMMddHHmmss'
$script:RaGuidFormat = 'N'
$script:RaJsonDepth = 5
$script:RaLogLevels = @('INFO', 'WARN', 'ERROR', 'PLAN')
$script:RaLogColors = @{ INFO = 'Gray'; WARN = 'Yellow'; ERROR = 'Red'; PLAN = 'Cyan' }
$script:RaUtf8Bom = [byte[]](0xEF, 0xBB, 0xBF)

# --- Patterns -------------------------------------------------------------
$script:RaSha256Pattern = '^[0-9A-Fa-f]{64}$'
$script:RaVersionPattern = '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$'
$script:RaZipNamePattern = '^[0-9A-Za-z._-]+\.zip$'
$script:RaManifestPathPattern = '^[0-9A-Za-z._-]+(/[0-9A-Za-z._-]+)*$'
$script:RaDotSegmentPattern = '(^|/)\.\.?(/|$)'
$script:RaDigitsPattern = '^\d+$'
$script:RaListSeparatorPattern = '[,; ]+'
$script:RaVdfTokenPattern = '"(?:[^"\\]|\\.)*"|\{|\}|//[^\r\n]*|[^\s{}"]+'
$script:RaVdfConditionalPattern = '^\[.*\]$'
$script:RaUnicodeEscapePattern = '\\u([0-9A-Fa-f]{4})'
# Our quoted (or unquoted) launcher path plus trailing blanks, anywhere in the options.
$script:RaWrapperPattern = '(?:"[^"]*\\{0}"|(?<!\S)[^\s"]*\\{0})[ \t]*' -f [regex]::Escape($script:RaLauncherExeName)

# --- Update channel -------------------------------------------------------
$script:RaGitHubLatestReleaseUrlFormat = 'https://api.github.com/repos/{0}/releases/latest'
$script:RaGitHubDownloadPrefixFormat = 'https://github.com/{0}/releases/download/'
$script:RaManifestAssetName = 'release-manifest.json'
$script:RaSignatureAssetName = 'release-manifest.json.sig'
$script:RaSignatureFileSuffix = '.sig'
$script:RaManifestSchemaVersion = 1
$script:RaMinRsaKeyBits = 2048
$script:RaSignatureHashAlgorithm = 'SHA256'
$script:RaRsaKeyXmlMarker = '<RSAKeyValue>'
$script:RaRepoPlaceholderMarker = 'PLACEHOLDER'
$script:RaHttpsPrefixPattern = '^https://'
$script:RaHttpUserAgent = 'RenameAnything-Bootstrap'
$script:RaHttpAccept = 'application/vnd.github+json, application/octet-stream'
$script:RaReleaseTagPrefix = 'v'

# --- Interaction ----------------------------------------------------------
$script:RaYesAnswers = @('y', 'yes', 't', 'tak')
$script:RaNoAnswers = @('n', 'no', 'nie')
$script:RaQuitAnswer = 'q'
$script:RaAllAnswer = 'a'
$script:RaAllAccountsSelector = 'all'
# Start mode menu: answer -> which integrations to set up.
$script:RaStartModes = @{
    '1' = @{ Steam = $true; Shortcut = $false }
    '2' = @{ Steam = $false; Shortcut = $true }
    '3' = @{ Steam = $true; Shortcut = $true }
    '4' = @{ Steam = $false; Shortcut = $false }
}
$script:RaShortcutFolderDesktopChoice = '1'
$script:RaShortcutFolderPickerChoice = '2'
$script:RaFolderPickerTitle = 'Rename Anything: shortcut folder'
$script:RaDefaultLanguage = 'en'
$script:RaPolishLanguage = 'pl'
$script:RaGameHashDisplayLength = 16
$script:RaExitSuccess = 0
$script:RaExitFailure = 1
#endregion Shared/Constants.ps1

#region Shared/Adapters.ps1
# ---------------------------------------------------------------------------
# Adapters (test seams) for everything outside the process: console, prompts,
# processes, registry, clock, network, file bytes, shell shortcuts, dialogs.
#
# Each adapter is a script-scope scriptblock with a fixed signature. Production
# code calls them ONLY through the small wrapper functions below or through
# "& $script:Ra<Adapter>", never the underlying cmdlet/API directly, so tests can
# replace any of them after dot-sourcing install.ps1:
#     $script:RaGetSteamProcess = { @('fake-steam') }
# Normal runs never change them.
# ---------------------------------------------------------------------------

# --- Runtime switches (set by Invoke-RaMain; tests set them directly) ------
$script:RaRoot = $null          # %LOCALAPPDATA%\RenameAnything
$script:RaLang = 'en'           # message language
$script:RaDry = $false          # -DryRun: plan only, write nothing
$script:RaAssumeYes = $false    # -Yes: never prompt
$script:RaQuiet = $false        # tests: no console output
$script:RaAfterWriteHook = $null # tests: simulate a bad write before verification
$script:RaBeforeOwnedRemove = $null # tests: simulate a replacement after ownership verification

# --- Console and prompts --------------------------------------------------
# ReadHost(Prompt) -> string
$script:RaReadHost = { param([string]$Prompt) Read-Host -Prompt $Prompt }
# WriteHost(Message, Color) -> nothing
$script:RaWriteHost = { param([string]$Message, [string]$Color) Write-RaConsoleLine -Message $Message -Color $Color }
# GetUiLanguage() -> two-letter ISO language name of the Windows UI
$script:RaGetUiLanguage = { (Get-UICulture).TwoLetterISOLanguageName }

# --- Processes, registry, clock, desktop ----------------------------------
# GetSteamProcess() -> array; empty when Steam is not running
$script:RaGetSteamProcess = { @(Get-Process -Name $script:RaSteamProcessName -ErrorAction SilentlyContinue) }
# GetRegistryValue(Path, Name) -> value or $null
$script:RaGetRegistryValue = {
    param([string]$Path, [string]$Name)
    $item = Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop
    $item.$Name
}
# Now() -> DateTime (backup and log time stamps)
$script:RaNow = { Get-Date }
# DesktopPath() -> folder
$script:RaDesktopPath = { [Environment]::GetFolderPath('Desktop') }
# FolderPicker(DefaultFolder) -> folder or $null. $null adapter = real FolderBrowserDialog.
$script:RaFolderPicker = $null

# --- Network --------------------------------------------------------------
# Only network access of this script. Called solely by -Install (ZIP download), -CheckUpdate and
# -Update. Sends no identifiers: just a fixed User-Agent required by the GitHub API.
# HttpGet(Url) -> byte[]
$script:RaHttpGet = {
    param([string]$Url)
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    } catch { Write-Verbose 'TLS 1.2 flag not settable; using defaults.' }
    $wc = New-Object System.Net.WebClient
    try {
        $wc.Headers['User-Agent'] = $script:RaHttpUserAgent
        $wc.Headers['Accept'] = $script:RaHttpAccept
        return , $wc.DownloadData($Url)
    } finally { $wc.Dispose() }
}

# --- File bytes (the byte-exact localconfig.vdf path) ---------------------
# ReadAllBytes(Path) -> byte[]
$script:RaReadAllBytes = { param([string]$Path) , [IO.File]::ReadAllBytes($Path) }
# WriteAllBytes(Path, Bytes) -> nothing
$script:RaWriteAllBytes = { param([string]$Path, [byte[]]$Bytes) [IO.File]::WriteAllBytes($Path, $Bytes) }
# CopyFile(Source, Destination) -> nothing (overwrites)
$script:RaCopyFile = { param([string]$Source, [string]$Destination) [IO.File]::Copy($Source, $Destination, $true) }

# --- Shell shortcuts (.lnk through WScript.Shell COM) ----------------------
# ReadShortcut(Path) -> [pscustomobject]@{ TargetPath; Arguments; Description; IconLocation }
$script:RaReadShortcut = {
    param([string]$Path)
    $sh = New-Object -ComObject WScript.Shell
    try {
        $s = $sh.CreateShortcut($Path)
        [pscustomobject]@{ TargetPath = $s.TargetPath; Arguments = $s.Arguments; Description = $s.Description; IconLocation = $s.IconLocation }
    } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($sh) }
}
# WriteShortcut(Path, Spec) -> nothing. Spec: TargetPath, Arguments, WorkingDirectory, IconLocation, Description.
$script:RaWriteShortcut = {
    param([string]$Path, [psobject]$Spec)
    $sh = New-Object -ComObject WScript.Shell
    try {
        $s = $sh.CreateShortcut($Path)
        $s.TargetPath = $Spec.TargetPath
        $s.Arguments = $Spec.Arguments
        $s.WorkingDirectory = $Spec.WorkingDirectory
        $s.IconLocation = $Spec.IconLocation
        $s.Description = $Spec.Description
        $s.Save()
    } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($sh) }
}

function Write-RaConsoleLine {
    <#
    .SYNOPSIS
        Default console adapter: writes one coloured line to the host.
    .DESCRIPTION
        The only place that calls Write-Host. Everything user-visible goes through
        $script:RaWriteHost, which calls this by default.
    .PARAMETER Message
        Text to print.
    .PARAMETER Color
        ConsoleColor name; empty means the default colour.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaConsoleLine -Message 'Done.' -Color 'Green'
    #>
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '',
        Justification = 'Interactive installer UI: coloured text for the user, deliberately not part of the output stream.')]
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [AllowEmptyString()][string]$Message,
        [AllowEmptyString()][string]$Color
    )
    if ($Color) { Write-Host $Message -ForegroundColor $Color } else { Write-Host $Message }
}

function Write-RaHost {
    <#
    .SYNOPSIS
        Prints a user-facing line through the console adapter.
    .DESCRIPTION
        Thin wrapper over $script:RaWriteHost so that callers never depend on Write-Host.
    .PARAMETER Message
        Text to print.
    .PARAMETER Color
        Optional ConsoleColor name.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaHost (Get-RaText 'Done') -Color Green
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$Message = '',
        [AllowEmptyString()][string]$Color = ''
    )
    & $script:RaWriteHost $Message $Color
}

function Get-RaNow {
    <#
    .SYNOPSIS
        Current time from the clock adapter.
    .DESCRIPTION
        Wrapper over $script:RaNow so tests can freeze time (deterministic backup names).
    .OUTPUTS
        System.DateTime
    .EXAMPLE
        (Get-RaNow).ToString($script:RaBackupTimestampFormat)
    #>
    [CmdletBinding()]
    [OutputType([datetime])]
    param()
    return [datetime](& $script:RaNow)
}

function New-RaUniqueName {
    <#
    .SYNOPSIS
        Returns a fresh 32-hex-digit GUID string for temporary names.
    .DESCRIPTION
        Used for download, staging and junction temporary names under the root folder.
    .OUTPUTS
        System.String
    .EXAMPLE
        $script:RaStagingPrefix + (New-RaUniqueName)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return [guid]::NewGuid().ToString($script:RaGuidFormat)
}

function Get-RaHttpContent {
    <#
    .SYNOPSIS
        Downloads a URL through the network adapter (HTTPS only).
    .DESCRIPTION
        The single entry point for network access. Plain HTTP and other schemes are refused.
    .PARAMETER Url
        HTTPS URL.
    .OUTPUTS
        System.Byte[]
    .EXAMPLE
        Get-RaHttpContent 'https://api.github.com/repos/o/r/releases/latest'
    #>
    [CmdletBinding()]
    [OutputType([byte[]], [object[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Url)
    if ($Url -notmatch $script:RaHttpsPrefixPattern) { throw "Refusing non-HTTPS URL: $Url" }
    return , ([byte[]](& $script:RaHttpGet $Url))
}
#endregion Shared/Adapters.ps1

#region Shared/Messages.ps1
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
#endregion Shared/Messages.ps1

#region Shared/Logging.ps1
# ---------------------------------------------------------------------------
# Logging and prompts (shared by every slice).
# ---------------------------------------------------------------------------

function Get-RaLogPath {
    <#
    .SYNOPSIS
        Path of bootstrap.log under the root folder.
    .DESCRIPTION
        The log is the only file kept by -Uninstall.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaLogPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $script:RaRoot $script:RaLogFileName)
}

function Write-RaLog {
    <#
    .SYNOPSIS
        Writes a message to the console (coloured by level) and appends it to bootstrap.log.
    .DESCRIPTION
        The console line is skipped with -NoConsole or when $script:RaQuiet is set.
        Nothing is written to disk in dry-run mode. A failing log write never fails the run.
    .PARAMETER Message
        Text to log.
    .PARAMETER Level
        INFO, WARN, ERROR or PLAN (dry-run plan lines).
    .PARAMETER NoConsole
        Log to the file only.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaLog 'Steam step declined.' -Level WARN
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$Message,
        [ValidateSet('INFO', 'WARN', 'ERROR', 'PLAN')][string]$Level = 'INFO',
        [switch]$NoConsole
    )
    $line = '{0} [{1}] {2}' -f (Get-RaNow).ToString($script:RaLogTimestampFormat), $Level, $Message
    if (-not $NoConsole -and -not $script:RaQuiet) { Write-RaHost $Message -Color $script:RaLogColors[$Level] }
    if ($script:RaDry) { return }
    try {
        Initialize-RaRoot
        $enc = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::AppendAllText((Get-RaLogPath), $line + [Environment]::NewLine, $enc)
    } catch { Write-Verbose "Log write failed: $($_.Exception.Message)" }
}

function Read-RaAnswer {
    <#
    .SYNOPSIS
        Asks the user a question and returns the trimmed answer.
    .DESCRIPTION
        Goes through the $script:RaReadHost adapter; $null becomes an empty string.
    .PARAMETER Prompt
        Question text (\uXXXX escapes are decoded).
    .OUTPUTS
        System.String
    .EXAMPLE
        $mode = Read-RaAnswer (Get-RaText 'AskMode')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Prompt)
    $answer = & $script:RaReadHost (ConvertFrom-RaEscapedText $Prompt)
    if ($null -eq $answer) { return '' }
    return ([string]$answer).Trim()
}

function Confirm-Ra {
    <#
    .SYNOPSIS
        Asks a yes/no question until the answer is understood.
    .DESCRIPTION
        Accepts English and Polish answers; an empty answer returns -Default.
        With -Yes ($script:RaAssumeYes) the answer is always yes and nothing is asked.
    .PARAMETER Prompt
        Question text.
    .PARAMETER Default
        Result for an empty answer.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Confirm-Ra (Get-RaText 'AskConfirmSteam') $false
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Prompt,
        [Parameter(Position = 1)][bool]$Default = $false
    )
    if ($script:RaAssumeYes) { return $true }
    while ($true) {
        $a = (Read-RaAnswer $Prompt).ToLowerInvariant()
        if ($a -eq '') { return $Default }
        if ($a -in $script:RaYesAnswers) { return $true }
        if ($a -in $script:RaNoAnswers) { return $false }
    }
}
#endregion Shared/Logging.ps1

#region Shared/Vdf.ps1
# ---------------------------------------------------------------------------
# Text VDF (Valve KeyValues) parser with source offsets. Offsets let the Steam
# slice replace exactly the bytes of one value and nothing else.
# ---------------------------------------------------------------------------

function ConvertFrom-RaVdfString {
    <#
    .SYNOPSIS
        Unescapes the body of a quoted VDF string.
    .DESCRIPTION
        Handles \\, \", \n and \t; any other backslash sequence is kept verbatim,
        as Steam does.
    .PARAMETER Raw
        String body without the surrounding quotes.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertFrom-RaVdfString 'C:\\Games\\x.exe'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Raw)
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
            [void]$sb.Append($c)
            $i++
        }
    }
    return $sb.ToString()
}

function ConvertTo-RaVdfString {
    <#
    .SYNOPSIS
        Escapes a value for a quoted VDF string.
    .DESCRIPTION
        Inverse of ConvertFrom-RaVdfString for \, ", newline and tab.
    .PARAMETER Value
        Plain value.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaVdfString '"C:\a b\x.exe" %command%'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`t", '\t')
}

function New-RaVdfNode {
    <#
    .SYNOPSIS
        Creates a VDF tree node.
    .DESCRIPTION
        Leaf nodes carry Value/ValueStart/ValueEnd; blocks carry OpenIndex/CloseIndex
        and Children. All indexes are character offsets in the parsed text (-1 = none).
    .PARAMETER Key
        Unescaped key.
    .PARAMETER KeyStart
        Offset of the key token.
    .PARAMETER IsBlock
        $true for a { } block, $false for a key/value pair.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        New-RaVdfNode -Key '<root>' -KeyStart -1 -IsBlock $true
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Key,
        [Parameter(Mandatory)][int]$KeyStart,
        [Parameter(Mandatory)][bool]$IsBlock
    )
    return [pscustomobject]@{
        Key = $Key; KeyStart = $KeyStart; IsBlock = $IsBlock
        Value = $null; ValueStart = -1; ValueEnd = -1
        OpenIndex = -1; CloseIndex = -1
        Children = (New-Object System.Collections.ArrayList)
    }
}

function ConvertFrom-RaVdf {
    <#
    .SYNOPSIS
        Parses text VDF into a node tree with source offsets.
    .DESCRIPTION
        Returns a synthetic root block. Comments (//) and [conditional] tokens are
        skipped. Throws on any structural problem: stray characters, a block without a
        key, a key without a value, unbalanced braces, an unterminated string.
    .PARAMETER Text
        Whole file text.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        $root = ConvertFrom-RaVdf (Read-RaTextFile $path).Text
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text)
    $root = New-RaVdfNode -Key '<root>' -KeyStart -1 -IsBlock $true
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    $pendingKey = $null
    $pendingKeyStart = -1
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
        if (-not $isQuoted -and $null -eq $pendingKey -and $tok -match $script:RaVdfConditionalPattern) { continue }
        $value = if ($isQuoted) { ConvertFrom-RaVdfString $tok.Substring(1, $tok.Length - 2) } else { $tok }
        if ($null -eq $pendingKey) {
            $pendingKey = $value
            $pendingKeyStart = $m.Index
        } else {
            $node = New-RaVdfNode -Key $pendingKey -KeyStart $pendingKeyStart -IsBlock $false
            $node.Value = $value
            $node.ValueStart = $m.Index
            $node.ValueEnd = $m.Index + $m.Length
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

function Find-RaVdfChild {
    <#
    .SYNOPSIS
        Returns the direct children of a node with the given key (case-insensitive).
    .DESCRIPTION
        Always returns an array (possibly empty) so callers can check .Count for
        missing or duplicate keys.
    .PARAMETER Node
        Parent node.
    .PARAMETER Key
        Key to match.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaVdfChild $app 'LaunchOptions'
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$Node,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$Key
    )
    return @($Node.Children | Where-Object { $_.Key -ieq $Key })
}

function Find-RaVdfPath {
    <#
    .SYNOPSIS
        Returns all nodes reached by a case-insensitive key path.
    .DESCRIPTION
        Follows every matching block at each level, so duplicates are visible to the caller.
    .PARAMETER Root
        Node to start from.
    .PARAMETER Path
        Keys to follow, outermost first.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaVdfPath $root @('AppState', 'installdir')
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$Root,
        [Parameter(Mandatory, Position = 1)][string[]]$Path
    )
    $current = @($Root)
    foreach ($k in $Path) {
        $next = @()
        foreach ($n in $current) { if ($n.IsBlock) { $next += Find-RaVdfChild $n $k } }
        $current = $next
    }
    return $current
}
#endregion Shared/Vdf.ps1

#region Shared/TextFile.ps1
# ---------------------------------------------------------------------------
# Byte-exact text file I/O and hashing.
# ---------------------------------------------------------------------------

function Read-RaTextFile {
    <#
    .SYNOPSIS
        Reads a UTF-8 file strictly, keeping everything needed to write it back byte for byte.
    .DESCRIPTION
        Refuses invalid UTF-8 and files whose UTF-8 round trip differs. Returns the text,
        whether a BOM was present, the dominant newline and the original bytes.
    .PARAMETER Path
        File to read.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Text, HasBom, Newline, Bytes)
    .EXAMPLE
        $file = Read-RaTextFile $configPath
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $bytes = [byte[]](& $script:RaReadAllBytes $Path)
    $bom = $script:RaUtf8Bom
    $hasBom = ($bytes.Length -ge $bom.Length -and $bytes[0] -eq $bom[0] -and $bytes[1] -eq $bom[1] -and $bytes[2] -eq $bom[2])
    $offset = if ($hasBom) { $bom.Length } else { 0 }
    $strict = New-Object System.Text.UTF8Encoding($false, $true)
    try { $text = $strict.GetString($bytes, $offset, $bytes.Length - $offset) }
    catch { throw "File is not valid UTF-8, refusing to edit: $Path" }
    $round = $strict.GetBytes($text)
    if ($round.Length -ne ($bytes.Length - $offset)) { throw "UTF-8 round trip mismatch, refusing to edit: $Path" }
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    return [pscustomobject]@{ Text = $text; HasBom = $hasBom; Newline = $newline; Bytes = $bytes }
}

function ConvertTo-RaFileContent {
    <#
    .SYNOPSIS
        Encodes text as UTF-8, optionally with a BOM.
    .DESCRIPTION
        Counterpart of Read-RaTextFile: produces exactly the bytes a file should have.
    .PARAMETER Text
        Text to encode.
    .PARAMETER HasBom
        Prepend the UTF-8 BOM.
    .OUTPUTS
        System.Byte[]
    .EXAMPLE
        ConvertTo-RaFileContent $edit.Text $file.HasBom
    #>
    [CmdletBinding()]
    [OutputType([byte[]], [object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][bool]$HasBom
    )
    $body = (New-Object System.Text.UTF8Encoding($false)).GetBytes($Text)
    if (-not $HasBom) { return , $body }
    return , ([byte[]](@($script:RaUtf8Bom) + $body))
}

function Test-RaBytesEqual {
    <#
    .SYNOPSIS
        Compares two byte arrays.
    .DESCRIPTION
        Length first, then byte by byte.
    .PARAMETER A
        First array.
    .PARAMETER B
        Second array.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaBytesEqual $after $expected
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$A,
        [Parameter(Mandatory, Position = 1)][AllowEmptyCollection()][byte[]]$B
    )
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}

function Get-RaSha256OfData {
    <#
    .SYNOPSIS
        SHA-256 of a byte array as upper-case hex.
    .DESCRIPTION
        Same format as Get-FileHash, so values can be compared directly.
    .PARAMETER Bytes
        Data to hash.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSha256OfData $file.Bytes
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') } finally { $sha.Dispose() }
}

function Get-RaFileSha256 {
    <#
    .SYNOPSIS
        SHA-256 of a file as upper-case hex.
    .DESCRIPTION
        Single place for file hashing (Get-FileHash, read-only).
    .PARAMETER Path
        File to hash.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaFileSha256 $zipPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
#endregion Shared/TextFile.ps1

#region Shared/State.ps1
# ---------------------------------------------------------------------------
# Root folder layout and state.json.
#   <root>\<version>\           verified bundle
#   <root>\current              junction to the active version
#   <root>\manifests\<v>.json   manifest of each installed version
#   <root>\state.json           installed/previous version, our shortcuts and Steam edits
#   <root>\bootstrap.log
# ---------------------------------------------------------------------------

function Get-RaStatePath {
    <#
    .SYNOPSIS
        Path of state.json under the root folder.
    .DESCRIPTION
        state.json is the source of truth for the active version and our changes.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaStatePath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $script:RaRoot $script:RaStateFileName)
}

function New-RaState {
    <#
    .SYNOPSIS
        Creates an empty state object.
    .DESCRIPTION
        Shape: InstalledVersion, PreviousVersion, Shortcuts (paths), SteamEntries
        (AccountId, ConfigPath, OriginalValue, WrittenValue, LauncherPath).
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        $state = New-RaState
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param()
    return [pscustomobject]@{
        InstalledVersion = $null; PreviousVersion = $null; Shortcuts = @(); SteamEntries = @()
        LoadMethod = 'Launcher'; Proxy = $null; ProxyActivation = 'Flag'; ProxyConfigSha256 = $null
    }
}

function Read-RaState {
    <#
    .SYNOPSIS
        Loads state.json, tolerating a missing or unreadable file.
    .DESCRIPTION
        Unknown properties are ignored; an unreadable file is logged and treated as empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        $state = Read-RaState
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param()
    $p = Get-RaStatePath
    $state = New-RaState
    if (Test-Path -LiteralPath $p) {
        try {
            $j = Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($j.PSObject.Properties['InstalledVersion']) { $state.InstalledVersion = $j.InstalledVersion }
            if ($j.PSObject.Properties['PreviousVersion']) { $state.PreviousVersion = $j.PreviousVersion }
            if ($j.PSObject.Properties['Shortcuts']) { $state.Shortcuts = @($j.Shortcuts | Where-Object { $_ }) }
            if ($j.PSObject.Properties['SteamEntries']) { $state.SteamEntries = @($j.SteamEntries | Where-Object { $_ }) }
            if ($j.PSObject.Properties['LoadMethod'] -and $j.LoadMethod -in @('Proxy', 'Launcher')) { $state.LoadMethod = $j.LoadMethod }
            if ($j.PSObject.Properties['Proxy']) { $state.Proxy = $j.Proxy }
            if ($j.PSObject.Properties['ProxyActivation'] -and $j.ProxyActivation -in @('Flag', 'Config')) { $state.ProxyActivation = $j.ProxyActivation }
            if ($j.PSObject.Properties['ProxyConfigSha256']) { $state.ProxyConfigSha256 = $j.ProxyConfigSha256 }
        } catch { Write-RaLog "state.json unreadable, ignoring: $($_.Exception.Message)" -Level WARN }
    }
    return $state
}

function Save-RaState {
    <#
    .SYNOPSIS
        Writes state.json (UTF-8 without BOM). No-op in dry-run mode.
    .DESCRIPTION
        Creates the root folder when needed.
    .PARAMETER State
        State object from Read-RaState/New-RaState.
    .OUTPUTS
        None.
    .EXAMPLE
        Save-RaState $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if ($script:RaDry) { return }
    Initialize-RaRoot
    $json = [pscustomobject]@{
        InstalledVersion = $State.InstalledVersion
        PreviousVersion  = $State.PreviousVersion
        Shortcuts        = @($State.Shortcuts)
        SteamEntries     = @($State.SteamEntries)
        LoadMethod       = $State.LoadMethod
        Proxy            = $State.Proxy
        ProxyActivation  = $State.ProxyActivation
        ProxyConfigSha256 = $State.ProxyConfigSha256
    } | ConvertTo-Json -Depth $script:RaJsonDepth
    [IO.File]::WriteAllText((Get-RaStatePath), $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-RaCurrentLink {
    <#
    .SYNOPSIS
        Path of the 'current' junction.
    .DESCRIPTION
        Steam and shortcuts always point through it, so updates never edit them.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaCurrentLink
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $script:RaRoot $script:RaCurrentLinkName)
}

function Get-RaLauncherPath {
    <#
    .SYNOPSIS
        Stable launcher path used by Steam and shortcuts.
    .DESCRIPTION
        <root>\current\RenameAnythingLauncher.exe: updates only re-point the junction.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaLauncherPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path (Get-RaCurrentLink) $script:RaLauncherExeName)
}

function Get-RaVersionDir {
    <#
    .SYNOPSIS
        Folder of one installed version.
    .DESCRIPTION
        <root>\<version>.
    .PARAMETER Version
        Version string, e.g. 0.1.0.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaVersionDir '0.1.0'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Version)
    return (Join-Path $script:RaRoot $Version)
}

function Get-RaManifestStorePath {
    <#
    .SYNOPSIS
        Path where the manifest of an installed version is stored.
    .DESCRIPTION
        <root>\manifests\<version>.json.
    .PARAMETER Version
        Version string.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaManifestStorePath '0.2.0'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Version)
    return (Join-Path (Join-Path $script:RaRoot $script:RaManifestStoreDirName) "$Version.json")
}

function Initialize-RaRoot {
    <#
    .SYNOPSIS
        Creates the root folder if it does not exist.
    .DESCRIPTION
        Idempotent helper used before the first write of a run.
    .OUTPUTS
        None.
    .EXAMPLE
        Initialize-RaRoot
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param()
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { New-Item -ItemType Directory -Path $script:RaRoot -Force | Out-Null }
}
#endregion Shared/State.ps1

#region Shared/Manifest.ps1
# ---------------------------------------------------------------------------
# Release manifest, hashes and signatures (shared by install and update).
# A manifest is an ordered dictionary:
#   Version, ZipName, ZipSha256, Files (relative path -> SHA256), SupportedGameSha256
# ---------------------------------------------------------------------------

function Test-RaSha256Text {
    <#
    .SYNOPSIS
        Tests whether a value is a 64-digit hexadecimal SHA-256.
    .DESCRIPTION
        Case-insensitive; used for every hash read from a manifest.
    .PARAMETER Value
        Candidate value.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaSha256Text $Manifest.ZipSha256
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    return ($Value -match $script:RaSha256Pattern)
}

function Test-RaManifest {
    <#
    .SYNOPSIS
        Validates a release manifest and throws on the first problem.
    .DESCRIPTION
        Checks version, ZIP name, ZIP hash (a placeholder means "no release yet"),
        every file path (no absolute paths, no . or .. segments) and hash, that the
        launcher is included, and the supported game hashes. Adds an empty
        SupportedGameSha256 list when it is missing.
    .PARAMETER Manifest
        Manifest dictionary.
    .OUTPUTS
        None.
    .EXAMPLE
        Test-RaManifest $script:RaPinnedRelease
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest)
    if ($Manifest.Version -notmatch $script:RaVersionPattern) { throw 'Release manifest: invalid version.' }
    if (-not (Test-RaSha256Text $Manifest.ZipSha256)) {
        throw 'Pinned release manifest is not configured (ZipSha256 placeholder). No release is published for this script yet; refusing to install.'
    }
    if ($Manifest.ZipName -notmatch $script:RaZipNamePattern) { throw 'Release manifest: invalid ZIP name.' }
    if ($Manifest.Files.Count -lt 1) { throw 'Release manifest: no files.' }
    foreach ($k in $Manifest.Files.Keys) {
        if (-not (Test-RaSha256Text $Manifest.Files[$k])) { throw "Release manifest: bad hash for $k" }
        if ($k -notmatch $script:RaManifestPathPattern -or $k -match $script:RaDotSegmentPattern) { throw "Release manifest: unsafe path $k" }
    }
    if (-not $Manifest.Files.Contains($script:RaLauncherExeName)) { throw 'Release manifest: launcher missing.' }
    if (-not $Manifest.Contains('SupportedGameSha256')) { $Manifest['SupportedGameSha256'] = @() }
    foreach ($h in @($Manifest.SupportedGameSha256 | Where-Object { $_ })) {
        if (-not (Test-RaSha256Text $h)) { throw 'Release manifest: bad game hash.' }
    }
}

function Compare-RaVersion {
    <#
    .SYNOPSIS
        Compares the numeric x.y.z part of two versions.
    .DESCRIPTION
        Pre-release/build suffixes after - or + are ignored.
    .PARAMETER A
        First version.
    .PARAMETER B
        Second version.
    .OUTPUTS
        System.Int32 (-1, 0 or 1)
    .EXAMPLE
        Compare-RaVersion '0.2.0' '0.1.9'
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$A,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$B
    )
    $va = [version](($A -split '[-+]')[0])
    $vb = [version](($B -split '[-+]')[0])
    return $va.CompareTo($vb)
}

function Test-RaInstalledVersion {
    <#
    .SYNOPSIS
        Verifies an installed version folder against its manifest.
    .DESCRIPTION
        True only when the folder holds exactly the manifest's files (same count) and
        every file has its pinned SHA-256.
    .PARAMETER Manifest
        Manifest dictionary.
    .PARAMETER Dir
        Version folder (or staging folder).
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaInstalledVersion $m (Get-RaVersionDir $m.Version)
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Dir
    )
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return $false }
    $files = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force)
    if ($files.Count -ne $Manifest.Files.Count) { return $false }
    foreach ($k in $Manifest.Files.Keys) {
        $p = Join-Path $Dir ($k -replace '/', '\')
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return $false }
        if ((Get-RaFileSha256 $p) -ne $Manifest.Files[$k].ToUpperInvariant()) { return $false }
    }
    return $true
}

function Expand-RaVerifiedZip {
    <#
    .SYNOPSIS
        Extracts a release ZIP, allowing only manifest entries and checking each file hash.
    .DESCRIPTION
        Refuses unexpected, duplicate and missing entries (which also blocks zip-slip
        names such as ../x, because they are never in the manifest). Directory entries
        are skipped.
    .PARAMETER Manifest
        Manifest dictionary.
    .PARAMETER ZipPath
        ZIP file whose own hash was already verified.
    .PARAMETER Destination
        Empty staging folder.
    .OUTPUTS
        None.
    .EXAMPLE
        Expand-RaVerifiedZip $m $zipPath $staging
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$ZipPath,
        [Parameter(Mandatory, Position = 2)][ValidateNotNullOrEmpty()][string]$Destination
    )
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
            $h = Get-RaFileSha256 $dest
            if ($h -ne $Manifest.Files[$name].ToUpperInvariant()) { throw "SHA256 mismatch for $name (got $h)" }
        }
    } finally { $zip.Dispose() }
    foreach ($k in $Manifest.Files.Keys) { if (-not $seen.ContainsKey($k)) { throw "Package is missing $k" } }
}

function ConvertTo-RaManifestJson {
    <#
    .SYNOPSIS
        Serializes a manifest in the release-manifest.json schema.
    .DESCRIPTION
        Same schema that sign-release-manifest.ps1 -MakeManifest writes.
    .PARAMETER Manifest
        Manifest dictionary.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaManifestJson $m
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest)
    return ([pscustomobject]@{
            schema = $script:RaManifestSchemaVersion; version = $Manifest.Version; zipName = $Manifest.ZipName; zipSha256 = $Manifest.ZipSha256
            files = [pscustomobject]$Manifest.Files; supportedGameSha256 = @($Manifest.SupportedGameSha256)
        } | ConvertTo-Json -Depth $script:RaJsonDepth)
}

function ConvertFrom-RaManifestJson {
    <#
    .SYNOPSIS
        Parses and validates release-manifest.json.
    .DESCRIPTION
        Rejects unknown schema versions, then runs Test-RaManifest on the result.
    .PARAMETER Json
        JSON text.
    .OUTPUTS
        System.Collections.Specialized.OrderedDictionary
    .EXAMPLE
        ConvertFrom-RaManifestJson ([IO.File]::ReadAllText($path))
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Specialized.OrderedDictionary])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Json)
    $j = $Json | ConvertFrom-Json
    if ($j.schema -ne $script:RaManifestSchemaVersion) { throw 'Release manifest: unsupported schema.' }
    $files = [ordered]@{}
    foreach ($p in $j.files.PSObject.Properties) { $files[$p.Name] = [string]$p.Value }
    $m = [ordered]@{
        Version = [string]$j.version; ZipName = [string]$j.zipName; ZipSha256 = [string]$j.zipSha256
        Files = $files; SupportedGameSha256 = @($j.supportedGameSha256 | ForEach-Object { [string]$_ })
    }
    Test-RaManifest $m
    return $m
}

function Read-RaStoredManifest {
    <#
    .SYNOPSIS
        Returns the manifest of an installed version, or $null.
    .DESCRIPTION
        The pinned release is answered from memory; other versions come from
        <root>\manifests\<version>.json. An unreadable or invalid file yields $null.
    .PARAMETER Version
        Version string.
    .OUTPUTS
        System.Collections.IDictionary
    .EXAMPLE
        Read-RaStoredManifest $state.PreviousVersion
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.IDictionary], [System.Collections.Specialized.OrderedDictionary])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Version)
    if ($Version -eq $script:RaPinnedRelease.Version) { return $script:RaPinnedRelease }
    $p = Get-RaManifestStorePath $Version
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    try { return (ConvertFrom-RaManifestJson ([IO.File]::ReadAllText($p))) } catch { return $null }
}

function Save-RaStoredManifest {
    <#
    .SYNOPSIS
        Stores the manifest of a freshly installed version.
    .DESCRIPTION
        Written as UTF-8 without BOM to <root>\manifests\<version>.json.
    .PARAMETER Manifest
        Manifest dictionary.
    .OUTPUTS
        None.
    .EXAMPLE
        Save-RaStoredManifest $m
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest)
    $store = Get-RaManifestStorePath $Manifest.Version
    New-Item -ItemType Directory -Path (Split-Path -Parent $store) -Force | Out-Null
    [IO.File]::WriteAllText($store, (ConvertTo-RaManifestJson $Manifest), (New-Object System.Text.UTF8Encoding($false)))
}

function Test-RaSignature {
    <#
    .SYNOPSIS
        Verifies an RSA PKCS#1 v1.5 / SHA-256 signature over exact bytes.
    .DESCRIPTION
        The public key is the one pinned in this script. Throws when the key is still a
        placeholder or too small; returns $false for a bad or malformed signature.
    .PARAMETER Data
        Signed bytes (the manifest file as downloaded).
    .PARAMETER SignatureBase64
        Base64 signature (the .sig asset).
    .PARAMETER PublicKeyXml
        RSA public key in .NET XML format.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaSignature $bytes $sigText $script:RaUpdatePublicKeyXml
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$Data,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$SignatureBase64,
        [Parameter(Mandatory, Position = 2)][AllowEmptyString()][string]$PublicKeyXml
    )
    if ($PublicKeyXml -notmatch $script:RaRsaKeyXmlMarker) {
        throw 'The update signing key is not configured in this script yet; refusing to trust a downloaded manifest.'
    }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    try {
        $rsa.FromXmlString($PublicKeyXml)
        if ($rsa.KeySize -lt $script:RaMinRsaKeyBits) { throw 'Pinned update key is too small.' }
        try {
            $sig = [Convert]::FromBase64String($SignatureBase64.Trim())
            return [bool]$rsa.VerifyData($Data, $script:RaSignatureHashAlgorithm, $sig)
        } catch { return $false }
    } finally { $rsa.Dispose() }
}
#endregion Shared/Manifest.ps1

#region Shared/Steam.ps1
# ---------------------------------------------------------------------------
# Steam discovery (read-only): install folder, accounts, game folder, process.
# ---------------------------------------------------------------------------

function Get-RaSteamRoot {
    <#
    .SYNOPSIS
        Finds the Steam folder.
    .DESCRIPTION
        Uses -Override when given (must exist), otherwise HKCU\Software\Valve\Steam\SteamPath
        through the registry adapter. Throws when Steam cannot be found.
    .PARAMETER Override
        Folder passed with -SteamRoot.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSteamRoot ''
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Override)
    if ($Override) {
        if (-not (Test-Path -LiteralPath $Override -PathType Container)) { throw "Steam folder not found: $Override" }
        return (Resolve-Path -LiteralPath $Override).ProviderPath
    }
    $p = $null
    try {
        $p = & $script:RaGetRegistryValue $script:RaSteamRegistryKey $script:RaSteamRegistryValue
    } catch { Write-Verbose "Steam registry value unavailable: $($_.Exception.Message)" }
    if (-not $p) { throw 'Steam not found (HKCU\Software\Valve\Steam\SteamPath missing).' }
    $p = [IO.Path]::GetFullPath(($p -replace '/', '\'))
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw "Steam folder from registry does not exist: $p" }
    return $p
}

function Get-RaSteamPersonaName {
    <#
    .SYNOPSIS
        Maps Steam account ids to persona names from config\loginusers.vdf.
    .DESCRIPTION
        Best effort: a missing or unreadable file yields an empty table.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Collections.Hashtable (account id -> persona name)
    .EXAMPLE
        (Get-RaSteamPersonaName $steam)['111']
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $names = @{}
    $login = Join-Path $SteamRootPath $script:RaSteamLoginUsersRelPath
    if (-not (Test-Path -LiteralPath $login)) { return $names }
    try {
        $root = ConvertFrom-RaVdf (Read-RaTextFile $login).Text
        foreach ($u in @(Find-RaVdfPath $root @($script:RaLoginUsersRootKey))) {
            foreach ($c in $u.Children) {
                if ($c.IsBlock -and $c.Key -match $script:RaDigitsPattern) {
                    $acc = [string]([uint64]$c.Key - $script:RaSteamId64Base)
                    $pn = @(Find-RaVdfChild $c $script:RaPersonaNameKey)
                    if ($pn.Count -eq 1) { $names[$acc] = $pn[0].Value }
                }
            }
        }
    } catch { Write-Verbose "loginusers.vdf unreadable: $($_.Exception.Message)" }
    return $names
}

function Get-RaSteamAccount {
    <#
    .SYNOPSIS
        Lists Steam accounts that have a localconfig.vdf.
    .DESCRIPTION
        Scans userdata\<id> (numeric, non-zero ids), sorted by name, and attaches the
        persona name when known.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Id, Name, ConfigPath)
    .EXAMPLE
        @(Get-RaSteamAccount $steam)
    #>
    [CmdletBinding()]
    [OutputType([psobject], [object[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $names = Get-RaSteamPersonaName $SteamRootPath
    $result = @()
    $ud = Join-Path $SteamRootPath $script:RaSteamUserDataDirName
    if (Test-Path -LiteralPath $ud) {
        foreach ($d in (Get-ChildItem -LiteralPath $ud -Directory | Sort-Object Name)) {
            if ($d.Name -notmatch $script:RaDigitsPattern -or $d.Name -eq $script:RaSteamAnonymousAccountId) { continue }
            $cfg = Join-Path $d.FullName $script:RaSteamLocalConfigRelPath
            if (Test-Path -LiteralPath $cfg -PathType Leaf) {
                $result += [pscustomobject]@{ Id = $d.Name; Name = $names[$d.Name]; ConfigPath = $cfg }
            }
        }
    }
    return $result
}

function Format-RaAccountList {
    <#
    .SYNOPSIS
        Comma-separated account ids for error messages.
    .DESCRIPTION
        Used by the account selection errors.
    .PARAMETER Accounts
        Accounts from Get-RaSteamAccount.
    .OUTPUTS
        System.String
    .EXAMPLE
        Format-RaAccountList $accounts
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Accounts)
    return (($Accounts | ForEach-Object Id) -join ', ')
}

function Test-RaSteamRunning {
    <#
    .SYNOPSIS
        Tests whether Steam is running (process adapter).
    .DESCRIPTION
        Only reads the process list; the script never stops Steam.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (Test-RaSteamRunning) { 'close Steam first' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    return (@(& $script:RaGetSteamProcess).Count -gt 0)
}

function Wait-RaSteamClosed {
    <#
    .SYNOPSIS
        Waits until the user has closed Steam.
    .DESCRIPTION
        Returns $true when Steam is not running. With -Yes it returns $false at once
        (and logs why); interactively it asks again until Steam is closed or the user
        types Q. It never stops the process itself.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Wait-RaSteamClosed)) { return $false }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    if (-not (Test-RaSteamRunning)) { return $true }
    if ($script:RaAssumeYes) {
        Write-RaLog (Get-RaText 'SteamRunningNonInteractive') -Level WARN
        return $false
    }
    while (Test-RaSteamRunning) {
        $a = Read-RaAnswer (Get-RaText 'SteamRunning')
        if ($a -ieq $script:RaQuitAnswer) { return $false }
    }
    return $true
}

function Select-RaSteamAccount {
    <#
    .SYNOPSIS
        Chooses the Steam accounts to edit.
    .DESCRIPTION
        -Selector "all" or a list of ids wins. Otherwise a single account is used
        directly; several accounts need -SteamAccount with -Yes, or an interactive
        choice (numbers separated by commas, or A for all).
    .PARAMETER Accounts
        Accounts from Get-RaSteamAccount.
    .PARAMETER Selector
        Value of -SteamAccount.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Select-RaSteamAccount @(Get-RaSteamAccount $steam) 'all'
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Accounts,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$Selector
    )
    if ($Accounts.Count -eq 0) { throw 'No Steam account with config\localconfig.vdf was found.' }
    if ($Selector) {
        if ($Selector -ieq $script:RaAllAccountsSelector) { return $Accounts }
        $ids = $Selector -split $script:RaListSeparatorPattern | Where-Object { $_ }
        $sel = @($Accounts | Where-Object { $ids -contains $_.Id })
        if ($sel.Count -ne @($ids).Count) { throw "Unknown Steam account id in -SteamAccount '$Selector'. Known: $(Format-RaAccountList $Accounts)" }
        return $sel
    }
    if ($Accounts.Count -eq 1) { return $Accounts }
    if ($script:RaAssumeYes) { throw "Several Steam accounts found; pass -SteamAccount <id>|all. Known: $(Format-RaAccountList $Accounts)" }
    for ($i = 0; $i -lt $Accounts.Count; $i++) {
        $n = if ($Accounts[$i].Name) { " ($($Accounts[$i].Name))" } else { '' }
        Write-RaHost ('  {0} = {1}{2}' -f ($i + 1), $Accounts[$i].Id, $n)
    }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskAccount')
        if ($a -ieq $script:RaAllAnswer) { return $Accounts }
        $nums = @($a -split $script:RaListSeparatorPattern | Where-Object { $_ -match $script:RaDigitsPattern } | ForEach-Object { [int]$_ })
        if ($nums.Count -gt 0 -and -not ($nums | Where-Object { $_ -lt 1 -or $_ -gt $Accounts.Count })) {
            return @($nums | Sort-Object -Unique | ForEach-Object { $Accounts[$_ - 1] })
        }
    }
}

function Get-RaSteamLibrary {
    <#
    .SYNOPSIS
        Lists Steam library folders (the Steam folder first).
    .DESCRIPTION
        Reads steamapps\libraryfolders.vdf in both the current (block with "path") and
        the legacy ("1" "D:\Lib") format. An unreadable file is logged and ignored.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Get-RaSteamLibrary $steam
    #>
    [CmdletBinding()]
    [OutputType([string[]], [object[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $libs = @($SteamRootPath)
    $lf = Join-Path $SteamRootPath $script:RaSteamLibraryFoldersRelPath
    if (Test-Path -LiteralPath $lf) {
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $lf).Text
            foreach ($top in $root.Children) {
                foreach ($entry in $top.Children) {
                    if ($entry.IsBlock) {
                        $p = @(Find-RaVdfChild $entry $script:RaLibraryPathKey)
                        if ($p.Count -eq 1) { $libs += $p[0].Value }
                    } elseif ($entry.Key -match $script:RaDigitsPattern) { $libs += $entry.Value } # legacy format
                }
            }
        } catch { Write-RaLog "libraryfolders.vdf unreadable: $($_.Exception.Message)" -Level WARN -NoConsole }
    }
    return @($libs | Select-Object -Unique)
}

function Get-RaGameDir {
    <#
    .SYNOPSIS
        Finds the HOI4 installation folder through the Steam libraries.
    .DESCRIPTION
        Reads appmanifest_394360.acf in each library and returns the first
        steamapps\common\<installdir> that contains hoi4.exe, or $null.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaGameDir $steam
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    foreach ($lib in (Get-RaSteamLibrary $SteamRootPath)) {
        $acf = Join-Path $lib ($script:RaSteamAppManifestRelPathFormat -f $script:RaAppId)
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $acf).Text
            $dir = @(Find-RaVdfPath $root $script:RaAppManifestInstallDirPath)
            if ($dir.Count -ne 1) { continue }
            $game = Join-Path $lib (Join-Path $script:RaSteamCommonRelPath $dir[0].Value)
            if (Test-Path -LiteralPath (Join-Path $game $script:RaGameExeName) -PathType Leaf) { return $game }
        } catch { Write-Verbose "appmanifest unreadable in ${lib}: $($_.Exception.Message)" }
    }
    return $null
}

function Get-RaGameSha256 {
    <#
    .SYNOPSIS
        SHA-256 of hoi4.exe (read-only), or $null when the game folder is unknown.
    .DESCRIPTION
        Used by -CheckUpdate to report whether a release supports the installed game build.
    .PARAMETER GameDir
        Game folder from Get-RaGameDir.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaGameSha256 (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$GameDir)
    if (-not $GameDir) { return $null }
    $exe = Join-Path $GameDir $script:RaGameExeName
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return $null }
    return (Get-RaFileSha256 $exe)
}
#endregion Shared/Steam.ps1

#region Shared/VersionStore.ps1
# ---------------------------------------------------------------------------
# Version store: verified install of one version, the 'current' junction and
# pruning. Shared by the install, update and rollback slices.
# ---------------------------------------------------------------------------

function Get-RaPackageZip {
    <#
    .SYNOPSIS
        Returns the path of the release ZIP to verify, downloading it when needed.
    .DESCRIPTION
        A local -PackagePath wins. Otherwise -ZipUrl (from the signed manifest) or the
        default GitHub release URL is downloaded into the root folder. The result tells
        the caller whether the file must be deleted afterwards.
    .PARAMETER Manifest
        Manifest of the version being installed.
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .PARAMETER ZipUrl
        Asset URL from the update channel or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Path, Downloaded)
    .EXAMPLE
        Get-RaPackageZip $m '' $null
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$ZipUrl
    )
    if ($LocalZip) {
        return [pscustomobject]@{ Path = (Resolve-Path -LiteralPath $LocalZip).ProviderPath; Downloaded = $false }
    }
    if (-not $ZipUrl) {
        if ($script:RaGitHubRepo -match $script:RaRepoPlaceholderMarker) {
            throw 'Release URL is still a placeholder; use -PackagePath with a downloaded release ZIP.'
        }
        $ZipUrl = ($script:RaGitHubDownloadPrefixFormat -f $script:RaGitHubRepo) + "$($script:RaReleaseTagPrefix)$($Manifest.Version)/$($Manifest.ZipName)"
    }
    Write-RaLog "Downloading $ZipUrl"
    $downloaded = Join-Path $script:RaRoot ($script:RaDownloadPrefix + (New-RaUniqueName) + '.zip')
    [IO.File]::WriteAllBytes($downloaded, (Get-RaHttpContent $ZipUrl))
    return [pscustomobject]@{ Path = $downloaded; Downloaded = $true }
}

function Install-RaVersion {
    <#
    .SYNOPSIS
        Installs one verified version into <root>\<version>.
    .DESCRIPTION
        Skips work when the folder already verifies. Otherwise obtains the ZIP, checks
        its SHA-256 before extraction, extracts into a staging folder with per-file
        checks, re-verifies the staging folder and only then moves it into place (a
        broken existing folder is moved aside). Stores the manifest for later rollback.
    .PARAMETER Manifest
        Manifest of the version.
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .PARAMETER ZipUrl
        Asset URL from the update channel or empty.
    .OUTPUTS
        System.String (the version folder)
    .EXAMPLE
        Install-RaVersion $m $PackagePath $null
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$ZipUrl
    )
    Test-RaManifest $Manifest
    $dir = Get-RaVersionDir $Manifest.Version
    if (Test-RaInstalledVersion $Manifest $dir) {
        Write-RaLog "Version $($Manifest.Version) already present and verified: $dir"
        return $dir
    }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would verify $($Manifest.ZipName) (sha256 $($Manifest.ZipSha256)) and install to $dir" -Level PLAN
        return $dir
    }
    Initialize-RaRoot
    $package = Get-RaPackageZip -Manifest $Manifest -LocalZip $LocalZip -ZipUrl $ZipUrl
    $staging = Join-Path $script:RaRoot ($script:RaStagingPrefix + (New-RaUniqueName))
    try {
        $hash = Get-RaFileSha256 $package.Path
        if ($hash -ne $Manifest.ZipSha256.ToUpperInvariant()) {
            throw "Package SHA256 mismatch: expected $($Manifest.ZipSha256), got $hash. Nothing was installed."
        }
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-RaVerifiedZip -Manifest $Manifest -ZipPath $package.Path -Destination $staging
        if (-not (Test-RaInstalledVersion $Manifest $staging)) { throw 'Staged files failed verification.' }
        if (Test-Path -LiteralPath $dir) {
            $broken = "$dir$($script:RaBrokenInfix)$((Get-RaNow).ToString($script:RaBrokenTimestampFormat))"
            Write-RaLog "Existing $dir failed verification; moved to $broken" -Level WARN
            Move-Item -LiteralPath $dir -Destination $broken
        }
        Move-Item -LiteralPath $staging -Destination $dir
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
        if ($package.Downloaded -and (Test-Path -LiteralPath $package.Path)) { Remove-Item -LiteralPath $package.Path -Force }
    }
    Save-RaStoredManifest $Manifest
    Write-RaLog "Installed version $($Manifest.Version) to $dir"
    return $dir
}

function Test-RaReparsePoint {
    <#
    .SYNOPSIS
        Tests whether a file system item is a reparse point (junction or symlink).
    .DESCRIPTION
        Used so that junctions are only ever unlinked, never recursed into.
    .PARAMETER Item
        File or directory info.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaReparsePoint (Get-Item -LiteralPath $p -Force)
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][System.IO.FileSystemInfo]$Item)
    return [bool]($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)
}

function Get-RaJunctionTarget {
    <#
    .SYNOPSIS
        Returns the full target path of a junction, or $null.
    .DESCRIPTION
        $null when the path does not exist or is not a reparse point.
    .PARAMETER Path
        Junction path.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaJunctionTarget (Get-RaCurrentLink)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item -or -not (Test-RaReparsePoint $item)) { return $null }
    $t = @($item.Target) | Select-Object -First 1
    if ($t) { return [IO.Path]::GetFullPath(($t -replace '^\\\\\?\\', '')) }
    return $null
}

function Remove-RaJunction {
    <#
    .SYNOPSIS
        Deletes a junction (the link only, never the target's content).
    .DESCRIPTION
        Refuses to delete anything that is not a reparse point. A missing path is a no-op.
    .PARAMETER Path
        Junction path.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaJunction (Get-RaCurrentLink)
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return }
    if (-not (Test-RaReparsePoint $item)) { throw "$Path is not a junction; refusing to delete it." }
    [IO.Directory]::Delete($Path, $false)
}

function Switch-RaCurrent {
    <#
    .SYNOPSIS
        Points <root>\current at <root>\<version>.
    .DESCRIPTION
        Creates the new junction under a temporary name first, then replaces the old one;
        on failure the previous link is restored. Verifies the result.
    .PARAMETER Version
        Version to activate (folder must exist).
    .OUTPUTS
        None.
    .EXAMPLE
        Switch-RaCurrent '0.2.0'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Version)
    $link = Get-RaCurrentLink
    $target = Get-RaVersionDir $Version
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would point $link -> $target" -Level PLAN
        return
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Version folder missing: $target" }
    $old = Get-RaJunctionTarget $link
    $tmp = "$link$($script:RaJunctionTempInfix)" + (New-RaUniqueName)
    New-Item -ItemType Junction -Path $tmp -Target $target | Out-Null
    try {
        if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
        [IO.Directory]::Move($tmp, $link)
        if ((Get-RaJunctionTarget $link) -ne [IO.Path]::GetFullPath($target)) { throw 'current junction verification failed.' }
        Write-RaLog "Active version: $Version ($link)"
    } catch {
        $err = $_.Exception.Message
        if (Test-Path -LiteralPath $tmp) { [IO.Directory]::Delete($tmp, $false) }
        if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
        if ($old) { New-Item -ItemType Junction -Path $link -Target $old | Out-Null }
        throw "Could not switch the current version: $err"
    }
}

function Set-RaActiveVersion {
    <#
    .SYNOPSIS
        Activates a version and records the previous one for rollback.
    .DESCRIPTION
        No-op when the version is already active and the junction points at it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER Version
        Version to activate.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaActiveVersion $state $m.Version
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Version
    )
    $link = Get-RaCurrentLink
    $oldTarget = Get-RaJunctionTarget $link
    $already = ($State.InstalledVersion -eq $Version) -and ($oldTarget -eq [IO.Path]::GetFullPath((Get-RaVersionDir $Version)))
    if ($already) { return }
    $oldProxy = $State.Proxy
    if ($oldProxy) { Install-RaProxy -State $State -GameDir (Split-Path -Parent $oldProxy.Path) -Version $Version }
    try { Switch-RaCurrent $Version } catch {
        if (-not $script:RaDry -and (Get-RaJunctionTarget $link) -ne $oldTarget) {
            if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
            if ($oldTarget) { New-Item -ItemType Junction -Path $link -Target $oldTarget | Out-Null }
        }
        if ($oldProxy) { Install-RaProxy -State $State -GameDir (Split-Path -Parent $oldProxy.Path) -Version $oldProxy.Version }
        throw
    }
    if ($State.InstalledVersion -and $State.InstalledVersion -ne $Version) { $State.PreviousVersion = $State.InstalledVersion }
    $State.InstalledVersion = $Version
}

function Remove-RaUnusedVersion {
    <#
    .SYNOPSIS
        Deletes installed versions other than the active and the previous one.
    .DESCRIPTION
        Never fails the run: a folder in use is logged and kept. Junctions are skipped.
        The stored manifest of a removed version is deleted too.
    .PARAMETER State
        State object (read only).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaUnusedVersion $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $keep = @($State.InstalledVersion, $State.PreviousVersion) | Where-Object { $_ }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    foreach ($d in Get-ChildItem -LiteralPath $script:RaRoot -Directory -Force) {
        if (Test-RaReparsePoint $d) { continue }
        if ($d.Name -match $script:RaVersionPattern -and $keep -notcontains $d.Name) {
            if ($script:RaDry) {
                Write-RaLog "[dry-run] would remove old version $($d.FullName)" -Level PLAN
                continue
            }
            try {
                Remove-Item -LiteralPath $d.FullName -Recurse -Force
                $mf = Get-RaManifestStorePath $d.Name
                if (Test-Path -LiteralPath $mf) { Remove-Item -LiteralPath $mf -Force }
                Write-RaLog "Removed old version $($d.FullName)"
            } catch { Write-RaLog "Could not remove $($d.FullName) (in use?): $($_.Exception.Message)" -Level WARN }
        }
    }
}
#endregion Shared/VersionStore.ps1

#region Features/Proxy.ps1
# ---------------------------------------------------------------------------
# Proxy installation. No foreign file is overwritten, renamed or removed.
# ---------------------------------------------------------------------------

function Get-RaProxyPath {
    <#
    .SYNOPSIS
        Resolves the proxy destination in a verified game directory.
    .DESCRIPTION
        Requires a normal directory containing hoi4.exe; refuses reparse points at
        the directory, executable or destination. Never creates a game directory.
    .PARAMETER GameDir
        Game directory discovered through Steam.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaProxyPath (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$GameDir)
    $dir = Get-Item -LiteralPath $GameDir -Force -ErrorAction Stop
    $exe = Get-Item -LiteralPath (Join-Path $dir.FullName $script:RaGameExeName) -Force -ErrorAction Stop
    if (-not $dir.PSIsContainer -or $exe.PSIsContainer -or (Test-RaReparsePoint $dir) -or (Test-RaReparsePoint $exe)) {
        throw 'Proxy destination must be a normal HOI4 directory containing a normal hoi4.exe.'
    }
    $path = Join-Path $dir.FullName $script:RaProxyFileName
    $existing = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if ($existing -and ($existing.PSIsContainer -or (Test-RaReparsePoint $existing))) {
        throw "Proxy destination is a directory or link; left untouched: $path"
    }
    return $path
}

function Test-RaOwnProxy {
    <#
    .SYNOPSIS
        Verifies an installed proxy against its ownership record and release manifest.
    .DESCRIPTION
        Both the recorded path and hash must match; the hash must also occur in the
        stored verified release manifest. Missing, changed or linked files fail closed.
    .PARAMETER State
        Installer ownership state.
    .PARAMETER Path
        Expected game proxy path.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaOwnProxy $state $path
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][string]$Path)
    if (-not $State.Proxy) { return $false }
    try {
        if ([IO.Path]::GetFullPath($State.Proxy.Path) -ine [IO.Path]::GetFullPath($Path)) { return $false }
        if ([IO.Path]::GetFileName($Path) -ine $script:RaProxyFileName) { return $false }
        if (-not (Test-RaSha256Text $State.Proxy.Sha256)) { return $false }
        $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
        if ($item.PSIsContainer -or (Test-RaReparsePoint $item)) { return $false }
        $manifest = Read-RaStoredManifest $State.Proxy.Version
        return ($manifest -and $manifest.Files.Contains($script:RaProxyBundlePath) -and
            $manifest.Files[$script:RaProxyBundlePath] -ieq $State.Proxy.Sha256 -and
            (Get-RaFileSha256 $Path) -ieq $State.Proxy.Sha256)
    } catch { return $false }
}

function Remove-RaOwnedFile {
    <#
    .SYNOPSIS
        Quarantines and re-verifies an owned file before deleting it.
    .DESCRIPTION
        Moving within its directory makes a racing replacement observable. Only the
        random quarantine path with the expected hash is deleted. A different file
        is restored, or preserved under the reported recovery path if restoration fails.
    .PARAMETER Path
        Recorded owned file path.
    .PARAMETER Sha256
        Hash established by the ownership record.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaOwnedFile -Path $state.Proxy.Path -Sha256 $state.Proxy.Sha256
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Sha256
    )
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or (Test-RaReparsePoint $item) -or (Get-RaFileSha256 $Path) -ine $Sha256) {
        throw "Owned file changed before removal; left untouched: $Path"
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned file $Path" -Level PLAN; return }
    $quarantine = $Path + '.' + (New-RaUniqueName) + '.remove'
    if ($script:RaBeforeOwnedRemove) { & $script:RaBeforeOwnedRemove $Path }
    try {
        [IO.File]::Move($Path, $quarantine)
        $moved = Get-Item -LiteralPath $quarantine -Force
        if ($moved.PSIsContainer -or (Test-RaReparsePoint $moved) -or (Get-RaFileSha256 $quarantine) -ine $Sha256) {
            throw "File changed during removal; preserving it: $Path"
        }
        [IO.File]::Delete($quarantine)
    } catch {
        $errorMessage = $_.Exception.Message
        if ((Test-Path -LiteralPath $quarantine) -and -not (Test-Path -LiteralPath $Path)) {
            try { [IO.File]::Move($quarantine, $Path) } catch {
                throw "Removal failed: $errorMessage. Recovery file preserved at $quarantine"
            }
        }
        if (Test-Path -LiteralPath $quarantine) { throw "Removal failed: $errorMessage. Recovery file preserved at $quarantine" }
        throw $errorMessage
    }
}

function Set-RaProxyActivation {
    <#
    .SYNOPSIS
        Enables the explicit config fallback or removes our previous fallback marker.
    .DESCRIPTION
        Flag mode is the default. Config mode writes only the documented ASCII marker.
        An existing marker is changed or deleted only when its saved hash still matches.
    .PARAMETER State
        Installer state, modified in memory.
    .PARAMETER Activation
        Flag or Config.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaProxyActivation $state Config
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][ValidateSet('Flag', 'Config')][string]$Activation)
    $path = Join-Path $script:RaRoot $script:RaProxyConfigName
    $exists = Test-Path -LiteralPath $path
    if ($exists) {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer -or (Test-RaReparsePoint $item) -or -not $State.ProxyConfigSha256 -or
            (Get-RaFileSha256 $path) -ine $State.ProxyConfigSha256) { throw "Foreign or modified activation config left untouched: $path" }
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] proxy activation: $Activation ($path)" -Level PLAN; return }
    if ($Activation -eq 'Config') {
        Initialize-RaRoot
        $bytes = [Text.Encoding]::ASCII.GetBytes($script:RaProxyConfigContent)
        if (-not $exists) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
        }
        $State.ProxyConfigSha256 = Get-RaFileSha256 $path
    } else {
        if ($exists) { Remove-RaOwnedFile -Path $path -Sha256 $State.ProxyConfigSha256 }
        $State.ProxyConfigSha256 = $null
    }
    $State.ProxyActivation = $Activation
}

function Install-RaProxy {
    <#
    .SYNOPSIS
        Installs or updates the proxy from a fully verified release.
    .DESCRIPTION
        Uses a new temporary file and an atomic replace for a previously owned file.
        New destinations use File.Move, which cannot overwrite a racing foreign file.
        The previous proxy is restored if installation or verification fails.
    .PARAMETER State
        Installer state, modified in memory.
    .PARAMETER GameDir
        Verified HOI4 installation directory.
    .PARAMETER Version
        Installed release to use (defaults to the state's active version).
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaProxy $state $gameDir
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$GameDir,
        [Parameter(Position = 2)][AllowNull()][AllowEmptyString()][string]$Version
    )
    $path = Get-RaProxyPath $GameDir
    if (-not $Version) { $Version = $State.InstalledVersion }
    $manifest = Read-RaStoredManifest $Version
    $exists = Test-Path -LiteralPath $path
    if ($exists -and -not (Test-RaOwnProxy $State $path)) {
        throw "Foreign or modified version.dll left untouched: $path. Choose -LoadMethod Launcher or resolve the conflict manually."
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] would install verified proxy $path from $Version" -Level PLAN; return }
    if (-not $manifest -or -not $manifest.Files.Contains($script:RaProxyBundlePath)) {
        throw 'Release does not contain the signed proxy/version.dll asset. Use -LoadMethod Launcher for an older release.'
    }
    $sourceDir = Get-RaVersionDir $Version
    if (-not (Test-RaInstalledVersion $manifest $sourceDir)) { throw 'Release bundle failed verification; proxy was not installed.' }
    if ($State.Proxy -and $State.Proxy.Path -ine $path) {
        throw 'A proxy is already recorded in another game directory. Uninstall it before choosing another directory.'
    }
    $sha = $manifest.Files[$script:RaProxyBundlePath].ToUpperInvariant()
    if ($exists -and (Get-RaFileSha256 $path) -eq $sha) { $State.Proxy = [pscustomobject]@{ Path = $path; Sha256 = $sha; Version = $Version }; return }
    $tmp = $path + '.' + (New-RaUniqueName) + '.tmp'
    $backup = $path + '.' + (New-RaUniqueName) + '.bak'
    $installed = $false
    $success = $false
    $oldRecord = $State.Proxy
    try {
        [IO.File]::Copy((Join-Path $sourceDir ($script:RaProxyBundlePath -replace '/', '\')), $tmp, $false)
        if ((Get-RaFileSha256 $tmp) -ne $sha) { throw 'Staged proxy hash mismatch.' }
        if ($exists) {
            if (-not (Test-RaOwnProxy $State $path)) { throw 'Proxy changed during installation; left untouched.' }
            [IO.File]::Replace($tmp, $path, $backup)
        } else { [IO.File]::Move($tmp, $path) }
        $installed = $true
        if ($exists -and (Get-RaFileSha256 $backup) -ine $oldRecord.Sha256) { throw 'Proxy changed during replacement; restoring the preserved file.' }
        if ((Get-RaFileSha256 $path) -ne $sha) { throw 'Installed proxy hash mismatch.' }
        $State.Proxy = [pscustomobject]@{ Path = $path; Sha256 = $sha; Version = $Version }
        Write-RaLog "Installed CAPIE proxy: $path ($sha)"
        $success = $true
    } catch {
        $State.Proxy = $oldRecord
        if ($installed -and (Test-Path -LiteralPath $backup)) { [IO.File]::Replace($backup, $path, $null) }
        elseif ($installed -and (Test-Path -LiteralPath $path) -and (Get-RaFileSha256 $path) -eq $sha) { [IO.File]::Delete($path) }
        throw
    } finally {
        if (Test-Path -LiteralPath $tmp) { [IO.File]::Delete($tmp) }
        # A failed restoration must retain its recovery copy.
        if ($success -and (Test-Path -LiteralPath $backup)) { [IO.File]::Delete($backup) }
    }
}

function Remove-RaProxy {
    <#
    .SYNOPSIS
        Removes only a hash-proven owned proxy and its activation marker.
    .DESCRIPTION
        Modified DLLs remain in place. A locked owned DLL causes failure before bundle
        removal, preserving the state needed to retry after closing the game.
    .PARAMETER State
        Installer state, modified in memory.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaProxy $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if ($State.Proxy) {
        $path = $State.Proxy.Path
        if (Test-Path -LiteralPath $path) {
            if (Test-RaOwnProxy $State $path) {
                [void](Get-RaProxyPath (Split-Path -Parent $path))
                if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned proxy $path" -Level PLAN }
                else { Remove-RaOwnedFile -Path $path -Sha256 $State.Proxy.Sha256; Write-RaLog "Removed owned proxy $path" }
            } else { Write-RaLog "Foreign or modified proxy left untouched: $path" -Level WARN }
        }
        if (-not $script:RaDry) { $State.Proxy = $null }
    }
    $config = Join-Path $script:RaRoot $script:RaProxyConfigName
    if (Test-Path -LiteralPath $config) {
        $item = Get-Item -LiteralPath $config -Force
        if (-not $item.PSIsContainer -and -not (Test-RaReparsePoint $item) -and $State.ProxyConfigSha256 -and
            (Get-RaFileSha256 $config) -ieq $State.ProxyConfigSha256) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned activation marker $config" -Level PLAN }
            else { Remove-RaOwnedFile -Path $config -Sha256 $State.ProxyConfigSha256; $State.ProxyConfigSha256 = $null }
        } else { throw "Foreign or modified activation config left untouched: $config. Move it out before uninstalling the bundle." }
    }
}
#endregion Features/Proxy.ps1

#region Features/SteamLaunchOption.ps1
# ---------------------------------------------------------------------------
# Slice: Steam launch option.
#   1. compose/remove our wrapper in a LaunchOptions string (pure),
#   2. edit the LaunchOptions value inside localconfig.vdf text (pure),
#   3. update the file with backup, byte-exact verification and restore,
#   4. apply to the selected accounts and record what was written in state.json.
# ---------------------------------------------------------------------------

function Get-RaWindowsArgument {
    <#
    .SYNOPSIS
        Parses Windows arguments while retaining each argument's source span.
    .DESCRIPTION
        Follows CommandLineToArgvW quote and backslash rules for arguments following
        the executable. Unmatched quotes consume the remaining text. Spaces and tabs
        delimit arguments only outside quotes. Source spans permit minimal removal.
    .PARAMETER Value
        Argument tail without the executable name.
    .OUTPUTS
        System.Object[] (Value, Start, Length)
    .EXAMPLE
        Get-RaWindowsArgument '-ca"pie" -debug'
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    $text = [string]$Value
    $index = 0
    while ($index -lt $text.Length) {
        while ($index -lt $text.Length -and ($text[$index] -eq ' ' -or $text[$index] -eq "`t")) { $index++ }
        if ($index -ge $text.Length) { break }
        $start = $index
        $quoteCount = 0
        $argument = New-Object Text.StringBuilder
        while ($index -lt $text.Length) {
            if ($quoteCount -eq 0 -and ($text[$index] -eq ' ' -or $text[$index] -eq "`t")) { break }
            $escapedQuote = $false
            if ($text[$index] -eq '\') {
                $slashStart = $index
                while ($index -lt $text.Length -and $text[$index] -eq '\') { $index++ }
                $slashes = $index - $slashStart
                if ($index -lt $text.Length -and $text[$index] -eq '"') {
                    [void]$argument.Append(('\' * [int][Math]::Floor($slashes / 2)))
                    if ($slashes -band 1) { $escapedQuote = $true }
                } else { [void]$argument.Append(('\' * $slashes)); continue }
            }
            if ($text[$index] -eq '"') {
                if ($escapedQuote) { [void]$argument.Append('"') } else { $quoteCount++ }
                $index++
                while ($index -lt $text.Length -and $text[$index] -eq '"') {
                    $quoteCount++
                    if ($quoteCount -eq 3) { [void]$argument.Append('"'); $quoteCount = 0 }
                    $index++
                }
                if ($quoteCount -eq 2) { $quoteCount = 0 }
            } else { [void]$argument.Append($text[$index]); $index++ }
        }
        [pscustomobject]@{
            Value = $argument.ToString(); Start = $start; Length = $index - $start; UnterminatedQuote = $quoteCount -ne 0
        }
    }
}

function Test-RaHasCapieFlag {
    <#
    .SYNOPSIS
        Tests for a complete -capie argument outside other quoted arguments.
    .DESCRIPTION
        A quoted "-capie" is accepted. Substrings and text within another quoted
        argument do not enable CAPIE. Matching follows the native case-sensitive flag.
    .PARAMETER Value
        Launch options string.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaHasCapieFlag '-debug_mode -capie'
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    foreach ($token in Get-RaWindowsArgument $Value) {
        if ($token.Value -ceq $script:RaProxyFlag) { return $true }
    }
    return $false
}

function Remove-RaCapieFlag {
    <#
    .SYNOPSIS
        Removes only complete -capie arguments.
    .DESCRIPTION
        Preserves all other argument bytes; adjacent blanks around removed tokens
        are trimmed only at the ends. Quoted text containing the flag is preserved.
    .PARAMETER Value
        Launch options string.
    .OUTPUTS
        System.String
    .EXAMPLE
        Remove-RaCapieFlag '-debug_mode -capie'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    $result = [string]$Value
    $tokens = @(Get-RaWindowsArgument $result)
    for ($i = $tokens.Count - 1; $i -ge 0; $i--) {
        $token = $tokens[$i]
        if ($token.Value -ceq $script:RaProxyFlag) {
            $result = $result.Remove($token.Start, $token.Length)
        }
    }
    return $result.Trim()
}

function Add-RaCapieFlag {
    <#
    .SYNOPSIS
        Adds a single -capie argument without adding an external wrapper.
    .DESCRIPTION
        Removes the old Rename Anything wrapper during migration. Existing options
        keep their meaning. Multiple %command% tokens are rejected as ambiguous.
    .PARAMETER Value
        Launch options string.
    .OUTPUTS
        System.String
    .EXAMPLE
        Add-RaCapieFlag '-debug_mode'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    $result = Remove-RaWrapper $Value
    if ([regex]::Matches([string]$result, '(?i)%command%').Count -gt 1) { throw 'Existing launch options contain %command% more than once; refusing to guess.' }
    if (Test-RaHasCapieFlag $result) { return $result }
    if (@(Get-RaWindowsArgument $result | Where-Object { $_.UnterminatedQuote }).Count -gt 0) {
        throw 'Existing launch options have an unterminated quote; refusing to append -capie.'
    }
    return (([string]$result).TrimEnd() + ' ' + $script:RaProxyFlag).TrimStart()
}

function Test-RaHasWrapper {
    <#
    .SYNOPSIS
        Tests whether launch options contain our launcher (quoted or unquoted).
    .DESCRIPTION
        Matches any folder, so an older install path is recognized too.
    .PARAMETER Value
        LaunchOptions value; $null and '' contain nothing.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaHasWrapper '"C:\RA\RenameAnythingLauncher.exe" %command%'
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    if ([string]::IsNullOrEmpty($Value)) { return $false }
    return [regex]::IsMatch($Value, $script:RaWrapperPattern, 'IgnoreCase')
}

function Remove-RaWrapperFragment {
    <#
    .SYNOPSIS
        Deletes every occurrence of our launcher fragment from launch options.
    .DESCRIPTION
        Pure string operation; whitespace right after the fragment is removed with it.
    .PARAMETER Value
        LaunchOptions value.
    .OUTPUTS
        System.String
    .EXAMPLE
        Remove-RaWrapperFragment '"C:\RA\RenameAnythingLauncher.exe" %command% -x'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    if ($null -eq $Value) { return $null }
    return [regex]::Replace($Value, $script:RaWrapperPattern, '', 'IgnoreCase')
}

function Add-RaWrapper {
    <#
    .SYNOPSIS
        Returns launch options with our wrapper directly before %command%.
    .DESCRIPTION
        Idempotent: an existing fragment (any path) is replaced. Options without
        %command% keep their meaning ("<ours> %command% <options>"). A second
        %command% is refused, as are quotes in the launcher path.
    .PARAMETER Current
        Existing LaunchOptions value or $null when the key is missing.
    .PARAMETER LauncherPath
        Absolute path of RenameAnythingLauncher.exe.
    .OUTPUTS
        System.String
    .EXAMPLE
        Add-RaWrapper '-debug_mode' 'C:\RA\current\RenameAnythingLauncher.exe'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$LauncherPath
    )
    if ($LauncherPath.Contains('"')) { throw 'Launcher path must not contain quotes' }
    if ([IO.Path]::GetFileName($LauncherPath) -ne $script:RaLauncherExeName) { throw 'Unexpected launcher file name' }
    $v = if ($null -eq $Current) { '' } else { Remove-RaWrapperFragment $Current }
    $fragment = '"' + $LauncherPath + '"'
    $commandMatches = [regex]::Matches($v, [regex]::Escape($script:RaSteamCommandToken), 'IgnoreCase')
    if ($commandMatches.Count -gt 1) { throw 'Existing launch options contain %command% more than once; refusing to guess. Edit them manually.' }
    if ($commandMatches.Count -eq 1) {
        $idx = $commandMatches[0].Index
        return $v.Substring(0, $idx) + $fragment + ' ' + $v.Substring($idx)
    }
    $rest = $v.Trim()
    if ($rest -eq '') { return "$fragment $($script:RaSteamCommandToken)" }
    # Without %command% Steam appends the options to the game command; keep that meaning.
    return "$fragment $($script:RaSteamCommandToken) $rest"
}

function Remove-RaWrapper {
    <#
    .SYNOPSIS
        Removes only our fragment from launch options.
    .DESCRIPTION
        A "%command%" left alone at the start is collapsed, which is equivalent in Steam
        to the options without it. Options without our fragment are returned unchanged.
    .PARAMETER Current
        LaunchOptions value.
    .OUTPUTS
        System.String
    .EXAMPLE
        Remove-RaWrapper '"C:\RA\RenameAnythingLauncher.exe" %command% -debug_mode'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current)
    if (-not (Test-RaHasWrapper $Current)) { return $Current }
    $t = (Remove-RaWrapperFragment $Current).Trim()
    if ($t -ieq $script:RaSteamCommandToken) { return '' }
    if ($t -match '^(?i)%command%\s+(.*)$') {
        $rest = $Matches[1]
        if ($rest -notmatch '(?i)%command%') { return $rest.Trim() }
    }
    return $t
}

function Get-RaAppLaunchInfo {
    <#
    .SYNOPSIS
        Locates the HOI4 app block and its LaunchOptions in localconfig.vdf text.
    .DESCRIPTION
        Only UserLocalConfigStore/Software/Valve/Steam/apps counts (WebStorage and depots
        are decoys). Throws when the file is corrupt or ambiguous: no or several apps
        blocks, several 394360 blocks, 394360 not a block, several LaunchOptions keys,
        or LaunchOptions being a block.
    .PARAMETER Text
        localconfig.vdf text.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Apps, App, Option, Value)
    .EXAMPLE
        (Get-RaAppLaunchInfo $text).Value
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text)
    $root = ConvertFrom-RaVdf $Text
    $apps = @(Find-RaVdfPath $root $script:RaAppsPath | Where-Object { $_.IsBlock })
    if ($apps.Count -eq 0) {
        throw 'No UserLocalConfigStore/Software/Valve/Steam/apps block in localconfig.vdf (start the game once from Steam, or set the launch option manually).'
    }
    if ($apps.Count -gt 1) { throw 'Multiple apps blocks in localconfig.vdf; refusing to edit.' }
    $app = @(Find-RaVdfChild $apps[0] $script:RaAppId)
    if ($app.Count -gt 1) { throw "Multiple $($script:RaAppId) blocks; refusing to edit." }
    if ($app.Count -eq 1 -and -not $app[0].IsBlock) { throw "$($script:RaAppId) is not a block; refusing to edit." }
    $lo = @()
    if ($app.Count -eq 1) { $lo = @(Find-RaVdfChild $app[0] $script:RaLaunchOptionsKey) }
    if ($lo.Count -gt 1) { throw 'Multiple LaunchOptions keys; refusing to edit.' }
    if ($lo.Count -eq 1 -and $lo[0].IsBlock) { throw 'LaunchOptions is a block; refusing to edit.' }
    return [pscustomobject]@{
        Apps   = $apps[0]
        App    = if ($app.Count -eq 1) { $app[0] } else { $null }
        Option = if ($lo.Count -eq 1) { $lo[0] } else { $null }
        Value  = if ($lo.Count -eq 1) { $lo[0].Value } else { $null }
    }
}

function Get-RaLineIndentBefore {
    <#
    .SYNOPSIS
        Returns the start and indentation of the line that holds a closing brace.
    .DESCRIPTION
        Used to insert a key or block with matching indentation. Refuses a brace that
        is not alone on its line.
    .PARAMETER Text
        Whole file text.
    .PARAMETER Index
        Offset of the closing brace.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (LineStart, Indent)
    .EXAMPLE
        Get-RaLineIndentBefore $text $info.App.CloseIndex
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][int]$Index
    )
    $nl = if ($Index -gt 0) { $Text.LastIndexOf("`n", $Index - 1) } else { -1 }
    $lineStart = $nl + 1
    $prefix = $Text.Substring($lineStart, $Index - $lineStart)
    if ($prefix -notmatch '^[\t ]*$') { throw 'Closing brace is not on its own line; refusing to edit.' }
    return [pscustomobject]@{ LineStart = $lineStart; Indent = $prefix }
}

function Set-RaLaunchOptionsInText {
    <#
    .SYNOPSIS
        Returns localconfig.vdf text with a new LaunchOptions value (minimal edit).
    .DESCRIPTION
        Replaces only the value token when the key exists (ReplacedValue), otherwise
        inserts the key before the app block's closing brace (InsertedKey), or a whole
        394360 block before the apps block's closing brace (InsertedBlock). Pure function.
    .PARAMETER Text
        Original text.
    .PARAMETER NewValue
        New LaunchOptions value (unescaped).
    .PARAMETER Newline
        Line ending used for inserted lines.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Text, Action, OldValue)
    .EXAMPLE
        Set-RaLaunchOptionsInText $file.Text $newValue $file.Newline
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$NewValue,
        [Parameter(Position = 2)][ValidateSet("`n", "`r`n")][string]$Newline = "`n"
    )
    $info = Get-RaAppLaunchInfo $Text
    $escaped = '"' + (ConvertTo-RaVdfString $NewValue) + '"'
    $keyToken = '"' + $script:RaLaunchOptionsKey + '"' + "`t`t"
    if ($null -ne $info.Option) {
        $o = $info.Option
        $new = $Text.Substring(0, $o.ValueStart) + $escaped + $Text.Substring($o.ValueEnd)
        return [pscustomobject]@{ Text = $new; Action = 'ReplacedValue'; OldValue = $info.Value }
    }
    if ($null -ne $info.App) {
        $pos = Get-RaLineIndentBefore $Text $info.App.CloseIndex
        $line = $pos.Indent + "`t" + $keyToken + $escaped + $Newline
        $new = $Text.Insert($pos.LineStart, $line)
        return [pscustomobject]@{ Text = $new; Action = 'InsertedKey'; OldValue = $null }
    }
    $pos = Get-RaLineIndentBefore $Text $info.Apps.CloseIndex
    $ind = $pos.Indent + "`t"
    $block = @(
        $ind, '"', $script:RaAppId, '"', $Newline,
        $ind, '{', $Newline,
        $ind, "`t", $keyToken, $escaped, $Newline,
        $ind, '}', $Newline
    ) -join ''
    $new = $Text.Insert($pos.LineStart, $block)
    return [pscustomobject]@{ Text = $new; Action = 'InsertedBlock'; OldValue = $null }
}

function New-RaBackupPath {
    <#
    .SYNOPSIS
        Returns a not-yet-existing backup path next to a config file.
    .DESCRIPTION
        <file>.renameanything-backup-<yyyyMMdd-HHmmss>[-n], time from the clock adapter.
    .PARAMETER ConfigPath
        File to back up.
    .OUTPUTS
        System.String
    .EXAMPLE
        New-RaBackupPath $configPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$ConfigPath)
    $stamp = (Get-RaNow).ToString($script:RaBackupTimestampFormat)
    $backup = "$ConfigPath$($script:RaBackupInfix)$stamp"
    $n = 1
    while (Test-Path -LiteralPath $backup) {
        $backup = "$ConfigPath$($script:RaBackupInfix)$stamp-$n"
        $n++
    }
    return $backup
}

function New-RaUpdateResult {
    <#
    .SYNOPSIS
        Builds the result object of Update-RaLocalConfig.
    .DESCRIPTION
        One shape for every outcome (unchanged, dry-run, written).
    .PARAMETER Changed
        Whether the value changes (or would change in dry-run mode).
    .PARAMETER OldValue
        Previous value or $null.
    .PARAMETER NewValue
        Resulting value.
    .PARAMETER Backup
        Backup path or $null.
    .PARAMETER Action
        Unchanged, ReplacedValue, InsertedKey or InsertedBlock.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        New-RaUpdateResult -Changed $false -OldValue 'x' -NewValue 'x' -Backup $null -Action 'Unchanged'
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][bool]$Changed,
        [AllowNull()][object]$OldValue,
        [AllowNull()][object]$NewValue,
        [AllowNull()][object]$Backup,
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Action
    )
    return [pscustomobject]@{ Changed = $Changed; OldValue = $OldValue; NewValue = $NewValue; Backup = $Backup; Action = $Action }
}

function Update-RaLocalConfig {
    <#
    .SYNOPSIS
        Changes LaunchOptions in one localconfig.vdf with backup, verification and restore.
    .DESCRIPTION
        Reads the file strictly, asks -Compute for the new value, edits only the value
        bytes, then (unless dry-run) re-checks that Steam is closed, writes a verified
        byte-exact backup, writes through a temporary file, compares the result byte for
        byte and parses it again. Any failure restores the backup.
    .PARAMETER ConfigPath
        localconfig.vdf of one account.
    .PARAMETER Compute
        Scriptblock(oldValueOrNull) returning the new value, or $null for "no change".
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Changed, OldValue, NewValue, Backup, Action)
    .EXAMPLE
        Update-RaLocalConfig -ConfigPath $cfg -Compute { param($old) Add-RaWrapper $old $launcher }
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$ConfigPath,
        [Parameter(Mandatory)][scriptblock]$Compute
    )
    $file = Read-RaTextFile $ConfigPath
    $info = Get-RaAppLaunchInfo $file.Text
    $newValue = & $Compute $info.Value
    if ($null -eq $newValue -or ($null -ne $info.Value -and $newValue -ceq $info.Value)) {
        return (New-RaUpdateResult -Changed $false -OldValue $info.Value -NewValue $info.Value -Backup $null -Action 'Unchanged')
    }
    $edit = Set-RaLaunchOptionsInText -Text $file.Text -NewValue $newValue -Newline $file.Newline
    $check = Get-RaAppLaunchInfo $edit.Text
    if ($check.Value -cne $newValue) { throw 'Internal check failed: edited text does not contain the expected value.' }
    $expected = ConvertTo-RaFileContent $edit.Text $file.HasBom
    if ($script:RaDry) {
        Write-RaLog ("[dry-run] {0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action) -Level PLAN
        return (New-RaUpdateResult -Changed $true -OldValue $info.Value -NewValue $newValue -Backup $null -Action $edit.Action)
    }
    if (Test-RaSteamRunning) { throw 'Steam started while editing; aborting before write.' }
    $backup = New-RaBackupPath $ConfigPath
    & $script:RaWriteAllBytes $backup $file.Bytes
    if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $backup)) $file.Bytes)) { throw "Backup verification failed: $backup" }
    Write-RaLog "Backup: $backup (sha256 $(Get-RaSha256OfData $file.Bytes))"
    $tmp = "$ConfigPath$($script:RaWriteTempSuffix)"
    try {
        & $script:RaWriteAllBytes $tmp $expected
        & $script:RaCopyFile $tmp $ConfigPath
        Remove-Item -LiteralPath $tmp -Force
        if ($script:RaAfterWriteHook) { & $script:RaAfterWriteHook $ConfigPath }
        $after = [byte[]](& $script:RaReadAllBytes $ConfigPath)
        if (-not (Test-RaBytesEqual $after $expected)) { throw 'Post-write verification failed: file content differs from expected.' }
        $verify = Get-RaAppLaunchInfo (Read-RaTextFile $ConfigPath).Text
        if ($verify.Value -cne $newValue) { throw 'Post-write verification failed: LaunchOptions value differs.' }
    } catch {
        $err = $_.Exception.Message
        try {
            if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
            & $script:RaCopyFile $backup $ConfigPath
            $restored = Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $ConfigPath)) $file.Bytes
        } catch { $restored = $false }
        if ($restored) { throw "$err Original file restored from backup." }
        throw "$err RESTORE FAILED - copy the backup back manually: $backup"
    }
    Write-RaLog ("{0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action)
    return (New-RaUpdateResult -Changed $true -OldValue $info.Value -NewValue $newValue -Backup $backup -Action $edit.Action)
}

function Find-RaSteamEntry {
    <#
    .SYNOPSIS
        Returns the state.json records for one localconfig.vdf.
    .DESCRIPTION
        Always an array; normally zero or one element.
    .PARAMETER State
        State object.
    .PARAMETER ConfigPath
        localconfig.vdf path.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaSteamEntry $state $acc.ConfigPath
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][string]$ConfigPath
    )
    return @($State.SteamEntries | Where-Object { $_.ConfigPath -eq $ConfigPath })
}

function Remove-RaSteamEntry {
    <#
    .SYNOPSIS
        Drops the state.json record of one localconfig.vdf (in memory).
    .DESCRIPTION
        The caller saves the state.
    .PARAMETER State
        State object (modified).
    .PARAMETER ConfigPath
        localconfig.vdf path.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaSteamEntry $state $acc.ConfigPath
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][string]$ConfigPath
    )
    $State.SteamEntries = @($State.SteamEntries | Where-Object { $_.ConfigPath -ne $ConfigPath })
}

function Set-RaSteamLaunchOption {
    <#
    .SYNOPSIS
        Adds our wrapper to the HOI4 launch options of the selected Steam accounts.
    .DESCRIPTION
        Requires Steam to be closed (asks, or fails with -Yes). Records per account the
        original value (the first one we ever saw) and what was written, so that undo can
        restore the exact original. Warns when another wrapper precedes ours.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder.
    .PARAMETER AccountSelector
        Value of -SteamAccount.
    .PARAMETER LauncherPath
        Stable launcher path (current junction).
    .PARAMETER LoadMethod
        Proxy appends -capie; Launcher uses the existing external wrapper.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaSteamLaunchOption $state $steam 'all' (Get-RaLauncherPath)
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$SteamRootPath,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$AccountSelector,
        [Parameter(Mandatory, Position = 3)][ValidateNotNullOrEmpty()][string]$LauncherPath,
        [ValidateSet('Proxy', 'Launcher')][string]$LoadMethod = 'Launcher'
    )
    $accounts = @(Select-RaSteamAccount @(Get-RaSteamAccount $SteamRootPath) $AccountSelector)
    if (-not (Wait-RaSteamClosed)) { throw 'Steam is running; Steam launch option was not changed.' }
    # Compute runs inside Update-RaLocalConfig and sees this scope dynamically; the name is unique on purpose.
    $raComputeLauncher = $LauncherPath
    $raComputeMethod = $LoadMethod
    $compute = {
        param($old)
        if ($raComputeMethod -eq 'Proxy') { Add-RaCapieFlag $old }
        else { Add-RaWrapper (Remove-RaCapieFlag $old) $raComputeLauncher }
    }
    foreach ($acc in $accounts) {
        $r = Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute $compute
        $existing = @(Find-RaSteamEntry $State $acc.ConfigPath)
        $original = if ($existing.Count -gt 0) { $existing[0].OriginalValue } else { $r.OldValue }
        if ($existing.Count -eq 0 -and (Test-RaHasWrapper $r.OldValue)) { $original = Remove-RaWrapper $r.OldValue }
        Remove-RaSteamEntry $State $acc.ConfigPath
        $record = [pscustomobject]@{
            AccountId     = $acc.Id
            ConfigPath    = $acc.ConfigPath
            OriginalValue = $original
            WrittenValue  = $r.NewValue
            LauncherPath  = $LauncherPath
            LoadMethod    = $LoadMethod
        }
        $State.SteamEntries = @($State.SteamEntries) + @($record)
        if ($LoadMethod -eq 'Launcher') {
            $other = ((Remove-RaWrapperFragment $r.NewValue) -replace '(?i)\s*%command%.*$', '').Trim()
            if ($other) { Write-RaLog "Note: another wrapper precedes ours in account $($acc.Id): '$other'. Compatibility is unknown." -Level WARN }
        }
    }
}

function Get-RaUndoValue {
    <#
    .SYNOPSIS
        Computes the launch options after removing our wrapper.
    .DESCRIPTION
        Restores the exact original only when the value is still what we wrote;
        otherwise (the user edited it since) removes just our fragment.
    .PARAMETER Current
        Current LaunchOptions value.
    .PARAMETER Entry
        state.json record for this file, or $null.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaUndoValue $old $entry
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current,
        [Parameter(Position = 1)][AllowNull()][psobject]$Entry
    )
    if ($null -ne $Entry -and $Current -ceq $Entry.WrittenValue) {
        if ($null -eq $Entry.OriginalValue) { return '' }
        return [string]$Entry.OriginalValue
    }
    if ($Entry -and $Entry.PSObject.Properties['LoadMethod'] -and $Entry.LoadMethod -eq 'Proxy') {
        # A pre-existing user flag was never ours to remove.
        if (Test-RaHasCapieFlag $Entry.OriginalValue) { return $Current }
        return (Remove-RaCapieFlag $Current)
    }
    return (Remove-RaCapieFlag (Remove-RaWrapper $Current))
}

function Remove-RaSteamLaunchOption {
    <#
    .SYNOPSIS
        Removes our wrapper from every Steam account that has it.
    .DESCRIPTION
        Accounts without our fragment are not touched (no backup either). Unreadable
        accounts are logged and skipped. Returns $false when Steam is running and the
        user did not close it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Remove-RaSteamLaunchOption $state $steam)) { 'Steam still running' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$SteamRootPath
    )
    $accounts = @(Get-RaSteamAccount $SteamRootPath)
    $targets = @()
    foreach ($acc in $accounts) {
        try {
            $info = Get-RaAppLaunchInfo (Read-RaTextFile $acc.ConfigPath).Text
            if ((Test-RaHasWrapper $info.Value) -or (Test-RaHasCapieFlag $info.Value)) { $targets += $acc }
        } catch { Write-RaLog "Account $($acc.Id): cannot read localconfig.vdf ($($_.Exception.Message)); skipped." -Level WARN }
    }
    if ($targets.Count -eq 0) {
        Write-RaLog 'No Steam launch option with Rename Anything found.'
        $State.SteamEntries = @()
        return $true
    }
    if (-not (Wait-RaSteamClosed)) { return $false }
    foreach ($acc in $targets) {
        # Compute runs inside Update-RaLocalConfig and sees this scope dynamically; the name is unique on purpose.
        $raComputeEntry = @(Find-RaSteamEntry $State $acc.ConfigPath) | Select-Object -First 1
        $compute = { param($old) Get-RaUndoValue $old $raComputeEntry }
        [void](Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute $compute)
        Remove-RaSteamEntry $State $acc.ConfigPath
    }
    return $true
}
#endregion Features/SteamLaunchOption.ps1

#region Features/Install.ps1
# ---------------------------------------------------------------------------
# Slice: install (-Install) of the release pinned in this script.
# ---------------------------------------------------------------------------

function Install-RaPackage {
    <#
    .SYNOPSIS
        Installs and activates the release pinned in this script.
    .DESCRIPTION
        Refuses while the pinned manifest is a placeholder. A newer version that is
        already installed and verifies is kept (and re-activated) instead of being
        downgraded.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaPackage $state $PackagePath
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip
    )
    $m = $script:RaPinnedRelease
    Test-RaManifest $m
    if ($State.InstalledVersion -and (Compare-RaVersion $State.InstalledVersion $m.Version) -gt 0) {
        $cur = Read-RaStoredManifest $State.InstalledVersion
        if ($cur -and (Test-RaInstalledVersion $cur (Get-RaVersionDir $State.InstalledVersion))) {
            Write-RaLog "A newer version ($($State.InstalledVersion)) is already installed; keeping it."
            Set-RaActiveVersion $State $State.InstalledVersion
            return
        }
    }
    [void](Install-RaVersion -Manifest $m -LocalZip $LocalZip -ZipUrl $null)
    Set-RaActiveVersion $State $m.Version
}
#endregion Features/Install.ps1

#region Features/Update.ps1
# ---------------------------------------------------------------------------
# Slice: update channel (-CheckUpdate, -Update) and -Rollback.
# Network access happens only here and in the ZIP download, and only on request.
# ---------------------------------------------------------------------------

function ConvertFrom-RaUtf8 {
    <#
    .SYNOPSIS
        Decodes UTF-8 bytes, optionally rejecting invalid sequences.
    .DESCRIPTION
        -Strict throws on invalid UTF-8 (used for signed manifests).
    .PARAMETER Bytes
        Data to decode.
    .PARAMETER Strict
        Throw on invalid bytes instead of substituting.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertFrom-RaUtf8 $bytes -Strict
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$Bytes,
        [switch]$Strict
    )
    return (New-Object System.Text.UTF8Encoding($false, [bool]$Strict)).GetString($Bytes)
}

function Assert-RaSignedManifest {
    <#
    .SYNOPSIS
        Verifies the signature of manifest bytes and parses them.
    .DESCRIPTION
        Throws "signature is INVALID" when the pinned key does not verify the bytes.
    .PARAMETER Bytes
        Manifest bytes exactly as received.
    .PARAMETER SignatureText
        Base64 signature.
    .OUTPUTS
        System.Collections.Specialized.OrderedDictionary
    .EXAMPLE
        Assert-RaSignedManifest $bytes $sig
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Specialized.OrderedDictionary])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$Bytes,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$SignatureText
    )
    if (-not (Test-RaSignature -Data $Bytes -SignatureBase64 $SignatureText -PublicKeyXml $script:RaUpdatePublicKeyXml)) {
        throw 'Release manifest signature is INVALID; nothing was changed.'
    }
    return (ConvertFrom-RaManifestJson (ConvertFrom-RaUtf8 $Bytes -Strict))
}

function Test-RaReleaseAssetUrl {
    <#
    .SYNOPSIS
        Tests that an asset URL is a download of this repository's releases.
    .DESCRIPTION
        Must start with https://github.com/<repo>/releases/download/ (case-insensitive).
    .PARAMETER Url
        Asset URL.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaReleaseAssetUrl $assets['release-manifest.json']
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Url)
    $prefix = $script:RaGitHubDownloadPrefixFormat -f $script:RaGitHubRepo
    return ([bool]$Url -and $Url.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))
}

function Get-RaRemoteRelease {
    <#
    .SYNOPSIS
        Fetches and verifies the latest signed release manifest.
    .DESCRIPTION
        Offline (-LocalManifestPath): reads <path> and <path>.sig. Online: exactly one
        GitHub API request plus the two small manifest assets, only on explicit user
        request. The signature must verify with the pinned key, the tag must match the
        manifest version and every asset URL must be under this repository's releases.
    .PARAMETER LocalManifestPath
        Signed release-manifest.json for offline use, or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Manifest, ZipUrl)
    .EXAMPLE
        Get-RaRemoteRelease $null
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$LocalManifestPath)
    if ($LocalManifestPath) {
        $bytes = [IO.File]::ReadAllBytes($LocalManifestPath)
        $sig = [IO.File]::ReadAllText("$LocalManifestPath$($script:RaSignatureFileSuffix)")
        return [pscustomobject]@{ Manifest = (Assert-RaSignedManifest $bytes $sig); ZipUrl = $null }
    }
    if ($script:RaGitHubRepo -match $script:RaRepoPlaceholderMarker) {
        throw 'Update channel is not configured yet (repository placeholder). Use -ManifestPath and -PackagePath for an offline update.'
    }
    $api = $script:RaGitHubLatestReleaseUrlFormat -f $script:RaGitHubRepo
    Write-RaLog "Checking $api (no identifiers are sent)"
    $rel = (ConvertFrom-RaUtf8 (Get-RaHttpContent $api)) | ConvertFrom-Json
    $assets = @{}
    foreach ($a in @($rel.assets)) { $assets[[string]$a.name] = [string]$a.browser_download_url }
    foreach ($n in $script:RaManifestAssetName, $script:RaSignatureAssetName) {
        if (-not $assets.ContainsKey($n)) { throw "Latest release has no $n asset." }
        if (-not (Test-RaReleaseAssetUrl $assets[$n])) { throw "Unexpected asset URL: $($assets[$n])" }
    }
    $mBytes = Get-RaHttpContent $assets[$script:RaManifestAssetName]
    $sigText = ConvertFrom-RaUtf8 (Get-RaHttpContent $assets[$script:RaSignatureAssetName])
    $m = Assert-RaSignedManifest $mBytes $sigText
    if (([string]$rel.tag_name).TrimStart($script:RaReleaseTagPrefix) -ne $m.Version) {
        throw "Release tag $($rel.tag_name) does not match manifest version $($m.Version)."
    }
    $zipUrl = $assets[$m.ZipName]
    if (-not (Test-RaReleaseAssetUrl $zipUrl)) { throw "Release has no valid $($m.ZipName) asset." }
    return [pscustomobject]@{ Manifest = $m; ZipUrl = $zipUrl }
}

function Invoke-RaCheckUpdate {
    <#
    .SYNOPSIS
        Reports the newest release and whether it (or the installed one) supports the game.
    .DESCRIPTION
        Changes nothing on disk except the log.
    .PARAMETER State
        State object (read only).
    .PARAMETER GameDir
        Game folder or empty (then game support is not reported).
    .PARAMETER LocalManifestPath
        Offline signed manifest or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Remote, Newer, GameSha, InstalledSupportsGame, LatestSupportsGame)
    .EXAMPLE
        Invoke-RaCheckUpdate (Read-RaState) (Get-RaGameDir $steam) $null
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$GameDir,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$LocalManifestPath
    )
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
        else { Write-RaLog (Get-RaText 'GameNotSupported' @($gameSha.Substring(0, $script:RaGameHashDisplayLength))) -Level WARN }
    }
    return [pscustomobject]@{
        Remote = $remote; Newer = $newer; GameSha = $gameSha
        InstalledSupportsGame = [bool]$installedSupports; LatestSupportsGame = [bool]$latestSupports
    }
}

function Invoke-RaUpdate {
    <#
    .SYNOPSIS
        Installs the newest signed release and activates it.
    .DESCRIPTION
        Returns $false when already up to date and the installed files verify; a
        failed verification triggers a reinstall of the same version.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER GameDir
        Game folder or empty.
    .PARAMETER LocalManifestPath
        Offline signed manifest or empty.
    .PARAMETER LocalZip
        Offline release ZIP or empty.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Invoke-RaUpdate $state $gameDir $null $null
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$GameDir,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$LocalManifestPath,
        [Parameter(Position = 3)][AllowEmptyString()][AllowNull()][string]$LocalZip
    )
    $check = Invoke-RaCheckUpdate -State $State -GameDir $GameDir -LocalManifestPath $LocalManifestPath
    $m = $check.Remote.Manifest
    if (-not $check.Newer) {
        if (Test-RaInstalledVersion $m (Get-RaVersionDir $m.Version)) { return $false }
        Write-RaLog "Installed files of $($m.Version) failed verification; reinstalling." -Level WARN
    }
    [void](Install-RaVersion -Manifest $m -LocalZip $LocalZip -ZipUrl $check.Remote.ZipUrl)
    Set-RaActiveVersion $State $m.Version
    return $true
}

function Invoke-RaRollback {
    <#
    .SYNOPSIS
        Switches back to the previous version kept on disk.
    .DESCRIPTION
        The previous version must still verify against its stored manifest. The two
        versions swap roles, so a second rollback returns to the newer one.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-RaRollback $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $prev = $State.PreviousVersion
    if (-not $prev) { throw 'No previous version to roll back to.' }
    $m = Read-RaStoredManifest $prev
    if (-not $m -or -not (Test-RaInstalledVersion $m (Get-RaVersionDir $prev))) {
        throw "Previous version $prev is missing or failed verification; cannot roll back."
    }
    $cur = $State.InstalledVersion
    Set-RaActiveVersion $State $prev
    Write-RaLog "Rolled back to $prev (kept $cur as previous)."
}
#endregion Features/Update.ps1

#region Features/Shortcut.ps1
# ---------------------------------------------------------------------------
# Slice: game shortcut (.lnk) in a folder the user chooses.
# ---------------------------------------------------------------------------

function Get-RaShortcutInfo {
    <#
    .SYNOPSIS
        Reads a .lnk file through the shortcut adapter.
    .DESCRIPTION
        Returns target, arguments, description and icon location.
    .PARAMETER Path
        Shortcut file.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (TargetPath, Arguments, Description, IconLocation)
    .EXAMPLE
        (Get-RaShortcutInfo $lnk).TargetPath
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    return (& $script:RaReadShortcut $Path)
}

function Test-RaOwnShortcut {
    <#
    .SYNOPSIS
        Tests whether a shortcut was created by us.
    .DESCRIPTION
        True only for an existing file whose target is RenameAnythingLauncher.exe inside
        the root folder. Unreadable files are not ours.
    .PARAMETER Path
        Shortcut file.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaOwnShortcut $lnk
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try { $t = (Get-RaShortcutInfo $Path).TargetPath } catch { return $false }
    if (-not $t) { return $false }
    $rootFull = [IO.Path]::GetFullPath($script:RaRoot).TrimEnd('\') + '\'
    return ([IO.Path]::GetFileName($t) -ieq $script:RaLauncherExeName -and
        [IO.Path]::GetFullPath($t).StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase))
}

function Show-RaFolderDialog {
    <#
    .SYNOPSIS
        Shows the Windows folder picker and returns the chosen folder or $null.
    .DESCRIPTION
        Default folder-picker adapter. WinForms dialogs need an STA thread; under an MTA
        host (PowerShell 7 default) the dialog runs in a temporary STA runspace.
    .PARAMETER Default
        Folder selected initially.
    .OUTPUTS
        System.String
    .EXAMPLE
        Show-RaFolderDialog ([Environment]::GetFolderPath('Desktop'))
    #>
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseCompatibleTypes', '',
        Justification = 'System.Windows.Forms is loaded by Add-Type right before use; the profiles list only default assemblies.')]
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][string]$Default)
    $code = {
        param($start, $title)
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = [System.Windows.Forms.FolderBrowserDialog]::new()
        $dlg.Description = $title
        $dlg.SelectedPath = $start
        $dlg.ShowNewFolderButton = $true
        $owner = [System.Windows.Forms.Form]::new()
        $owner.TopMost = $true
        try {
            if ($dlg.ShowDialog($owner) -eq 'OK') { return $dlg.SelectedPath }
            return $null
        } finally {
            $owner.Dispose()
            $dlg.Dispose()
        }
    }
    $title = $script:RaFolderPickerTitle
    if ([Threading.Thread]::CurrentThread.ApartmentState -eq 'STA') { return (& $code $Default $title) }
    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = 'STA'
    $rs.Open()
    try {
        $ps = [powershell]::Create()
        $ps.Runspace = $rs
        [void]$ps.AddScript($code.ToString()).AddArgument($Default).AddArgument($title)
        $r = @($ps.Invoke())
        if ($r.Count -gt 0) { return $r[-1] }
        return $null
    } finally { $rs.Dispose() }
}

function Select-RaFolder {
    <#
    .SYNOPSIS
        Lets the user pick a folder through the folder-picker adapter.
    .DESCRIPTION
        $script:RaFolderPicker = $null means the real dialog (Show-RaFolderDialog).
    .PARAMETER Default
        Folder selected initially.
    .OUTPUTS
        System.String
    .EXAMPLE
        Select-RaFolder $desktop
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][string]$Default)
    if ($script:RaFolderPicker) { return (& $script:RaFolderPicker $Default) }
    return (Show-RaFolderDialog $Default)
}

function Resolve-RaShortcutFolder {
    <#
    .SYNOPSIS
        Decides where the shortcut goes.
    .DESCRIPTION
        -ShortcutPath wins; with -Yes the Desktop is used; otherwise the user chooses
        Desktop (1 or Enter), the folder dialog (2) or types an absolute path.
    .PARAMETER Requested
        -ShortcutPath value or empty.
    .OUTPUTS
        System.String
    .EXAMPLE
        Resolve-RaShortcutFolder $ShortcutPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Requested)
    $desktop = & $script:RaDesktopPath
    if ($Requested) { return $Requested }
    if ($script:RaAssumeYes) { return $desktop }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskShortcutFolder' @($desktop))
        if ($a -eq '' -or $a -eq $script:RaShortcutFolderDesktopChoice) { return $desktop }
        if ($a -eq $script:RaShortcutFolderPickerChoice) {
            $f = Select-RaFolder $desktop
            if ($f) { return $f }
            continue
        }
        $a = $a.Trim('"')
        if ([IO.Path]::IsPathRooted($a)) { return $a }
    }
}

function New-RaShortcut {
    <#
    .SYNOPSIS
        Creates the "Hearts of Iron IV + Rename Anything" shortcut.
    .DESCRIPTION
        Target: the stable launcher path; argument: the game's Paradox Launcher
        (dowser.exe, what Steam's %command% runs); icon from hoi4.exe (read-only);
        description: the multiplayer warning. Never overwrites a file it did not create.
        The path is recorded in state.json so uninstall can remove it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER Folder
        Existing folder for the shortcut.
    .PARAMETER LauncherPath
        Stable launcher path (current junction).
    .PARAMETER GameDir
        Game folder from Get-RaGameDir.
    .OUTPUTS
        System.String (the shortcut path)
    .EXAMPLE
        New-RaShortcut $state $folder (Get-RaLauncherPath) (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Folder,
        [Parameter(Mandatory, Position = 2)][ValidateNotNullOrEmpty()][string]$LauncherPath,
        [Parameter(Position = 3)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) { throw "Shortcut folder does not exist: $Folder" }
    $lnk = Join-Path ((Resolve-Path -LiteralPath $Folder).ProviderPath) $script:RaShortcutFileName
    if ((Test-Path -LiteralPath $lnk) -and -not (Test-RaOwnShortcut $lnk)) { throw "A different file already exists and was left untouched: $lnk" }
    if (-not $GameDir) { throw 'HOI4 installation not found through Steam libraries; cannot build the shortcut.' }
    $dowser = Join-Path $GameDir $script:RaParadoxLauncherExeName
    if (-not (Test-Path -LiteralPath $dowser -PathType Leaf)) { throw "Paradox Launcher not found: $dowser" }
    $icon = Join-Path $GameDir $script:RaGameExeName
    $iconLocation = if (Test-Path -LiteralPath $icon) { $icon + $script:RaIconIndexSuffix } else { $LauncherPath + $script:RaIconIndexSuffix }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would create $lnk -> `"$LauncherPath`" `"$dowser`"" -Level PLAN
        return $lnk
    }
    $spec = [pscustomobject]@{
        TargetPath       = $LauncherPath
        Arguments        = '"' + $dowser + '"'
        WorkingDirectory = Split-Path -Parent $LauncherPath
        IconLocation     = $iconLocation
        Description      = Get-RaText 'ShortcutComment'
    }
    & $script:RaWriteShortcut $lnk $spec
    $State.Shortcuts = @(@($State.Shortcuts | Where-Object { $_ -ne $lnk }) + $lnk)
    Write-RaLog "Shortcut created: $lnk"
    return $lnk
}

function Remove-RaOwnShortcut {
    <#
    .SYNOPSIS
        Deletes our shortcuts (recorded ones and the Desktop default).
    .DESCRIPTION
        A shortcut that no longer points to our launcher is left untouched and logged.
        Clears the recorded list.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaOwnShortcut $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $candidates = @($State.Shortcuts) + @(Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName)
    foreach ($p in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $p)) { continue }
        if (Test-RaOwnShortcut $p) {
            if ($script:RaDry) {
                Write-RaLog "[dry-run] would delete shortcut $p" -Level PLAN
                continue
            }
            Remove-Item -LiteralPath $p -Force
            Write-RaLog "Shortcut deleted: $p"
        } else {
            Write-RaLog "Shortcut no longer points to our launcher, left untouched: $p" -Level WARN
        }
    }
    $State.Shortcuts = @()
}
#endregion Features/Shortcut.ps1

#region Features/Uninstall.ps1
# ---------------------------------------------------------------------------
# Slice: uninstall (-Uninstall). Keeps bootstrap.log.
# ---------------------------------------------------------------------------

function Invoke-RaUninstall {
    <#
    .SYNOPSIS
        Removes the Steam launch option part, our shortcuts and the installed files.
    .DESCRIPTION
        Steam first: while Steam keeps the launch option pointing at the launcher, no
        file is removed. Then shortcuts, then the 'current' junction (so nothing can be
        deleted through it), then everything under the root folder except bootstrap.log.
    .PARAMETER State
        State object (modified).
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-RaUninstall $state $steam
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    if ($SteamRootPath) {
        if (-not (Remove-RaSteamLaunchOption $State $SteamRootPath)) {
            throw 'Steam is running: the launch option still points to the launcher, so no files were removed. Close Steam and run -Uninstall again.'
        }
    } else {
        Write-RaLog 'Steam not found; skipping launch options.' -Level WARN
    }
    Remove-RaProxy $State
    Remove-RaOwnShortcut $State
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    $log = Get-RaLogPath
    $link = Get-RaCurrentLink
    if (Test-Path -LiteralPath $link) {
        if ($script:RaDry) { Write-RaLog "[dry-run] would delete junction $link" -Level PLAN } else { Remove-RaJunction $link }
    }
    foreach ($item in Get-ChildItem -LiteralPath $script:RaRoot -Force) {
        if ($item.FullName -ieq $log -or $item.FullName -ieq $link) { continue }
        if ($script:RaDry) {
            Write-RaLog "[dry-run] would delete $($item.FullName)" -Level PLAN
            continue
        }
        if (Test-RaReparsePoint $item) {
            [IO.Directory]::Delete($item.FullName, $false)
            continue
        }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
    Write-RaLog "Removed files in $($script:RaRoot) (kept $($script:RaLogFileName))"
}
#endregion Features/Uninstall.ps1

#region Main.ps1
# ---------------------------------------------------------------------------
# Main: option handling, action dispatch and the script entry point.
# ---------------------------------------------------------------------------

$script:RaOptionNames = @(
    'Install', 'Shortcut', 'ShortcutPath', 'SteamLaunchOption', 'SteamAccount', 'RemoveSteamLaunchOption',
    'Uninstall', 'DryRun', 'Yes', 'PackagePath', 'SteamRoot', 'Language', 'CheckUpdate', 'Update', 'Rollback', 'ManifestPath',
    'LoadMethod', 'ProxyActivation'
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
        Sets language, root folder (unless a test already set it), dry-run and -Yes,
        prints title/intro and logs the start line with the switches that were given.
    .PARAMETER Options
        Normalized option table (every known key present).
    .OUTPUTS
        None.
    .EXAMPLE
        Initialize-RaRun $options
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options)
    $script:RaLang = if ($Options.Language) { $Options.Language } else { Get-RaDefaultLanguage }
    if (-not $script:RaRoot) { $script:RaRoot = Join-Path $env:LOCALAPPDATA $script:RaRootDirName }
    $script:RaDry = [bool]$Options.DryRun
    $script:RaAssumeYes = [bool]$Options.Yes
    Write-RaHost (Get-RaText 'Title') -Color White
    Write-RaHost (Get-RaText 'Intro' @((Get-RaLogPath)))
    if ($script:RaDry) { Write-RaHost (Get-RaText 'DryRunNote') -Color Cyan }
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
        LoadMethod     = if ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod) { $Options.LoadMethod } else { 'Proxy' }
        ProxyActivation = if ($Options.ContainsKey('ProxyActivation') -and $Options.ProxyActivation) { $Options.ProxyActivation } else { 'Flag' }
    }
    if (-not ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod) -and $State -and $State.InstalledVersion) {
        $plan.LoadMethod = $State.LoadMethod
    }
    if (-not ($plan.Install -or $plan.Steam -or $plan.Shortcut)) {
        if ($script:RaAssumeYes) { throw 'With -Yes choose at least one of -Install, -SteamLaunchOption, -Shortcut, -Update, -Uninstall.' }
        $plan.Install = Confirm-Ra (Get-RaText 'AskInstall' @($script:RaRoot)) $true
        if (-not ($Options.ContainsKey('LoadMethod') -and $Options.LoadMethod)) {
            $defaultLoader = if ($plan.LoadMethod -eq 'Launcher') { '2' } else { '1' }
            do { $loader = Read-RaAnswer (Get-RaText 'AskLoadMethod' @($defaultLoader)) } until ($loader -in @('', '1', '2'))
            if ($loader) { $plan.LoadMethod = if ($loader -eq '2') { 'Launcher' } else { 'Proxy' } }
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
    if ($plan.Steam -or $plan.Shortcut) {
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
        Install-RaPackage $State $Options.PackagePath
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
        Priority: -Uninstall, -RemoveSteamLaunchOption, -CheckUpdate, -Rollback, -Update,
        then the install/integration flow. Exceptions propagate to Invoke-RaMain.
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
        Write-RaHost (Get-RaText 'UninstallDone' @((Get-RaLogPath))) -Color Green
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
        Write-RaDone
        return $script:RaExitSuccess
    }
    if ($Options.Update) {
        if ((Invoke-RaUpdate -State $state -GameDir (& $gameDir) -LocalManifestPath $Options.ManifestPath -LocalZip $Options.PackagePath)) {
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
        FAILED and turned into exit code 1.
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
        DryRun = $DryRun; Yes = $Yes; PackagePath = $PackagePath; SteamRoot = $SteamRoot; Language = $Language
        CheckUpdate = $CheckUpdate; Update = $Update; Rollback = $Rollback; ManifestPath = $ManifestPath
        LoadMethod = $LoadMethod; ProxyActivation = $ProxyActivation
    }
    if ($MyInvocation.MyCommand.CommandType -eq 'ExternalScript') { exit ([int]@($raExit)[-1]) }
    # Under `irm | iex` never call exit (it would close the user's terminal).
}
#endregion Main.ps1
