<#
.SYNOPSIS
    Rename Anything (HOI4) bootstrap: installs the native launcher bundle and
    optionally wires it into Steam launch options and/or a game shortcut.

.DESCRIPTION
    Runs on Windows PowerShell 5.1 and PowerShell 7, without administrator rights
    and without external modules. The default Launcher method writes nothing into the HOI4
    installation; the explicit Proxy alternative adds one version.dll there. Saves are
    untouched. See tools/bootstrap/README.md.

    Interactive (no action switches): a terminal interface (menu: install or update, install an
    older version, disable, uninstall) when the console is interactive, plain prompts otherwise or with -Plain.
        irm <release-url>/install.ps1 | iex
    Noninteractive:
        & ([scriptblock]::Create((irm <release-url>/install.ps1))) -Install -SteamLaunchOption -Yes
        .\install.ps1 -Install -LoadMethod Launcher -Shortcut -ShortcutPath "$env:USERPROFILE\Desktop" -Yes

    This file is intentionally English-only and pure ASCII. ASCII bytes read the same in
    UTF-8, with or without a BOM, and in every legacy code page that Windows PowerShell 5.1
    or `irm` may use, and every message prints the same in any console encoding. The
    script therefore never changes the console encoding of the caller's session.

    install.ps1 is GENERATED from tools/bootstrap/src by tools/bootstrap/build-install.ps1.
    Edit the fragments in src/ and rebuild; never edit install.ps1 by hand.

.PARAMETER Install
    Install or verify the release pinned in this script (with -Latest: the newest signed release).

.PARAMETER Latest
    With -Install: download the newest release from GitHub Releases, verify its signed manifest
    and every SHA-256, and install it into %LOCALAPPDATA%\RenameAnything (never the game folder).

.PARAMETER LoadMethod
    Launcher (default): the CAPIE launcher in the Steam launch options; nothing in the game folder;
    CAPIE loads only when the "CAPIE" mod is enabled in the playset. Proxy (explicit alternative,
    alias -Method): game-local version.dll activated by -capie; it does not read the playset.

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

.PARAMETER Purge
    With -Uninstall: also delete the log and the localconfig.vdf backups this script made, so
    nothing of the installation is left.

.PARAMETER Disable
    Remove only our part of the Steam launch options and the proxy activation marker; files stay.

.PARAMETER NoAnimation
    Terminal interface without the splash animation (also: RA_NO_ANIMATION=1, NO_COLOR, CI).

.PARAMETER Plain
    Plain question-and-answer prompts instead of the terminal interface.

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
    Install or update: when CAPIE is not installed, install it (the newest signed release when the
    update channel is configured); otherwise install the newest signed release and switch to it. When
    it is already the newest, repair instead (switch mod, and the Steam launch option with
    -SteamLaunchOption).

.PARAMETER ListVersions
    List the signed releases on GitHub (one request) with API level, game version, date and changes.

.PARAMETER Version
    Install this signed release (x.y.z) from GitHub instead of the newest; same verification.

.PARAMETER CloseSteam
    With -Yes: when Steam is running and the launch option must change, close Steam with
    "steam.exe -shutdown" (never killed), wait up to 60 s, and start Steam again afterwards.
    Without -Yes the script asks instead.

.PARAMETER Rollback
    Switch back to the previous version kept on disk.

.PARAMETER ManifestPath
    Signed release-manifest.json for an offline -Update or -CheckUpdate.

.OUTPUTS
    System.Int32. Exit code 0 on success, 1 on failure, 2 when no action was given and the
    console cannot answer questions (also the process exit code with -File).

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
    [switch]$Latest,
    [Alias('Method')][ValidateSet('', 'Proxy', 'Launcher')][string]$LoadMethod,
    [ValidateSet('', 'Flag', 'Config')][string]$ProxyActivation,
    [switch]$Shortcut,
    [string]$ShortcutPath,
    [switch]$SteamLaunchOption,
    [string]$SteamAccount,
    [switch]$RemoveSteamLaunchOption,
    [switch]$Uninstall,
    [switch]$Purge,
    [switch]$Disable,
    [switch]$NoAnimation,
    [switch]$Plain,
    [Alias('WhatIf')][switch]$DryRun,
    [switch]$Yes,
    [string]$PackagePath,
    [string]$SteamRoot,
    [switch]$CheckUpdate,
    [switch]$Update,
    [switch]$ListVersions,
    [string]$Version,
    [switch]$CloseSteam,
    [switch]$Rollback,
    [string]$ManifestPath
)

# GENERATED FILE: edit tools/bootstrap/src/* and run tools/bootstrap/build-install.ps1.

#region Shared/Configuration.ps1
# ---------------------------------------------------------------------------
# Release configuration: the ONLY values that change for a public release.
# ---------------------------------------------------------------------------

# GitHub "owner/repo". Release downloads and the (on-demand only) update check derive from it.
$script:RaGitHubRepo = 'capie-mods/capie'

# RSA public key (XML, >= 2048 bit) that signs release-manifest.json of every later release.
# Placeholder until the maintainer generates the key (tools/bootstrap/sign-release-manifest.ps1).
$script:RaUpdatePublicKeyXml = '<RSAKeyValue><Modulus>21/rZrMx9hLPYwh/QMiCnXaOUebJdj+aLFxd/vpqfcDsrczFXN6j8JA/9Iuch3Nc5aFr/KRMhtlGsPqf6ov/IseZUd//Q5snHolFyZ9QI78/k8lEGZ4rkojPZoqg8SQh5DdNYA0UHL3D/uI6HL26ZwfV7v3oAlcTUXFdW1quGvtxFlDNDFCvqHMl+Hc7BpOwQQXG4ChS08UHSYsTaBr0U7nFC734w4wqgae3ahZJBFDmubfkPiPZd7x+NExEihOLbwrzHc1NegtSEBlOcOWxwBlLXp49JitQttrJyA+2/fuWFACMqynMsSWtUnOslslULb0vcAbJDV9RtdS4qLZsAZVfJjbuk0+kfqN7ztOl2biXKCu7OlzvP4t9hfA5DCoqyP3PGqZTA8nFqXkIU5USKRg75cwQ/j7YczH2SBQpNghivWghCQMUVUgheL1ETk6BZCY+oJy1RIEuWkb2W002RsmmX/4R69YdCDAazCEX6sTWqALFboMLGd+5AMQRu6ZV</Modulus><Exponent>AQAB</Exponent></RSAKeyValue>'

# Pinned release manifest. The ZIP hash is checked BEFORE extraction and every
# extracted file is checked against its own pinned hash; any mismatch, missing or
# extra file aborts the install (fail-closed). File hashes below are the current
# reviewed launcher-candidate bundle; ZipSha256 must be filled when the release ZIP
# is built. Until then the installer refuses to install anything.
$script:RaPinnedRelease = [ordered]@{
    Version             = '1.5.0'
    ZipName             = 'CAPIE-1.5.0-win64.zip'
    ZipSha256           = 'C846D5968ECFE71B95FCDDD52560C94A0239ECF184FBA1461F8E6E8A0EEE211C'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                                = '552C0497D96F9C7A4EA15DD332E21763628612A0383F055D2564EBF8C2A0E5F3'
        'LICENSE.MinHook.txt'                                       = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'                                    = '7A1C946F8A15F6120A0232453E3013B9F854EC2A6B526D0AB9C86F19959D0DE1'
        'runtime/capie_editor.dll'                                  = '0DAD2732552305F43E1E87FB6E10E6A5B473E683EA27F1E82D0EBD9B8934D656'
        'runtime/capie_injector.exe'                                = 'EB5E9FF5F9B944B55CA5EB4B343D18D507584284FB60F5F119A8BE8843484C25'
        'proxy/version.dll'                                         = '436E04D9FAD9B0A328F311F9E187D312DEB5EDB1F944BC30250A3C9D76429961'
        'mod/capie.mod'                                             = '618206343DF86601F223E112F2BFDD30974545F5CA9EA956F9245A57804AD977'
        'mod/capie/descriptor.mod'                                  = '7EB57809FACDD308ACD61F238B6D5B08AC83CE433A689F0FC25447C343C07A53'
        'mod/capie/gfx/interface/capie_menu_mark.dds'               = '6842E6D83D773648EE7A5878EA5B0A2E77C66353C926406CFE95B23C88B2B522'
        'mod/capie/interface/capie_menu.gfx'                        = '392A554D2286B5C81591946E4E0AA439FCC0D3A1D5471F56D472B656B242E695'
        'mod/capie/interface/frontendmainview.gui'                  = '901F62142B80FEEF88892A64B0DE4D7B96B2B448967D12156BAF2415C6A15918'
        'mod/capie/localisation/english/capie_switch_l_english.yml' = 'EE94308AB621CC46BC70AD066F1E223AF2071FA9F4A3728797ABD04AF23ABC3F'
        'mod/capie/localisation/polish/capie_switch_l_polish.yml'   = 'E59AF5200E9D539A9AD36AC8A6EDEDD1D589C55FC176236B9498DB92C0182CA9'
        'mod/capie/thumbnail.png'                                   = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
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
$script:RaSteamHelperProcessName = 'steamwebhelper'          # must be gone too before localconfig.vdf is edited
$script:RaSteamExeRegistryValue = 'SteamExe'
$script:RaSteamExeName = 'steam.exe'
$script:RaSteamShutdownArgument = '-shutdown'                 # Steam's own clean exit; nothing is killed
$script:RaSteamShutdownTimeoutSeconds = 60
$script:RaSteamShutdownPollMilliseconds = 1000
$script:RaSteamProgressEverySeconds = 5
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
# "CAPIE" switch mod: bundle prefix and the game's user mod folder (relative to Documents).
$script:RaModBundlePrefix = 'mod/'
$script:RaGameUserModRelPath = 'Paradox Interactive\Hearts of Iron IV\mod'
# Descriptor the Paradox Launcher writes for a subscription to the CAPIE switch mod on the Workshop.
# HOI4 skips a Workshop mod when a local descriptor has the same name, so with a subscription the
# local copy is not installed (the local copy is also named differently: "CAPIE (local copy)").
$script:RaWorkshopModDescriptor = 'ugc_3819929471.mod'
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
# localconfig.vdf backups made by New-RaBackupPath (only these are deleted by -Uninstall -Purge).
$script:RaBackupNamePattern = '^localconfig\.vdf\.renameanything-backup-\d{8}-\d{6}(-\d+)?$'

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
# Our quoted (or unquoted) launcher path plus trailing blanks, anywhere in the options.
$script:RaWrapperPattern = '(?:"[^"]*\\{0}"|(?<!\S)[^\s"]*\\{0})[ \t]*' -f [regex]::Escape($script:RaLauncherExeName)

# --- Update channel -------------------------------------------------------
$script:RaGitHubLatestReleaseUrlFormat = 'https://api.github.com/repos/{0}/releases/latest'
# Version picker: one request lists the newest releases; manifests are fetched only for the chosen one.
$script:RaGitHubReleasesUrlFormat = 'https://api.github.com/repos/{0}/releases?per_page=20'
$script:RaReleaseSummaryLines = 3
$script:RaReleaseDateFormat = 'yyyy-MM-dd'
# Facts in the release notes: the lines of tools/release (build_notes) and hand-written notes
# ("Hearts of Iron IV 1.19.3.0 (`hoi4.exe` SHA-256 7DC947BE...31D5)"). A shortened hash keeps its start.
$script:RaNotesGameVersionPattern = 'Hearts of Iron IV (\d+(?:\.\d+){1,3})'
$script:RaNotesGameShaPattern = 'hoi4\.exe`?\s+SHA-256\s+`?([0-9A-Fa-f]{8,64})'
$script:RaNotesApiLevelPattern = 'script API level (\d+)'
$script:RaNotesBulletPattern = '^\s*[-*]\s+(.+)$'
$script:RaNotesSectionPattern = '^\s*##\s+Supported game build'   # the changelog part ends here
$script:RaReleaseShaPrefixLength = 8
$script:RaVersionCorePattern = '^(\d+\.\d+\.\d+).*$'
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
$script:RaYesAnswers = @('y', 'yes')
$script:RaNoAnswers = @('n', 'no')
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
$script:RaGameHashDisplayLength = 16
$script:RaExitSuccess = 0
$script:RaExitFailure = 1
$script:RaExitUsage = 2                    # no action and no interactive console: usage shown
# Switches that name an action; without any of them an interactive run opens the menu.
$script:RaActionOptionNames = @(
    'Install', 'Shortcut', 'ShortcutPath', 'SteamLaunchOption', 'RemoveSteamLaunchOption', 'Uninstall',
    'CheckUpdate', 'Update', 'Rollback', 'Disable', 'ListVersions', 'Version'
)

# --- Terminal interface ---------------------------------------------------
# Colours copied from docs/design/tokens.json (a Pester test keeps them equal).
$script:RaTuiPalette = [ordered]@{
    'field-ink'        = '#161412'
    'panel-rim'        = '#3c3936'
    'steel-bevel'      = '#76776b'
    'olive-button'     = '#2e3627'
    'olive-lit'        = '#424e38'
    'pencil-green-lit' = '#507040'
    'brass'            = '#d0a070'
    'parchment'        = '#ece6d3'
    'khaki'            = '#c3b091'
    'ash'              = '#a39a86'
    'header-amber'     = '#ffbd00'
    'go-green'         = '#7db35a'
    'alarm-red'        = '#ff3232'
    'caution-orange'   = '#ff7019'
}
$script:RaTuiBackground = 'field-ink'       # recessed panel; alarm-red reaches 5.0:1 on it
$script:RaTuiSelectedBackground = 'olive-button'
$script:RaTuiMaxWidth = 78
$script:RaTuiMinWidth = 44                 # the map sheet is 39 columns plus the indent
$script:RaTuiFallbackWidth = 80
$script:RaTuiIndent = 2
$script:RaTuiFrameDelayMs = 45              # one character per frame: the pencil writes
$script:RaTuiHoldDelayMs = 350              # still frame after the name is written
$script:RaTuiStatusWidth = 6                # "OK    ", "FAIL  " ...
$script:RaTuiKeyWidth = 14                  # summary key column
# Status words of the CLI pattern (DESIGN-SYSTEM.md, "CLI output pattern") and their colours.
$script:RaTuiStatusStyles = [ordered]@{ OK = 'go-green'; ASK = 'header-amber'; SKIP = 'khaki'; NOTE = 'khaki'; FAIL = 'alarm-red' }
$script:RaTuiLevelStatus = @{ INFO = 'OK'; WARN = 'NOTE'; ERROR = 'FAIL'; PLAN = 'SKIP' }
$script:RaTuiEnvNoColor = 'NO_COLOR'
$script:RaTuiEnvNoAnimation = 'RA_NO_ANIMATION'
$script:RaTuiEnvCi = 'CI'
$script:RaTuiEnvTerm = 'TERM'
$script:RaTuiDumbTerm = 'dumb'
$script:RaTuiMenuActions = @('Setup', 'Older', 'Disable', 'Uninstall', 'Quit')
# The pencil writes this name into the margin of the map sheet, next to the grid square of the flag.
$script:RaTuiAnnotationName = 'Rename Anything'
$script:RaTuiAnnotationRef = 'C3'
$script:RaTuiPencil = '<====]'
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
$script:RaDry = $false          # -DryRun: plan only, write nothing
$script:RaAssumeYes = $false    # -Yes: never prompt
$script:RaQuiet = $false        # tests: no console output
$script:RaAfterWriteHook = $null # tests: simulate a bad write before verification
$script:RaBeforeOwnedRemove = $null # tests: simulate a replacement after ownership verification
$script:RaLogSink = $null       # terminal interface: receives (Level, Message) of console log lines
$script:RaMpWarningShown = $false # terminal interface already showed the multiplayer note
$script:RaCloseSteam = $false   # -CloseSteam: with -Yes the script may close and restart Steam
$script:RaSteamClosedByUs = $false # this run closed Steam (or planned to, in dry-run mode)
$script:RaSteamRestartExe = $null  # steam.exe to start again after the change
$script:RaReleaseCache = $null  # releases listed in this run (one GitHub request per run)

# --- Console and prompts --------------------------------------------------
# ReadHost(Prompt) -> string
$script:RaReadHost = { param([string]$Prompt) Read-Host -Prompt $Prompt }
# WriteHost(Message, Color) -> nothing
$script:RaWriteHost = { param([string]$Message, [string]$Color) Write-RaConsoleLine -Message $Message -Color $Color }

# --- Terminal interface (raw console) --------------------------------------
# GetTerminal() -> [pscustomobject]@{ Interactive; Vt; Width }. Interactive = neither input nor
# output is redirected and the session is user-interactive; Vt = the host renders ANSI sequences.
$script:RaGetTerminal = {
    $interactive = $false
    $width = 0
    try {
        $interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected -and -not [Console]::IsOutputRedirected
        $width = [Console]::WindowWidth
    } catch { $interactive = $false }
    $vt = $false
    try { $vt = [bool]$Host.UI.SupportsVirtualTerminal } catch { $vt = $false }
    [pscustomobject]@{ Interactive = $interactive; Vt = $vt; Width = $width }
}
# GetEnv(Name) -> value or $null
$script:RaGetEnv = { param([string]$Name) [Environment]::GetEnvironmentVariable($Name) }
# WriteRaw(Text) -> nothing; text may contain ANSI sequences and no implicit newline
$script:RaWriteRaw = { param([string]$Text) $Host.UI.Write($Text) }
# ReadKey() -> System.ConsoleKeyInfo (no echo)
$script:RaReadKey = { [Console]::ReadKey($true) }
# Sleep(Milliseconds) -> nothing (animation frames only)
$script:RaSleep = { param([int]$Milliseconds) Start-Sleep -Milliseconds $Milliseconds }

# --- Processes, registry, clock, desktop ----------------------------------
# GetSteamProcess() -> array; empty when Steam is not running
$script:RaGetSteamProcess = { @(Get-Process -Name $script:RaSteamProcessName -ErrorAction SilentlyContinue) }
# GetSteamHelperProcess() -> array of steamwebhelper processes (they outlive steam.exe briefly)
$script:RaGetSteamHelperProcess = { @(Get-Process -Name $script:RaSteamHelperProcessName -ErrorAction SilentlyContinue) }
# StartProcess(FilePath, Arguments) -> nothing; starts a program without waiting (Steam shutdown/start only)
$script:RaStartProcess = {
    param([string]$FilePath, [string[]]$Arguments)
    if (@($Arguments).Count -gt 0) { Start-Process -FilePath $FilePath -ArgumentList $Arguments } else { Start-Process -FilePath $FilePath }
}
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
# DocumentsPath() -> folder (the game's user folder lives under it)
$script:RaDocumentsPath = { [Environment]::GetFolderPath('MyDocuments') }
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
        "  -Update               install, or update to the newest signed release (-Rollback goes back)`n",
        "  -ListVersions         list the signed releases; -Version x.y.z installs one of them`n",
        "  -Disable              remove the CAPIE launcher from the Steam launch options; files stay`n",
        "  -Uninstall [-Purge]   remove CAPIE; -Purge also removes the log and Steam config backups`n",
        "  -CheckUpdate          report the newest release (one request to GitHub)`n",
        "Add -Yes to run without questions (-CloseSteam lets it close and restart Steam), -DryRun to see the`n",
        "plan first. Interactive console: run with no switches for the menu, or with -Plain for plain`n",
        'questions.'
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
        'Steam is still running. Close Steam completely (Steam menu > Exit), then press Enter. Type Q to skip the ',
        'Steam step.'
    ) -join ''
    SteamRunningNonInteractive = @(
        'Steam is running, so the launch option was not changed. Close Steam and run the script again, or add ',
        '-CloseSteam to let this script close Steam (steam.exe -shutdown) and start it again afterwards.'
    ) -join ''
    AskCloseSteam              = 'Steam is running. Close Steam now so the launch option can be changed? [Y/n]'
    AskRestartSteam            = 'Start Steam again? [Y/n]'
    SteamClosing               = 'Asking Steam to exit (steam.exe -shutdown). Nothing is forced.'
    SteamWaiting               = 'Waiting for Steam to exit: {0} of {1} s.'
    SteamClosed                = 'Steam has exited.'
    SteamCloseTimeout          = @(
        'Steam did not exit within {0} s. Nothing was forced. Close Steam yourself (Steam menu > Exit) or skip ',
        'the Steam step.'
    ) -join ''
    SteamExeMissing            = 'steam.exe was not found in the registry, so this script cannot close Steam.'
    SteamStarted               = 'Steam is starting.'
    AskAccount                 = 'Several Steam accounts found. Type a number, several numbers separated by commas, or A for all:'
    Done                       = 'Done.'
    DryRunNote                 = 'DRY RUN: nothing will be written.'
    ShortcutComment            = 'HOI4 with CAPIE, the native extension used by Rename Anything. Not declared multiplayer-compatible.'
    NotInstalled               = 'The launcher is not installed yet. Run with -Install first.'
    UninstallDone              = 'Uninstalled. Workshop mods and saves were not touched. The log was kept: {0}'
    Failed                     = 'FAILED: {0}'
    UpdateStatus               = 'Installed version: {0}. Latest release: {1}.'
    UpdateAvailable            = 'Version {0} is available. Run with -Update to install it; the current version stays for -Rollback.'
    AlreadyUpToDate            = 'CAPIE {0} is already up to date. Repairing instead: the CAPIE switch mod and, if chosen, the Steam launch option.'
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
    TuiSetup                   = 'Install or update'
    TuiSetupHint               = 'installs CAPIE, or updates to the newest signed release (the current one stays for rollback)'
    TuiOlder                   = 'Install an older version'
    TuiOlderHint               = 'choose a signed release from GitHub; option 1 returns to the newest'
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
    TuiAskSteamClosed          = 'Steam is still running. Close it completely (Steam > Exit), then press Enter. Q skips the Steam step.'
    TuiAskSteamRepair          = 'Set the CAPIE launcher in the HOI4 Steam launch options again (repair)? [y/N]'
    TuiOlderTitle              = 'Which version should be installed?'
    TuiOlderKeys               = 'Up and Down or 1-9 to choose, Enter to install, Esc to go back.'
    TuiOlderLoading            = 'Listing signed releases on GitHub (one request).'
    TuiOlderInstall            = 'Install CAPIE {0}? It replaces the active version, which stays for rollback. [Y/n]'
    VersionsTitle              = 'Signed CAPIE releases, newest first:'
    VersionsNone               = 'No signed releases were found on GitHub.'
    VersionsHint               = 'Install one with -Version x.y.z. -Update returns to the newest.'
    VersionFacts               = 'API {0} | HoI4 {1} | {2}'
    VersionUnknown             = 'unknown'
    VersionMarkInstalled       = 'installed'
    VersionMarkLatest          = 'latest'
    VersionMarkPrerelease      = 'pre-release'
    VersionGameWarning         = '! not for your hoi4.exe ({0}...): the game would start without CAPIE'
    VersionNotFound            = 'Version {0} is not among the signed releases on GitHub (see -ListVersions).'
    VersionManifestFacts       = 'Release {0}: CAPIE script API level {1}, made for HoI4 {2}.'
    VersionUnsupported         = @(
        'Release {0} does not support your hoi4.exe build ({1}...). It is installed anyway; the launcher starts ',
        'the game without CAPIE until a matching version is active.'
    ) -join ''
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
        The console line is skipped with -NoConsole or when $script:RaQuiet is set. While the
        terminal interface runs, the line goes to $script:RaLogSink (status-word rendering).
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
    if (-not $NoConsole -and -not $script:RaQuiet) {
        if ($script:RaLogSink) { & $script:RaLogSink $Level $Message } else { Write-RaHost $Message -Color $script:RaLogColors[$Level] }
    }
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
        Question text.
    .OUTPUTS
        System.String
    .EXAMPLE
        $mode = Read-RaAnswer (Get-RaText 'AskMode')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Prompt)
    $answer = & $script:RaReadHost $Prompt
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
        (AccountId, ConfigPath, OriginalValue, WrittenValue, LauncherPath), ModFiles (Path, Sha256,
        Backup) of the "CAPIE" switch mod in the player's mod folder.
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
        LoadMethod = 'Launcher'; Proxy = $null; ProxyActivation = 'Flag'; ProxyConfigSha256 = $null; ModFiles = @()
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
            if ($j.PSObject.Properties['ModFiles']) { $state.ModFiles = @($j.ModFiles | Where-Object { $_ }) }
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
        ModFiles         = @($State.ModFiles)
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
    $data = [ordered]@{
        schema = $script:RaManifestSchemaVersion; version = $Manifest.Version; zipName = $Manifest.ZipName; zipSha256 = $Manifest.ZipSha256
        files = [pscustomobject]$Manifest.Files; supportedGameSha256 = @($Manifest.SupportedGameSha256)
    }
    if ($Manifest.Contains('ApiLevel') -and $Manifest.ApiLevel) { $data['capie_api_level'] = [int]$Manifest.ApiLevel }
    if ($Manifest.Contains('GameVersion') -and $Manifest.GameVersion) { $data['game_version'] = [string]$Manifest.GameVersion }
    if ($Manifest.Contains('ChangelogSummary') -and @($Manifest.ChangelogSummary).Count -gt 0) { $data['changelog_summary'] = @($Manifest.ChangelogSummary) }
    return ([pscustomobject]$data | ConvertTo-Json -Depth $script:RaJsonDepth)
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
    # Optional display data (version picker); older manifests do not have it.
    $m['ApiLevel'] = if ($j.PSObject.Properties['capie_api_level']) { [int]$j.capie_api_level } else { $null }
    $m['GameVersion'] = if ($j.PSObject.Properties['game_version']) { [string]$j.game_version } else { $null }
    $m['ChangelogSummary'] = if ($j.PSObject.Properties['changelog_summary']) { @($j.changelog_summary | ForEach-Object { [string]$_ }) } else { @() }
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
# Steam discovery (install folder, accounts, game folder, process) and, on request, closing
# and restarting Steam the official way (steam.exe -shutdown; never killed). ADR-0037.
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

function Test-RaSteamAnyProcess {
    <#
    .SYNOPSIS
        Tests whether steam.exe or one of its steamwebhelper processes still runs.
    .DESCRIPTION
        Steam writes localconfig.vdf when it exits; the edit waits until every Steam process
        is gone.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaSteamAnyProcess
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    return ((@(& $script:RaGetSteamProcess).Count + @(& $script:RaGetSteamHelperProcess).Count) -gt 0)
}

function Get-RaSteamExe {
    <#
    .SYNOPSIS
        Path of steam.exe from the registry, or $null.
    .DESCRIPTION
        HKCU\Software\Valve\Steam\SteamExe first, then SteamPath\steam.exe. Only an existing
        file is returned.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSteamExe
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $candidates = @()
    foreach ($name in $script:RaSteamExeRegistryValue, $script:RaSteamRegistryValue) {
        try {
            $value = [string](& $script:RaGetRegistryValue $script:RaSteamRegistryKey $name)
            if (-not $value) { continue }
            $value = $value -replace '/', '\'
            $candidates += if ($name -eq $script:RaSteamRegistryValue) { Join-Path $value $script:RaSteamExeName } else { $value }
        } catch { Write-Verbose "Registry value $name unavailable: $($_.Exception.Message)" }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    return $null
}

function Stop-RaSteam {
    <#
    .SYNOPSIS
        Asks Steam to exit the official way and waits until it is gone.
    .DESCRIPTION
        Runs "steam.exe -shutdown" (Steam's own clean exit) and polls every second, for at most
        60 s, until steam.exe and steamwebhelper are gone, logging progress every 5 s. Nothing is
        killed: when Steam stays, the caller falls back to asking the player. Dry-run mode only
        logs the plan and reports success so the rest of the plan can be shown.
    .OUTPUTS
        System.Boolean ($true when Steam is closed)
    .EXAMPLE
        if (Stop-RaSteam) { 'edit localconfig.vdf' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    $exe = Get-RaSteamExe
    if (-not $exe) {
        Write-RaLog (Get-RaText 'SteamExeMissing') -Level WARN
        return $false
    }
    if ($script:RaDry) {
        $plan = ("[dry-run] would run `"{0}`" {1}, wait up to {2} s and offer to start Steam again" -f
            $exe, $script:RaSteamShutdownArgument, $script:RaSteamShutdownTimeoutSeconds)
        Write-RaLog $plan -Level PLAN
        $script:RaSteamClosedByUs = $true
        $script:RaSteamRestartExe = $exe
        return $true
    }
    Write-RaLog (Get-RaText 'SteamClosing')
    & $script:RaStartProcess $exe @($script:RaSteamShutdownArgument)
    for ($waited = 0; $waited -lt $script:RaSteamShutdownTimeoutSeconds; $waited++) {
        if (-not (Test-RaSteamAnyProcess)) {
            $script:RaSteamClosedByUs = $true
            $script:RaSteamRestartExe = $exe
            Write-RaLog (Get-RaText 'SteamClosed')
            return $true
        }
        if ($waited -gt 0 -and $waited % $script:RaSteamProgressEverySeconds -eq 0) {
            Write-RaLog (Get-RaText 'SteamWaiting' @($waited, $script:RaSteamShutdownTimeoutSeconds))
        }
        & $script:RaSleep $script:RaSteamShutdownPollMilliseconds
    }
    if (-not (Test-RaSteamAnyProcess)) {
        $script:RaSteamClosedByUs = $true
        $script:RaSteamRestartExe = $exe
        Write-RaLog (Get-RaText 'SteamClosed')
        return $true
    }
    Write-RaLog (Get-RaText 'SteamCloseTimeout' @($script:RaSteamShutdownTimeoutSeconds)) -Level WARN
    return $false
}

function Start-RaSteamAgain {
    <#
    .SYNOPSIS
        Starts Steam again after this run closed it.
    .DESCRIPTION
        Same steam.exe, no arguments, no waiting. Does nothing when this run did not close
        Steam. Dry-run mode only logs the plan.
    .OUTPUTS
        None.
    .EXAMPLE
        Start-RaSteamAgain
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param()
    if (-not $script:RaSteamClosedByUs -or -not $script:RaSteamRestartExe) { return }
    $exe = $script:RaSteamRestartExe
    $script:RaSteamClosedByUs = $false
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would start `"$exe`"" -Level PLAN
        return
    }
    & $script:RaStartProcess $exe @()
    Write-RaLog (Get-RaText 'SteamStarted')
}

function Wait-RaSteamClosed {
    <#
    .SYNOPSIS
        Makes sure Steam is closed before localconfig.vdf is edited.
    .DESCRIPTION
        Returns $true when Steam is not running. Interactively it first offers to close Steam
        (steam.exe -shutdown, see Stop-RaSteam; default yes); when that is declined or Steam
        does not exit in time, it asks the player to close Steam until it is closed or the
        player types Q. With -Yes it closes Steam only when -CloseSteam was given, and otherwise
        returns $false at once (and logs why). Steam is never killed.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Wait-RaSteamClosed)) { return $false }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    if ($script:RaDry -and $script:RaSteamClosedByUs) { return $true }
    if (-not (Test-RaSteamRunning)) { return $true }
    if ($script:RaAssumeYes) {
        if ($script:RaCloseSteam) { return (Stop-RaSteam) }
        Write-RaLog (Get-RaText 'SteamRunningNonInteractive') -Level WARN
        return $false
    }
    if ((Confirm-Ra (Get-RaText 'AskCloseSteam') $true) -and (Stop-RaSteam)) { return $true }
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
        Write-RaLog "Package $($Manifest.ZipName): SHA256 matches the manifest."
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

#region Shared/Tui.ps1
# ---------------------------------------------------------------------------
# Terminal interface: capability detection and rendering (shared building blocks).
#
# Visual direction (docs/design/DESIGN-SYSTEM.md, "The Staff Annotation"): a recessed
# field-ink panel, a small staff map sheet with a grid, a flag planted in square C3, and
# the pencil writing the mod's name into the margin next to it. The only motion is the
# pencil writing, one character per frame; the resting frame is complete. Status lines use
# the CLI pattern (OK, ASK, SKIP, NOTE, FAIL) and read the same without colour.
#
# Everything here is pure (returns strings or line models) except the few Write/Read
# helpers at the end, which go through the adapters in Adapters.ps1.
# A line model is an array of segments; a segment is @{ S = '<token name>'; T = '<text>' }.
# ---------------------------------------------------------------------------

# Map sheet: grid letters, frame, six rows. The flag (#==) stands on the pole in C3.
$script:RaTuiMapHeader = '     A     B     C     D     E     F'
$script:RaTuiMapFrame = '  +-----+-----+-----+-----+-----+-----+'
$script:RaTuiMapRows = @(
    ' ~ ~ ~ ~ ~ ~ ~ ~ ______ ~ ~ ~ ~ ~ ~',
    ' ~ ~ ~ ~ ~ ~ ___/      \___ ~ ~ ~ ~',
    ' ~ ~ ~ ~ __/  |##==        \_ ~ ~ ~',
    ' ~ ~ ~ _/     |              \ ~ ~ ',
    ' ~ ~ ~ \__    |          ____/ ~ ~ ',
    ' ~ ~ ~ ~ ~\___|_________/ ~ ~ ~ ~ ~'
)
# Character classes of the map and their tokens.
$script:RaTuiMapStyles = [ordered]@{ '~' = 'ash'; '#' = 'header-amber'; '=' = 'header-amber'; '|' = 'steel-bevel' }
$script:RaTuiCoastStyle = 'khaki'
$script:RaTuiFrameStyle = 'steel-bevel'

function New-RaTuiSegment {
    <#
    .SYNOPSIS
        Creates one styled text segment of a line model.
    .DESCRIPTION
        S is a colour token name from $script:RaTuiPalette, T the text.
    .PARAMETER Style
        Token name.
    .PARAMETER Text
        Text.
    .OUTPUTS
        System.Collections.Hashtable
    .EXAMPLE
        New-RaTuiSegment 'khaki' 'Install'
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Style,
        [Parameter(Position = 1)][AllowEmptyString()][string]$Text = ''
    )
    return @{ S = $Style; T = $Text }
}

function Test-RaActionOption {
    <#
    .SYNOPSIS
        Tests whether the options name an action (install, update, disable, uninstall...).
    .DESCRIPTION
        Without an action an interactive run opens the menu or the plain questions; a
        non-interactive run prints the usage text and stops.
    .PARAMETER Options
        Option table (missing keys mean "not given").
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaActionOption @{ Install = $true }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options)
    foreach ($name in $script:RaActionOptionNames) { if ($Options.ContainsKey($name) -and $Options[$name]) { return $true } }
    return $false
}

function Get-RaTuiMode {
    <#
    .SYNOPSIS
        Decides how the run talks to the user: terminal interface or plain prompts.
    .DESCRIPTION
        The interface opens only for an interactive console when no action switch, -Yes or
        -Plain was given. Colour needs a virtual-terminal host and is off with NO_COLOR or
        TERM=dumb. Animation also needs colour and is off with -NoAnimation,
        RA_NO_ANIMATION (any value except 0) or CI. Width is the console width minus one,
        clamped to 44..78.
    .PARAMETER Options
        Normalized option table.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Tui, Interactive, Color, Vt, Animate, Width)
    .EXAMPLE
        $mode = Get-RaTuiMode $options
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options)
    $terminal = & $script:RaGetTerminal
    $action = Test-RaActionOption $Options
    $noColor = [bool](& $script:RaGetEnv $script:RaTuiEnvNoColor) -or ((& $script:RaGetEnv $script:RaTuiEnvTerm) -eq $script:RaTuiDumbTerm)
    $noAnimationEnv = & $script:RaGetEnv $script:RaTuiEnvNoAnimation
    $noAnimation = ([bool]$noAnimationEnv -and $noAnimationEnv -ne '0') -or [bool](& $script:RaGetEnv $script:RaTuiEnvCi) -or [bool]$Options['NoAnimation']
    $tui = [bool]$terminal.Interactive -and -not $action -and -not $Options['Yes'] -and -not $Options['Plain']
    $color = $tui -and [bool]$terminal.Vt -and -not $noColor
    $width = [int]$terminal.Width - 1
    if ($width -lt 1) { $width = $script:RaTuiFallbackWidth }
    $width = [Math]::Max($script:RaTuiMinWidth, [Math]::Min($script:RaTuiMaxWidth, $width))
    return [pscustomobject]@{
        Tui = $tui; Interactive = [bool]$terminal.Interactive; Color = $color; Vt = ($tui -and [bool]$terminal.Vt)
        Animate = ($color -and -not $noAnimation); Width = $width
    }
}

function ConvertTo-RaTuiSgr {
    <#
    .SYNOPSIS
        ANSI 24-bit colour sequence for a palette token.
    .DESCRIPTION
        Foreground by default, background with -Background.
    .PARAMETER Style
        Token name from $script:RaTuiPalette.
    .PARAMETER Background
        Background instead of foreground.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaTuiSgr 'parchment'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][string]$Style, [switch]$Background)
    $hex = $script:RaTuiPalette[$Style]
    if (-not $hex) { throw "Unknown palette token: $Style" }
    $r = [Convert]::ToInt32($hex.Substring(1, 2), 16)
    $g = [Convert]::ToInt32($hex.Substring(3, 2), 16)
    $b = [Convert]::ToInt32($hex.Substring(5, 2), 16)
    $kind = if ($Background) { 48 } else { 38 }
    return ('{0}[{1};2;{2};{3};{4}m' -f [char]27, $kind, $r, $g, $b)
}

function Format-RaTuiLine {
    <#
    .SYNOPSIS
        Renders one line model as a string for the console.
    .DESCRIPTION
        With colour: panel background, indent, each segment in its colour, padded to the
        panel width, then a reset. Without colour: indent and plain text, trailing blanks
        removed. Text beyond the panel width is cut (callers wrap first).
    .PARAMETER Segments
        Line model.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .PARAMETER Background
        Panel token for this line (default: the panel background).
    .OUTPUTS
        System.String
    .EXAMPLE
        Format-RaTuiLine @((New-RaTuiSegment 'khaki' 'x')) $mode
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Segments,
        [Parameter(Mandatory, Position = 1)][psobject]$Mode,
        [string]$Background = $script:RaTuiBackground
    )
    $room = $Mode.Width - $script:RaTuiIndent
    $plain = ''
    $sb = New-Object System.Text.StringBuilder
    foreach ($segment in $Segments) {
        $text = [string]$segment.T
        if ($plain.Length + $text.Length -gt $room) { $text = $text.Substring(0, [Math]::Max(0, $room - $plain.Length)) }
        $plain += $text
        if ($Mode.Color) { [void]$sb.Append((ConvertTo-RaTuiSgr $segment.S)).Append($text) }
    }
    $indent = ' ' * $script:RaTuiIndent
    if (-not $Mode.Color) { return ($indent + $plain).TrimEnd() }
    $pad = ' ' * ($room - $plain.Length)
    return (ConvertTo-RaTuiSgr $Background -Background) + $indent + $sb.ToString() + $pad + ('{0}[0m' -f [char]27)
}

function Split-RaTuiText {
    <#
    .SYNOPSIS
        Word-wraps text to a width.
    .DESCRIPTION
        Breaks at spaces; a word longer than the width (a path, a hash) is cut into pieces.
        Explicit line breaks are kept. Always returns at least one (possibly empty) line.
    .PARAMETER Text
        Text.
    .PARAMETER Width
        Maximum characters per line (at least 1).
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Split-RaTuiText 'a long sentence' 6
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Text,
        [Parameter(Mandatory, Position = 1)][ValidateRange(1, 1000)][int]$Width
    )
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($paragraph in ([string]$Text).Replace("`r`n", "`n").Split("`n")) {
        $current = ''
        foreach ($word in $paragraph.Split(' ')) {
            if ($word -eq '') { continue }
            while ($word.Length -gt $Width) {
                if ($current) { $lines.Add($current); $current = '' }
                $lines.Add($word.Substring(0, $Width))
                $word = $word.Substring($Width)
            }
            if (-not $current) { $current = $word }
            elseif ($current.Length + 1 + $word.Length -le $Width) { $current += ' ' + $word }
            else { $lines.Add($current); $current = $word }
        }
        $lines.Add($current)
    }
    return $lines.ToArray()
}

function Get-RaTuiStatusModel {
    <#
    .SYNOPSIS
        Line models of one status line in the CLI pattern ("OK    Package verified").
    .DESCRIPTION
        The status word is padded to a fixed column and coloured by $script:RaTuiStatusStyles;
        the text wraps under itself with a hanging indent.
    .PARAMETER Word
        OK, ASK, SKIP, NOTE or FAIL.
    .PARAMETER Text
        Message.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiStatusModel -Word 'OK' -Text 'CAPIE launcher installed' -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateSet('OK', 'ASK', 'SKIP', 'NOTE', 'FAIL')][string]$Word,
        [Parameter(Position = 1)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    $room = $Mode.Width - $script:RaTuiIndent - $script:RaTuiStatusWidth
    $textStyle = if ($Word -in @('SKIP', 'NOTE')) { 'khaki' } else { 'parchment' }
    $result = @()
    $first = $true
    foreach ($line in (Split-RaTuiText $Text $room)) {
        $head = if ($first) { $Word.PadRight($script:RaTuiStatusWidth) } else { ' ' * $script:RaTuiStatusWidth }
        $result += , @((New-RaTuiSegment $script:RaTuiStatusStyles[$Word] $head), (New-RaTuiSegment $textStyle $line))
        $first = $false
    }
    return , $result
}

function Get-RaTuiTextModel {
    <#
    .SYNOPSIS
        Line models of a wrapped paragraph in one colour.
    .DESCRIPTION
        Used for headings, hints and notes.
    .PARAMETER Text
        Text.
    .PARAMETER Style
        Token name.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiTextModel -Text 'What should this setup do?' -Style 'parchment' -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][string]$Style,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    $result = @()
    foreach ($line in (Split-RaTuiText $Text ($Mode.Width - $script:RaTuiIndent))) { $result += , @((New-RaTuiSegment $Style $line)) }
    return , $result
}

function Get-RaTuiMapModel {
    <#
    .SYNOPSIS
        Line models of the staff map sheet.
    .DESCRIPTION
        Grid letters and row numbers in ash, frame in steel, sea in ash, coast in khaki, the
        pole in steel and the flag in header amber (the one highlighted target of the view).
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiMapModel
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param()
    $result = @(, @((New-RaTuiSegment 'ash' $script:RaTuiMapHeader)))
    $result += , @((New-RaTuiSegment $script:RaTuiFrameStyle $script:RaTuiMapFrame))
    $row = 1
    foreach ($inner in $script:RaTuiMapRows) {
        $segments = @((New-RaTuiSegment 'ash' "$row "), (New-RaTuiSegment $script:RaTuiFrameStyle '|'))
        $runStyle = $null
        $run = ''
        foreach ($ch in $inner.ToCharArray()) {
            $key = [string]$ch
            $style = $script:RaTuiCoastStyle
            if ($ch -eq ' ') { $style = $runStyle } elseif ($script:RaTuiMapStyles.Contains($key)) { $style = $script:RaTuiMapStyles[$key] }
            if (-not $style) { $style = 'ash' }
            if ($style -ne $runStyle -and $run) { $segments += New-RaTuiSegment $runStyle $run; $run = '' }
            $runStyle = $style
            $run += $key
        }
        if ($run) { $segments += New-RaTuiSegment $runStyle $run }
        $segments += New-RaTuiSegment $script:RaTuiFrameStyle '|'
        $result += , $segments
        $row++
    }
    $result += , @((New-RaTuiSegment $script:RaTuiFrameStyle $script:RaTuiMapFrame))
    return , $result
}

function Get-RaTuiAnnotationLine {
    <#
    .SYNOPSIS
        Line model of the margin note: grid square, the name written so far, the pencil.
    .DESCRIPTION
        Frame k of the splash shows the first k characters; the last frame shows the full
        name with the pencil resting after it.
    .PARAMETER Count
        Characters written (clamped to the name length).
    .OUTPUTS
        System.Object[] (line model)
    .EXAMPLE
        Get-RaTuiAnnotationLine 7
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Mandatory, Position = 0)][int]$Count)
    $name = $script:RaTuiAnnotationName
    $written = $name.Substring(0, [Math]::Max(0, [Math]::Min($Count, $name.Length)))
    return , @(
        (New-RaTuiSegment 'ash' "$($script:RaTuiAnnotationRef)   "),
        (New-RaTuiSegment 'parchment' $written),
        (New-RaTuiSegment 'pencil-green-lit' $script:RaTuiPencil)
    )
}

function Get-RaTuiMenuModel {
    <#
    .SYNOPSIS
        Line models of a selectable list (by default the action menu).
    .DESCRIPTION
        One row per item: number, name, hint lines (each long hint wraps under itself). The
        selected row is marked with ">" (so it reads without colour) and drawn on the olive
        button background.
    .PARAMETER Selected
        Zero-based index of the selected item.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .PARAMETER Items
        Items (Name, Hints); default: the action menu (Get-RaTuiMenuItem).
    .OUTPUTS
        System.Object[] (pscustomobject rows: Segments, Background)
    .EXAMPLE
        Get-RaTuiMenuModel 0 $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][int]$Selected,
        [Parameter(Mandatory, Position = 1)][psobject]$Mode,
        [Parameter(Position = 2)][AllowNull()][object[]]$Items = $null
    )
    if ($null -eq $Items) { $Items = Get-RaTuiMenuItem }
    $result = @()
    $nameWidth = 0
    foreach ($item in $Items) { $nameWidth = [Math]::Max($nameWidth, ([string]$item.Name).Length) }
    $numberWidth = ([string]$Items.Count).Length
    for ($i = 0; $i -lt $Items.Count; $i++) {
        $item = $Items[$i]
        $on = ($i -eq $Selected)
        $marker = if ($on) { '> ' } else { '  ' }
        $head = '{0}{1}  {2}  ' -f $marker, ([string]($i + 1)).PadLeft($numberWidth), ([string]$item.Name).PadRight($nameWidth)
        $hintRoom = [Math]::Max(1, $Mode.Width - $script:RaTuiIndent - $head.Length)
        $nameStyle = if ($on) { 'parchment' } else { 'khaki' }
        $background = if ($on) { $script:RaTuiSelectedBackground } else { $script:RaTuiBackground }
        $first = $true
        foreach ($hintText in @($item.Hints)) {
            foreach ($hint in (Split-RaTuiText ([string]$hintText) $hintRoom)) {
                $lead = if ($first) { $head } else { ' ' * $head.Length }
                $result += [pscustomobject]@{
                    Segments   = @((New-RaTuiSegment $nameStyle $lead), (New-RaTuiSegment 'ash' $hint))
                    Background = $background
                }
                $first = $false
            }
        }
    }
    return , $result
}

function Get-RaTuiMenuItem {
    <#
    .SYNOPSIS
        Items of the action menu.
    .DESCRIPTION
        One item (Name, Hints) per entry of $script:RaTuiMenuActions, texts from Messages.
    .OUTPUTS
        System.Object[] (pscustomobject: Name, Hints)
    .EXAMPLE
        Get-RaTuiMenuItem
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param()
    $items = @()
    foreach ($action in $script:RaTuiMenuActions) {
        $items += [pscustomobject]@{ Name = (Get-RaText "Tui$action"); Hints = @(Get-RaText "Tui$($action)Hint") }
    }
    return , $items
}

function Get-RaTuiSummaryModel {
    <#
    .SYNOPSIS
        Line models of the summary: a key column and a value column under a hairline.
    .DESCRIPTION
        Keys in ash, values in parchment, values wrap under themselves.
    .PARAMETER Rows
        Array of two-element arrays (key, value).
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiSummaryModel @(, @('Log', 'C:\x\bootstrap.log')) $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Mandatory, Position = 0)][object[]]$Rows, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $room = $Mode.Width - $script:RaTuiIndent - $script:RaTuiKeyWidth
    $result = @(, @((New-RaTuiSegment 'parchment' (Get-RaText 'TuiSummary'))))
    $result += , @((New-RaTuiSegment 'steel-bevel' ('-' * ($Mode.Width - $script:RaTuiIndent))))
    foreach ($row in $Rows) {
        $first = $true
        foreach ($line in (Split-RaTuiText ([string]$row[1]) $room)) {
            $key = if ($first) { ([string]$row[0]).PadRight($script:RaTuiKeyWidth) } else { ' ' * $script:RaTuiKeyWidth }
            $result += , @((New-RaTuiSegment 'ash' $key), (New-RaTuiSegment 'parchment' $line))
            $first = $false
        }
    }
    return , $result
}

function Write-RaTuiModel {
    <#
    .SYNOPSIS
        Writes line models to the console through the raw-output adapter.
    .DESCRIPTION
        Accepts plain line models or menu rows (objects with Segments and Background).
    .PARAMETER Lines
        Line models.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaTuiModel (Get-RaTuiMapModel) $mode
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Lines, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $sb = New-Object System.Text.StringBuilder
    foreach ($line in $Lines) {
        if ($line -is [psobject] -and $line.PSObject.Properties['Segments']) {
            [void]$sb.Append((Format-RaTuiLine @($line.Segments) $Mode -Background $line.Background)).Append("`n")
        } else {
            [void]$sb.Append((Format-RaTuiLine @($line) $Mode)).Append("`n")
        }
    }
    & $script:RaWriteRaw $sb.ToString()
}

function Write-RaTuiBlank {
    <#
    .SYNOPSIS
        Writes one empty panel line.
    .DESCRIPTION
        Keeps the panel background continuous between groups.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaTuiBlank $mode
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    Write-RaTuiModel @(, @()) $Mode
}

function Show-RaTuiSplash {
    <#
    .SYNOPSIS
        Draws the map sheet and lets the pencil write the name into the margin.
    .DESCRIPTION
        With animation the margin line is redrawn in place (carriage return) one character
        per frame, then held still. Without animation only the final frame is drawn. The
        endorsement line follows in both cases.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Int32 (number of frames drawn for the margin line)
    .EXAMPLE
        [void](Show-RaTuiSplash $mode)
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    Write-RaTuiBlank $Mode
    Write-RaTuiModel (Get-RaTuiMapModel) $Mode
    Write-RaTuiBlank $Mode
    $frames = 0
    $total = $script:RaTuiAnnotationName.Length
    if ($Mode.Animate) {
        for ($k = 0; $k -lt $total; $k++) {
            & $script:RaWriteRaw ("`r" + (Format-RaTuiLine (Get-RaTuiAnnotationLine $k) $Mode))
            & $script:RaSleep $script:RaTuiFrameDelayMs
            $frames++
        }
        & $script:RaWriteRaw ("`r" + (Format-RaTuiLine (Get-RaTuiAnnotationLine $total) $Mode) + "`n")
        $frames++
        & $script:RaSleep $script:RaTuiHoldDelayMs
    } else {
        Write-RaTuiModel @(, (Get-RaTuiAnnotationLine $total)) $Mode
        $frames++
    }
    $tag = Get-RaText 'TuiTagline'
    $at = $tag.IndexOf('CAPIE')
    $refPad = ' ' * ($script:RaTuiAnnotationRef.Length + 3)
    Write-RaTuiModel @(, @(
            (New-RaTuiSegment 'ash' ($refPad + $tag.Substring(0, $at))),
            (New-RaTuiSegment 'brass' 'CAPIE'),
            (New-RaTuiSegment 'ash' $tag.Substring($at + 5))
        )) $Mode
    Write-RaTuiBlank $Mode
    return $frames
}

function Read-RaTuiKey {
    <#
    .SYNOPSIS
        Reads one key and names it.
    .DESCRIPTION
        Returns Up, Down, Enter, Escape, or the typed character in lower case.
    .OUTPUTS
        System.String
    .EXAMPLE
        $key = Read-RaTuiKey
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $info = & $script:RaReadKey
    switch ([string]$info.Key) {
        'UpArrow' { return 'Up' }
        'DownArrow' { return 'Down' }
        'Enter' { return 'Enter' }
        'Escape' { return 'Escape' }
    }
    return ([string]$info.KeyChar).ToLowerInvariant()
}

function Select-RaTuiIndex {
    <#
    .SYNOPSIS
        Shows a selectable list and returns the index of the chosen item, or -1.
    .DESCRIPTION
        Up/Down move the selection, digits 1-9 pick directly, Enter confirms, Esc or Q
        returns -1. On a virtual-terminal host the list is redrawn in place (cursor up);
        otherwise it is printed again below, and a digit confirms at once.
    .PARAMETER Items
        Items (Name, Hints).
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Int32
    .EXAMPLE
        $index = Select-RaTuiIndex (Get-RaTuiMenuItem) $mode
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][object[]]$Items, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $selected = 0
    $count = $Items.Count
    $model = Get-RaTuiMenuModel -Selected $selected -Mode $Mode -Items $Items
    Write-RaTuiModel $model $Mode
    while ($true) {
        $key = Read-RaTuiKey
        $before = $selected
        if ($key -eq 'Up') { $selected = ($selected + $count - 1) % $count }
        elseif ($key -eq 'Down') { $selected = ($selected + 1) % $count }
        elseif ($key -eq 'Enter') { return $selected }
        elseif ($key -eq 'Escape' -or $key -eq $script:RaQuitAnswer) { return -1 }
        elseif ($key -match '^[1-9]$' -and [int]$key -le $count) {
            $selected = [int]$key - 1
            if (-not $Mode.Vt) { return $selected }
        }
        if ($selected -ne $before) {
            if ($Mode.Vt) { & $script:RaWriteRaw ('{0}[{1}A' -f [char]27, @($model).Count) } else { Write-RaTuiBlank $Mode }
            $model = Get-RaTuiMenuModel -Selected $selected -Mode $Mode -Items $Items
            Write-RaTuiModel $model $Mode
        }
    }
}

function Select-RaTuiAction {
    <#
    .SYNOPSIS
        Shows the menu and returns the chosen action.
    .DESCRIPTION
        Select-RaTuiIndex over the action menu; Esc or Q means Quit.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.String (Setup, Older, Disable, Uninstall or Quit)
    .EXAMPLE
        $action = Select-RaTuiAction $mode
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    $index = Select-RaTuiIndex (Get-RaTuiMenuItem) $Mode
    if ($index -lt 0) { return 'Quit' }
    return $script:RaTuiMenuActions[$index]
}

function Confirm-RaTui {
    <#
    .SYNOPSIS
        Asks a yes/no question as an ASK line and reads one key.
    .DESCRIPTION
        Y answers yes, N answers no, Enter takes the default, Esc answers no. The answer is
        echoed after the question so the transcript stays readable.
    .PARAMETER Prompt
        Question (ends with the default in brackets).
    .PARAMETER Default
        Answer for Enter.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Confirm-RaTui -Prompt (Get-RaText 'TuiAskSteam') -Default $true -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Prompt,
        [Parameter(Mandatory, Position = 1)][bool]$Default,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    Write-RaTuiModel (Get-RaTuiStatusModel -Word 'ASK' -Text $Prompt -Mode $Mode) $Mode
    while ($true) {
        $key = Read-RaTuiKey
        $answer = $null
        if ($key -eq 'Enter') { $answer = $Default }
        elseif ($key -eq 'Escape') { $answer = $false }
        elseif ($key -eq 'y') { $answer = $true }
        elseif ($key -eq 'n') { $answer = $false }
        if ($null -ne $answer) {
            $echo = if ($answer) { Get-RaText 'TuiYes' } else { Get-RaText 'TuiNo' }
            Write-RaTuiModel @(, @((New-RaTuiSegment 'ash' ((' ' * $script:RaTuiStatusWidth) + $echo)))) $Mode
            return [bool]$answer
        }
    }
}
#endregion Shared/Tui.ps1

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

function Install-RaLatest {
    <#
    .SYNOPSIS
        Installs and activates the newest signed release from GitHub Releases (-Install -Latest).
    .DESCRIPTION
        The path of the release installer: one GitHub API request for the latest release, its
        release-manifest.json and .sig (RSA signature checked with the key pinned in this
        script, tag = manifest version, asset URLs inside this repository), then the ZIP: its
        SHA-256 before extraction, every file's SHA-256 after extraction, and a full
        re-verification before the version folder is moved into place under
        %LOCALAPPDATA%\RenameAnything. Nothing is written to the game folder. Offline:
        -ManifestPath (with the .sig next to it) and -PackagePath.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER LocalManifestPath
        Signed release-manifest.json for an offline install, or empty.
    .PARAMETER LocalZip
        Release ZIP for an offline install, or empty.
    .OUTPUTS
        System.String (the installed version)
    .EXAMPLE
        Install-RaLatest -State $state -LocalManifestPath $null -LocalZip $null
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalManifestPath,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$LocalZip
    )
    $remote = Get-RaRemoteRelease $LocalManifestPath
    $m = $remote.Manifest
    [void](Install-RaVersion -Manifest $m -LocalZip $LocalZip -ZipUrl $remote.ZipUrl)
    Set-RaActiveVersion $State $m.Version
    return [string]$m.Version
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
    Write-RaLog (Get-RaText 'ReleaseVerified' @($m.Version))
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

#region Features/Versions.ps1
# ---------------------------------------------------------------------------
# Slice: older versions (-ListVersions, -Version, menu "Install an older version").
# One GitHub request lists the newest releases; only the chosen release's signed manifest
# is downloaded, and it goes through the same verification as the newest release
# (signature with the pinned key, tag = manifest version, asset URLs inside this repository,
# ZIP and per-file SHA-256). The list itself shows what the release notes say; the
# manifest fields capie_api_level, game_version and changelog_summary are shown once the
# chosen manifest is verified.
# ---------------------------------------------------------------------------

function ConvertFrom-RaReleaseNote {
    <#
    .SYNOPSIS
        Reads the facts the release notes state about a release.
    .DESCRIPTION
        The notes are written by tools/release (build_notes): the changelog section of the
        version, then "## Supported game build" with the game version, the hoi4.exe SHA-256
        and the CAPIE script API level; hand-written notes may shorten the hash
        ("7DC947BE...31D5"), then only its start is kept. Missing facts are $null; the summary
        is the first list items before that section, without the item naming the game build.
    .PARAMETER Body
        Release notes (Markdown) or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (GameVersion, GameSha256 (full or start), ApiLevel, Summary)
    .NOTES
        Summary items: list markers and Markdown emphasis are removed.
    .EXAMPLE
        ConvertFrom-RaReleaseNote $release.body
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Body)
    $text = [string]$Body
    $facts = [pscustomobject]@{ GameVersion = $null; GameSha256 = $null; ApiLevel = $null; Summary = @() }
    $m = [regex]::Match($text, $script:RaNotesGameVersionPattern)
    if ($m.Success) { $facts.GameVersion = $m.Groups[1].Value }
    $m = [regex]::Match($text, $script:RaNotesGameShaPattern)
    if ($m.Success) { $facts.GameSha256 = $m.Groups[1].Value.ToUpperInvariant() }
    $m = [regex]::Match($text, $script:RaNotesApiLevelPattern)
    if ($m.Success) { $facts.ApiLevel = [int]$m.Groups[1].Value }
    $summary = @()
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match $script:RaNotesSectionPattern) { break }
        $b = [regex]::Match($line, $script:RaNotesBulletPattern)
        if (-not $b.Success -or $b.Groups[1].Value -match $script:RaNotesGameVersionPattern) { continue }
        $summary += ($b.Groups[1].Value -replace '[*_`]', '').Trim()
        if ($summary.Count -ge $script:RaReleaseSummaryLines) { break }
    }
    $facts.Summary = $summary
    return $facts
}

function Get-RaReleaseList {
    <#
    .SYNOPSIS
        Lists the published releases that carry a signed manifest, newest first.
    .DESCRIPTION
        One request to the GitHub Releases API (the newest 20 releases), cached for the rest
        of this run. Drafts, releases whose tag is not a version, and releases without
        release-manifest.json and its .sig inside this repository's downloads are left out.
        Nothing is verified here: the chosen release is verified by Get-RaReleaseManifest.
    .OUTPUTS
        System.Object[] (pscustomobject: Version, Tag, Date, Prerelease, Assets, Notes)
    .EXAMPLE
        (Get-RaReleaseList)[0]
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param()
    if ($null -ne $script:RaReleaseCache) { return , @($script:RaReleaseCache) }
    if ($script:RaGitHubRepo -match $script:RaRepoPlaceholderMarker) {
        throw 'Update channel is not configured yet (repository placeholder); older versions cannot be listed.'
    }
    $api = $script:RaGitHubReleasesUrlFormat -f $script:RaGitHubRepo
    Write-RaLog "Listing releases from $api (no identifiers are sent)" -NoConsole
    $data = (ConvertFrom-RaUtf8 (Get-RaHttpContent $api)) | ConvertFrom-Json
    $list = @()
    foreach ($rel in @($data)) {
        if ($null -eq $rel -or [bool]$rel.draft) { continue }
        $version = ([string]$rel.tag_name).TrimStart($script:RaReleaseTagPrefix)
        if ($version -notmatch $script:RaVersionPattern) { continue }
        $assets = @{}
        foreach ($a in @($rel.assets)) { if ($a) { $assets[[string]$a.name] = [string]$a.browser_download_url } }
        $signed = $true
        foreach ($n in $script:RaManifestAssetName, $script:RaSignatureAssetName) {
            if (-not $assets.ContainsKey($n) -or -not (Test-RaReleaseAssetUrl $assets[$n])) { $signed = $false }
        }
        if (-not $signed) { continue }
        $date = $null
        foreach ($field in 'published_at', 'created_at') {
            if (-not $date -and $rel.PSObject.Properties[$field] -and $rel.$field) {
                try { $date = ([datetime]$rel.$field).ToUniversalTime().ToString($script:RaReleaseDateFormat, [Globalization.CultureInfo]::InvariantCulture) }
                catch { Write-Verbose "Unreadable date: $($rel.$field)" }
            }
        }
        $list += [pscustomobject]@{
            Version    = $version
            Tag        = [string]$rel.tag_name
            Date       = $date
            Prerelease = [bool]$rel.prerelease
            Assets     = $assets
            Notes      = (ConvertFrom-RaReleaseNote ([string]$rel.body))
        }
    }
    # Newest first by the numeric x.y.z part (GitHub lists by creation date; a fix of an older line may come later).
    $sorted = @($list | Sort-Object -Property @{ Expression = { [version]($_.Version -replace $script:RaVersionCorePattern, '$1') } } -Descending)
    $script:RaReleaseCache = $sorted
    return , $sorted
}

function Get-RaReleaseManifest {
    <#
    .SYNOPSIS
        Downloads and verifies the signed manifest of one listed release.
    .DESCRIPTION
        Two small downloads (release-manifest.json and .sig). The signature must verify with
        the pinned key, the manifest version must match the tag and the ZIP must be an asset
        of this repository's releases.
    .PARAMETER Release
        One entry of Get-RaReleaseList.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Manifest, ZipUrl)
    .EXAMPLE
        Get-RaReleaseManifest (Get-RaReleaseList)[1]
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Release)
    $mBytes = Get-RaHttpContent $Release.Assets[$script:RaManifestAssetName]
    $sigText = ConvertFrom-RaUtf8 (Get-RaHttpContent $Release.Assets[$script:RaSignatureAssetName])
    $m = Assert-RaSignedManifest $mBytes $sigText
    Write-RaLog (Get-RaText 'ReleaseVerified' @($m.Version))
    if ($Release.Version -ne $m.Version) {
        throw "Release tag $($Release.Tag) does not match manifest version $($m.Version)."
    }
    $zipUrl = if ($Release.Assets.ContainsKey($m.ZipName)) { $Release.Assets[$m.ZipName] } else { $null }
    if (-not (Test-RaReleaseAssetUrl $zipUrl)) { throw "Release has no valid $($m.ZipName) asset." }
    return [pscustomobject]@{ Manifest = $m; ZipUrl = $zipUrl }
}

function Get-RaReleaseRow {
    <#
    .SYNOPSIS
        Display lines of one release for the version list.
    .DESCRIPTION
        Name is the version. The first hint line holds API level, supported game version with
        the start of the hoi4.exe SHA-256, date and the installed/latest marks; then up to
        three changelog lines; then a warning when the release notes name a hoi4.exe build
        other than the player's. Unknown facts read "unknown".
    .PARAMETER Release
        One entry of Get-RaReleaseList.
    .PARAMETER InstalledVersion
        Active version or empty.
    .PARAMETER LatestVersion
        Newest listed version.
    .PARAMETER GameSha
        SHA-256 of the player's hoi4.exe, or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Name, Hints, Supported)
    .EXAMPLE
        Get-RaReleaseRow -Release $release -InstalledVersion '1.5.0' -LatestVersion '1.5.0' -GameSha $sha
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$Release,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$InstalledVersion,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$LatestVersion,
        [Parameter(Position = 3)][AllowEmptyString()][AllowNull()][string]$GameSha
    )
    $unknown = Get-RaText 'VersionUnknown'
    $notes = $Release.Notes
    $api = if ($null -ne $notes.ApiLevel) { [string]$notes.ApiLevel } else { $unknown }
    $game = if ($notes.GameVersion) { $notes.GameVersion } else { $unknown }
    if ($notes.GameSha256) { $game += ' (' + $notes.GameSha256.Substring(0, $script:RaReleaseShaPrefixLength) + ')' }
    # The notes may give only the start of the hash; a release is for this game when the hash starts with it.
    $date = if ($Release.Date) { $Release.Date } else { $unknown }
    $marks = @()
    if ($InstalledVersion -and $Release.Version -eq $InstalledVersion) { $marks += Get-RaText 'VersionMarkInstalled' }
    if ($LatestVersion -and $Release.Version -eq $LatestVersion) { $marks += Get-RaText 'VersionMarkLatest' }
    if ($Release.Prerelease) { $marks += Get-RaText 'VersionMarkPrerelease' }
    $first = Get-RaText 'VersionFacts' @($api, $game, $date)
    if ($marks.Count -gt 0) { $first += ' [' + ($marks -join ', ') + ']' }
    $hints = @($first)
    foreach ($line in @($notes.Summary)) { if ($line) { $hints += "- $line" } }
    $supported = $true
    if ($GameSha -and $notes.GameSha256 -and -not $GameSha.ToUpperInvariant().StartsWith($notes.GameSha256)) {
        $supported = $false
        $hints += Get-RaText 'VersionGameWarning' @($GameSha.Substring(0, $script:RaReleaseShaPrefixLength))
    }
    return [pscustomobject]@{ Name = $Release.Version; Hints = $hints; Supported = $supported }
}

function Get-RaReleaseRowList {
    <#
    .SYNOPSIS
        The listed releases with their display rows.
    .DESCRIPTION
        Combines Get-RaReleaseList, the installed version and the player's hoi4.exe hash.
    .PARAMETER State
        State object (read only).
    .PARAMETER GameDir
        Game folder or empty.
    .OUTPUTS
        System.Object[] (pscustomobject: Release, Row)
    .EXAMPLE
        Get-RaReleaseRowList (Read-RaState) $gameDir
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    $releases = Get-RaReleaseList   # returns the array itself (no @() around it)
    $latest = @($releases | Where-Object { -not $_.Prerelease } | Select-Object -First 1)
    $latestVersion = if ($latest.Count -gt 0) { $latest[0].Version } elseif ($releases.Count -gt 0) { $releases[0].Version } else { $null }
    $gameSha = Get-RaGameSha256 $GameDir
    $rows = @()
    foreach ($r in $releases) {
        $row = Get-RaReleaseRow -Release $r -InstalledVersion $State.InstalledVersion -LatestVersion $latestVersion -GameSha $gameSha
        $rows += [pscustomobject]@{ Release = $r; Row = $row }
    }
    return , $rows
}

function Show-RaReleaseList {
    <#
    .SYNOPSIS
        Prints the signed releases (-ListVersions).
    .DESCRIPTION
        Changes nothing on disk except the log. One GitHub request.
    .PARAMETER State
        State object (read only).
    .PARAMETER GameDir
        Game folder or empty.
    .OUTPUTS
        System.Int32 (number of releases listed)
    .EXAMPLE
        Show-RaReleaseList (Read-RaState) $gameDir
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    $rows = Get-RaReleaseRowList $State $GameDir
    if ($rows.Count -eq 0) {
        Write-RaHost (Get-RaText 'VersionsNone')
        return 0
    }
    Write-RaHost (Get-RaText 'VersionsTitle') -Color White
    foreach ($item in $rows) {
        $hints = @($item.Row.Hints)
        Write-RaHost ('  {0,-9} {1}' -f $item.Row.Name, $hints[0])
        for ($i = 1; $i -lt $hints.Count; $i++) { Write-RaHost ('  {0,-9} {1}' -f '', $hints[$i]) }
    }
    Write-RaHost (Get-RaText 'VersionsHint')
    return $rows.Count
}

function Install-RaReleaseVersion {
    <#
    .SYNOPSIS
        Installs and activates one listed release (-Version, "Install an older version").
    .DESCRIPTION
        Finds the version in Get-RaReleaseList, verifies its signed manifest, reports its
        manifest facts (or "unknown"), warns when it does not list the player's hoi4.exe
        build, then installs it like the newest release (ZIP and per-file SHA-256, version
        folder under %LOCALAPPDATA%\RenameAnything) and switches to it. The version that was
        active stays as the previous one, and option 1 / -Update returns to the newest.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER Version
        Version to install (x.y.z).
    .PARAMETER GameDir
        Game folder or empty.
    .OUTPUTS
        System.String (the installed version)
    .EXAMPLE
        Install-RaReleaseVersion -State $state -Version '1.4.0' -GameDir $gameDir
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Version,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    $wanted = $Version.TrimStart($script:RaReleaseTagPrefix)
    $all = Get-RaReleaseList
    $release = @($all | Where-Object { $_.Version -eq $wanted } | Select-Object -First 1)
    if ($release.Count -eq 0) { throw (Get-RaText 'VersionNotFound' @($wanted)) }
    $remote = Get-RaReleaseManifest $release[0]
    $m = $remote.Manifest
    $unknown = Get-RaText 'VersionUnknown'
    $api = if ($m.Contains('ApiLevel') -and $m.ApiLevel) { [string]$m.ApiLevel } else { $unknown }
    $game = if ($m.Contains('GameVersion') -and $m.GameVersion) { [string]$m.GameVersion } else { $unknown }
    Write-RaLog (Get-RaText 'VersionManifestFacts' @($m.Version, $api, $game))
    $gameSha = Get-RaGameSha256 $GameDir
    if ($gameSha -and @($m.SupportedGameSha256) -notcontains $gameSha) {
        Write-RaLog (Get-RaText 'VersionUnsupported' @($m.Version, $gameSha.Substring(0, $script:RaReleaseShaPrefixLength))) -Level WARN
    }
    [void](Install-RaVersion -Manifest $m -LocalZip $null -ZipUrl $remote.ZipUrl)
    Set-RaActiveVersion $State $m.Version
    return [string]$m.Version
}
#endregion Features/Versions.ps1

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

#region Features/PlaysetMod.ps1
# ---------------------------------------------------------------------------
# Slice: the "CAPIE" switch mod in the player's mod folder
# (Documents\Paradox Interactive\Hearts of Iron IV\mod). The release bundle carries it under
# mod/ (mod/capie.mod and mod/capie/...). -Install, -Update and -Rollback copy it from the
# verified version folder; -Uninstall removes it again. Only files recorded in state.json with
# their SHA-256 are ever replaced or removed; a file that was there before is backed up first
# (byte-verified copy) and restored by -Uninstall; a file the player changed is left alone.
# ---------------------------------------------------------------------------

function Get-RaPlaysetModRoot {
    <#
    .SYNOPSIS
        The game's user mod folder.
    .DESCRIPTION
        <Documents>\Paradox Interactive\Hearts of Iron IV\mod, Documents from the adapter.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaPlaysetModRoot
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path ([string](& $script:RaDocumentsPath)) $script:RaGameUserModRelPath)
}

function Get-RaPlaysetModTarget {
    <#
    .SYNOPSIS
        Maps a bundle path under mod/ to its place in the mod folder.
    .DESCRIPTION
        mod/capie.mod -> <mod folder>\capie.mod, mod/capie/x -> <mod folder>\capie\x.
    .PARAMETER BundlePath
        Manifest path starting with mod/.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaPlaysetModTarget 'mod/capie.mod'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][string]$BundlePath)
    $relative = $BundlePath.Substring($script:RaModBundlePrefix.Length) -replace '/', '\'
    return (Join-Path (Get-RaPlaysetModRoot) $relative)
}

function Find-RaPlaysetModRecord {
    <#
    .SYNOPSIS
        The state.json record of one mod file, or $null.
    .DESCRIPTION
        Records: Path, Sha256 (what we wrote), Backup (our copy of a file that was there before, or $null).
    .PARAMETER State
        State object.
    .PARAMETER Path
        Target path.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        Find-RaPlaysetModRecord $state $target
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][string]$Path)
    return (@($State.ModFiles) | Where-Object { $_ -and $_.Path -ieq $Path } | Select-Object -First 1)
}

function Write-RaVerifiedFile {
    <#
    .SYNOPSIS
        Writes bytes through a temporary file and verifies the result by SHA-256.
    .DESCRIPTION
        The temporary file is moved over the target (replace) and the target hash must equal
        the expected one, else the call throws.
    .PARAMETER Source
        Verified source file (inside the version folder).
    .PARAMETER Target
        Destination.
    .PARAMETER Sha256
        Expected hash.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaVerifiedFile -Source $src -Target $dest -Sha256 $hash
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Source,
        [Parameter(Mandatory, Position = 1)][string]$Target,
        [Parameter(Mandatory, Position = 2)][string]$Sha256
    )
    $parent = Split-Path -Parent $Target
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $tmp = "$Target$($script:RaWriteTempSuffix)"
    & $script:RaCopyFile $Source $tmp
    & $script:RaCopyFile $tmp $Target
    Remove-Item -LiteralPath $tmp -Force
    if ((Get-RaFileSha256 $Target) -ine $Sha256) { throw "Verification failed after writing $Target" }
}

function Install-RaPlaysetMod {
    <#
    .SYNOPSIS
        Copies the "CAPIE" switch mod of the active version into the player's mod folder.
    .DESCRIPTION
        For each mod/ file of the active version's manifest: identical file present -> only
        recorded; our older file (hash recorded) -> replaced; a file that was there before ->
        backed up (byte-verified copy next to it) and replaced; every written file is verified
        by SHA-256. Our files that the new version no longer has are removed (hash-checked).
        A release without mod/ files changes nothing. With a Workshop subscription to the switch
        mod (ugc_<id>.mod in the mod folder) no local copy is installed and an earlier one is
        removed, because HOI4 skips a Workshop mod that has a local twin with the same name.
        No-op in dry-run mode except for plan lines.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaPlaysetMod $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if (-not $State.InstalledVersion) { return }
    $manifest = Read-RaStoredManifest $State.InstalledVersion
    if (-not $manifest) { return }
    $bundle = @($manifest.Files.Keys | Where-Object { ([string]$_).StartsWith($script:RaModBundlePrefix) })
    if ($bundle.Count -eq 0) {
        Write-RaLog "Release $($State.InstalledVersion) has no CAPIE switch mod; mod folder unchanged." -NoConsole
        return
    }
    $workshop = Join-Path (Get-RaPlaysetModRoot) $script:RaWorkshopModDescriptor
    if (Test-Path -LiteralPath $workshop -PathType Leaf) {
        Write-RaLog (Get-RaText 'WorkshopModPresent' @($script:RaWorkshopModDescriptor)) -Level WARN
        Remove-RaPlaysetMod $State
        return
    }
    $versionDir = Get-RaVersionDir $State.InstalledVersion
    $records = @()
    foreach ($entry in $bundle) {
        $target = Get-RaPlaysetModTarget $entry
        $hash = ([string]$manifest.Files[$entry]).ToUpperInvariant()
        $record = Find-RaPlaysetModRecord $State $target
        $backup = if ($record) { $record.Backup } else { $null }
        if ($script:RaDry) {
            Write-RaLog "[dry-run] would place the CAPIE mod file $target" -Level PLAN
            continue
        }
        if (Test-Path -LiteralPath $target -PathType Container) { throw "A folder is in the way of the CAPIE mod file: $target" }
        $current = if (Test-Path -LiteralPath $target) { Get-RaFileSha256 $target } else { $null }
        if ($current -ne $hash) {
            if ($current -and -not ($record -and $current -ieq $record.Sha256)) {
                $backup = New-RaBackupPath $target
                $original = [byte[]](& $script:RaReadAllBytes $target)
                & $script:RaWriteAllBytes $backup $original
                if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $backup)) $original)) { throw "Backup verification failed: $backup" }
                Write-RaLog "Backup of an existing mod file: $backup"
            }
            Write-RaVerifiedFile -Source (Join-Path $versionDir ($entry -replace '/', '\')) -Target $target -Sha256 $hash
        }
        $records += [pscustomobject]@{ Path = $target; Sha256 = $hash; Backup = $backup }
    }
    if ($script:RaDry) { return }
    foreach ($old in @($State.ModFiles | Where-Object { $_ })) {
        if (@($records | Where-Object { $_.Path -ieq $old.Path }).Count -gt 0) { continue }
        $records += $old   # no longer in the release: kept on record so -Uninstall still handles it
    }
    $State.ModFiles = $records
    Write-RaLog ("CAPIE switch mod installed in {0}: enable 'CAPIE' in your playset." -f (Get-RaPlaysetModRoot))
}

function Remove-RaPlaysetMod {
    <#
    .SYNOPSIS
        Removes our switch mod files and restores what was there before.
    .DESCRIPTION
        A file still carrying our recorded hash is removed through the owned-file quarantine;
        a changed file is kept and reported. A backup we made is moved back afterwards. Empty
        folders that held our files are removed, never the mod folder itself.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaPlaysetMod $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $kept = @()
    $root = Get-RaPlaysetModRoot
    foreach ($record in @($State.ModFiles | Where-Object { $_ })) {
        $path = [string]$record.Path
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            if ((Get-RaFileSha256 $path) -ine $record.Sha256) {
                Write-RaLog "Mod file changed since installation; left in place: $path" -Level WARN
                $kept += $record
                continue
            }
            Remove-RaOwnedFile -Path $path -Sha256 $record.Sha256
        }
        if ($record.Backup -and (Test-Path -LiteralPath $record.Backup -PathType Leaf)) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would restore $path from $($record.Backup)" -Level PLAN; continue }
            $original = [byte[]](& $script:RaReadAllBytes $record.Backup)
            & $script:RaWriteAllBytes $path $original
            if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $path)) $original)) { throw "Restore verification failed: $path" }
            [IO.File]::Delete($record.Backup)
            Write-RaLog "Restored the earlier file $path"
        }
    }
    if (-not $script:RaDry) {
        $folders = @{}
        foreach ($record in @($State.ModFiles | Where-Object { $_ })) {
            $folder = Split-Path -Parent ([string]$record.Path)
            while ($folder -and $folder.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                $folders[$folder] = $true
                $folder = Split-Path -Parent $folder
            }
        }
        foreach ($folder in @($folders.Keys | Sort-Object { $_.Length } -Descending)) {
            if ((Test-Path -LiteralPath $folder) -and @(Get-ChildItem -LiteralPath $folder -Force).Count -eq 0) {
                [IO.Directory]::Delete($folder, $false)
            }
        }
        $State.ModFiles = $kept
    }
}
#endregion Features/PlaysetMod.ps1

#region Features/Uninstall.ps1
# ---------------------------------------------------------------------------
# Slice: uninstall (-Uninstall, -Uninstall -Purge) and disable (-Disable).
# ---------------------------------------------------------------------------

function Invoke-RaUninstall {
    <#
    .SYNOPSIS
        Removes the Steam launch option part, our shortcuts and the installed files.
    .DESCRIPTION
        Steam first: while Steam keeps the launch option pointing at the launcher, no
        file is removed. Then the proxy, shortcuts and the "CAPIE" switch mod (earlier files
        restored), then the 'current' junction (so nothing can be
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
    Remove-RaPlaysetMod $State
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

function Invoke-RaDisable {
    <#
    .SYNOPSIS
        Turns CAPIE off without removing files (-Disable).
    .DESCRIPTION
        Removes only our part of the HOI4 Steam launch options (restoring the exact earlier
        value when nobody changed it since) and our proxy activation marker, if any. The next
        start from Steam is the clean game. Installed files stay, so -SteamLaunchOption turns
        CAPIE on again without a download.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-RaDisable $state $steam
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    if ($SteamRootPath) {
        if (-not (Remove-RaSteamLaunchOption $State $SteamRootPath)) { throw 'Steam is running; launch option not changed.' }
    } else {
        Write-RaLog 'Steam not found; launch options unchanged.' -Level WARN
    }
    if ($State.ProxyActivation -eq 'Config') { Set-RaProxyActivation $State 'Flag' }
}

function Remove-RaSteamBackup {
    <#
    .SYNOPSIS
        Deletes the localconfig.vdf backups this script made in every Steam account.
    .DESCRIPTION
        Only files named exactly like New-RaBackupPath names them
        (localconfig.vdf.renameanything-backup-<yyyyMMdd-HHmmss>[-n]) in userdata\<id>\config
        are removed; any other file is left alone. Called by -Uninstall -Purge after the launch
        options were restored.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Int32 (number of deleted or, in dry-run mode, planned files)
    .EXAMPLE
        Remove-RaSteamBackup $steam
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $count = 0
    foreach ($acc in @(Get-RaSteamAccount $SteamRootPath)) {
        $dir = Split-Path -Parent $acc.ConfigPath
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        foreach ($item in @(Get-ChildItem -LiteralPath $dir -File -Force | Where-Object { $_.Name -match $script:RaBackupNamePattern })) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would delete backup $($item.FullName)" -Level PLAN } else {
                Remove-Item -LiteralPath $item.FullName -Force
                Write-RaLog "Deleted backup $($item.FullName)"
            }
            $count++
        }
    }
    return $count
}

function Invoke-RaPurge {
    <#
    .SYNOPSIS
        Removes the last traces after a successful uninstall (-Uninstall -Purge).
    .DESCRIPTION
        Deletes our localconfig.vdf backups, then bootstrap.log, then the root folder when it
        is empty. Must run last: any later log line would create the log again. A root folder
        that still holds foreign files is kept and reported.
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        System.Boolean ($true when nothing of the installation is left)
    .EXAMPLE
        Invoke-RaPurge $steam
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$SteamRootPath)
    if ($SteamRootPath) { [void](Remove-RaSteamBackup $SteamRootPath) }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return $true }
    $rest = @(Get-ChildItem -LiteralPath $script:RaRoot -Force | Where-Object { $_.FullName -ine (Get-RaLogPath) })
    if ($rest.Count -gt 0) {
        Write-RaLog "Kept $($script:RaRoot): it contains files that are not ours ($($rest[0].Name))." -Level WARN
        return $false
    }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would delete $(Get-RaLogPath) and $($script:RaRoot)" -Level PLAN
        return $true
    }
    if (Test-Path -LiteralPath (Get-RaLogPath)) { Remove-Item -LiteralPath (Get-RaLogPath) -Force }
    [IO.Directory]::Delete($script:RaRoot, $false)
    return $true
}
#endregion Features/Uninstall.ps1

#region Features/Tui.ps1
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
#endregion Features/Tui.ps1

#region Main.ps1
# ---------------------------------------------------------------------------
# Main: option handling, action dispatch and the script entry point.
# ---------------------------------------------------------------------------

$script:RaOptionNames = @(
    'Install', 'Shortcut', 'ShortcutPath', 'SteamLaunchOption', 'SteamAccount', 'RemoveSteamLaunchOption',
    'Uninstall', 'DryRun', 'Yes', 'PackagePath', 'SteamRoot', 'CheckUpdate', 'Update', 'Rollback', 'ManifestPath',
    'LoadMethod', 'ProxyActivation', 'Latest', 'Purge', 'Disable', 'NoAnimation', 'Plain', 'ListVersions', 'Version',
    'CloseSteam'
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
    $script:RaCloseSteam = [bool]$Options.CloseSteam
    $script:RaSteamClosedByUs = $false
    $script:RaSteamRestartExe = $null
    $script:RaReleaseCache = $null
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
        Without -Install/-SteamLaunchOption/-Shortcut the user is asked (install or update?
        which start mode?); a yes installs the newest signed release when the update channel
        is configured. The multiplayer warning is shown before any Steam or shortcut step,
        each of which must then be confirmed (-Yes confirms).
    .PARAMETER Options
        Normalized option table.
    .PARAMETER State
        Existing installation preferences, or null for a fresh install.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Install, Latest, Steam, Shortcut, ShortcutFolder)
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
        Latest         = [bool]$Options['Latest']
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
        if ($script:RaAssumeYes) {
            throw 'With -Yes choose at least one of -Install, -Version, -SteamLaunchOption, -Shortcut, -Update, -Disable, -Uninstall.'
        }
        $plan.Install = Confirm-Ra (Get-RaText 'AskInstall' @($script:RaRoot)) $true
        if ($plan.Install -and -not $Options['PackagePath'] -and (Test-RaUpdateChannel)) { $plan.Latest = $true }
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

function Invoke-RaSteamStep {
    <#
    .SYNOPSIS
        Sets the Steam launch option; returns $false (and logs why) when that failed.
    .DESCRIPTION
        A failed Steam step makes the exit code 1 but does not stop the steps after it.
    .PARAMETER State
        State object (saved on success).
    .PARAMETER SteamRootPath
        Steam folder; empty throws "Steam not found".
    .PARAMETER AccountSelector
        Value of -SteamAccount.
    .PARAMETER LoadMethod
        Launcher or Proxy.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Invoke-RaSteamStep -State $state -SteamRootPath $steam)) { 'exit code 1' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$AccountSelector,
        [Parameter(Position = 3)][ValidateSet('Proxy', 'Launcher')][string]$LoadMethod = 'Launcher'
    )
    if (-not $SteamRootPath) { throw 'Steam not found.' }
    try {
        Set-RaSteamLaunchOption -State $State -SteamRootPath $SteamRootPath -AccountSelector $AccountSelector `
            -LauncherPath (Get-RaLauncherPath) -LoadMethod $LoadMethod
        Save-RaState $State
        return $true
    } catch {
        Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
        return $false
    }
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
        if ($Options['Version']) { [void](Install-RaReleaseVersion -State $State -Version $Options['Version'] -GameDir $gameDir) }
        elseif ($plan.Latest) { [void](Install-RaLatest -State $State -LocalManifestPath $Options.ManifestPath -LocalZip $Options.PackagePath) }
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
        $steamOk = Invoke-RaSteamStep -State $State -SteamRootPath $SteamRootPath -AccountSelector $Options.SteamAccount -LoadMethod $plan.LoadMethod
        $steamStepFailed = -not $steamOk
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
        Priority: -Uninstall, -Disable, -RemoveSteamLaunchOption, -ListVersions, -CheckUpdate,
        -Rollback, -Update (an install when nothing is installed yet, else update or repair),
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
    if ($Options['ListVersions']) {
        [void](Show-RaReleaseList -State $state -GameDir (& $gameDir))
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
    if ($Options.Update -and -not $state.InstalledVersion) {
        # Install or update: nothing installed yet, so this is an install.
        $Options.Install = $true
        $Options.Latest = [bool]$Options.ManifestPath -or ((Test-RaUpdateChannel) -and -not $Options.PackagePath)
    } elseif ($Options.Update) {
        $updated = Invoke-RaUpdate -State $state -GameDir (& $gameDir) -LocalManifestPath $Options.ManifestPath -LocalZip $Options.PackagePath
        if ($updated) { Save-RaState $state } else { Write-RaLog (Get-RaText 'AlreadyUpToDate' @($state.InstalledVersion)) }
        Install-RaPlaysetMod $state
        Save-RaState $state
        $steamOk = $true
        if ($Options.SteamLaunchOption) {
            $method = if ($state.LoadMethod) { [string]$state.LoadMethod } else { 'Launcher' }
            $steamOk = Invoke-RaSteamStep -State $state -SteamRootPath $steamRootPath -AccountSelector $Options.SteamAccount -LoadMethod $method
        }
        if ($updated) { Remove-RaUnusedVersion $state }
        if (-not $steamOk) { return $script:RaExitFailure }
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
    if ($options.Version) {
        if (([string]$options.Version).TrimStart($script:RaReleaseTagPrefix) -notmatch $script:RaVersionPattern) {
            Write-RaHost (Get-RaText 'Failed' @("-Version needs a version like 1.5.0, not '$($options.Version)'."))
            return $script:RaExitFailure
        }
        $options.Install = $true
    }
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
    $code = $script:RaExitFailure
    try {
        $code = Invoke-RaAction $options
    } catch {
        Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR
        $code = $script:RaExitFailure
    }
    if ($script:RaSteamClosedByUs) {
        # This run closed Steam: offer to start it again (with -Yes -CloseSteam it just starts).
        try {
            if ($script:RaAssumeYes -or (Confirm-Ra (Get-RaText 'AskRestartSteam') $true)) { Start-RaSteamAgain }
            else { $script:RaSteamClosedByUs = $false }
        } catch { Write-RaLog (Get-RaText 'Failed' @($_.Exception.Message)) -Level ERROR }
    }
    return $code
}

if ($MyInvocation.InvocationName -ne '.') {
    $raExit = Invoke-RaMain @{
        Install = $Install; Shortcut = $Shortcut; ShortcutPath = $ShortcutPath; SteamLaunchOption = $SteamLaunchOption
        SteamAccount = $SteamAccount; RemoveSteamLaunchOption = $RemoveSteamLaunchOption; Uninstall = $Uninstall
        DryRun = $DryRun; Yes = $Yes; PackagePath = $PackagePath; SteamRoot = $SteamRoot
        CheckUpdate = $CheckUpdate; Update = $Update; Rollback = $Rollback; ManifestPath = $ManifestPath
        LoadMethod = $LoadMethod; ProxyActivation = $ProxyActivation; Latest = $Latest; Purge = $Purge; Disable = $Disable
        NoAnimation = $NoAnimation; Plain = $Plain; ListVersions = $ListVersions; Version = $Version; CloseSteam = $CloseSteam
    }
    if ($MyInvocation.MyCommand.CommandType -eq 'ExternalScript') { exit ([int]@($raExit)[-1]) }
    # Under `irm | iex` never call exit (it would close the user's terminal).
}
#endregion Main.ps1
