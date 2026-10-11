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