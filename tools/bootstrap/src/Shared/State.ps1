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
