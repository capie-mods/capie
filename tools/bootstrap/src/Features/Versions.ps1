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
