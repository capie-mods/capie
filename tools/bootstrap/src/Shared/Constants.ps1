# ---------------------------------------------------------------------------
# Constants. Fixed names, formats, patterns and limits used by several slices.
# Never assigned at run time (tests do not change them either).
# ---------------------------------------------------------------------------

# --- Game and Steam -------------------------------------------------------
$script:RaAppId = '394360'                                  # Hearts of Iron IV Steam app id
$script:RaGameExeName = 'hoi4.exe'
$script:RaParadoxLauncherExeName = 'dowser.exe'             # what Steam's %command% starts
# Processes that load our files: no install, update, rollback or uninstall while one runs.
$script:RaGameProcessNames = @('hoi4', 'dowser')
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
$script:RaJunctionOldInfix = '.old-'         # the previous 'current' link, kept until the new one is in place
# Leftovers of an interrupted run (names from New-RaUniqueName: 32 lower-case hex digits). Swept at
# the start of a run only when the name matches exactly, the item is ours (see Clear-RaLeftover) and
# it is older than RaLeftoverMinAgeMinutes (so a second setup window running at the same time keeps its files).
$script:RaLeftoverDownloadPattern = '^download-[0-9a-f]{32}\.zip$'
$script:RaLeftoverStagingPattern = '^\.staging-[0-9a-f]{32}$'
$script:RaLeftoverJunctionPattern = '^current\.(new|old)-[0-9a-f]{32}$'
$script:RaLeftoverMarkerPattern = '^proxy\.enabled\.[0-9a-f]{32}\.remove$'
$script:RaLeftoverProxyPattern = '^version\.dll\.[0-9a-f]{32}\.(tmp|bak|remove)$'
$script:RaLeftoverMinAgeMinutes = 30
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
# What a player is told to open or run when the installed CAPIE does not support the game build.
$script:RaGitHubLatestPageFormat = 'https://github.com/{0}/releases/latest'
$script:RaGitHubLatestInstallerFormat = 'https://github.com/{0}/releases/latest/download/install.ps1'
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
