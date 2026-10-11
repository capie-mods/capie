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
