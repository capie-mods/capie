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
