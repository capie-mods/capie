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
    if (-not $Manifest.Contains('Proxy')) { $Manifest['Proxy'] = $null }
    $proxy = $Manifest.Proxy
    if ($null -eq $proxy) { return }
    # Optional separate proxy asset (CAPIE 1.6.1+): its own ZIP, only for -LoadMethod Proxy.
    if ($proxy.ZipName -notmatch $script:RaZipNamePattern -or $proxy.ZipName -ieq $Manifest.ZipName) { throw 'Release manifest: invalid proxy ZIP name.' }
    if (-not (Test-RaSha256Text $proxy.ZipSha256)) { throw 'Release manifest: bad proxy ZIP hash.' }
    if (-not $proxy.Files -or -not $proxy.Files.Contains($script:RaProxyBundlePath)) { throw 'Release manifest: proxy asset without proxy/version.dll.' }
    foreach ($k in $proxy.Files.Keys) {
        if (-not (Test-RaSha256Text $proxy.Files[$k])) { throw "Release manifest: bad hash for $k" }
        if ($k -notmatch $script:RaManifestPathPattern -or $k -match $script:RaDotSegmentPattern) { throw "Release manifest: unsafe path $k" }
        if ($Manifest.Files.Contains($k)) { throw "Release manifest: $k is listed twice." }
    }
}

function Get-RaManifestProxySha256 {
    <#
    .SYNOPSIS
        The signed SHA-256 of a release's proxy version.dll, or $null when the release has none.
    .DESCRIPTION
        CAPIE 1.6.1 and later list it in the separate proxy asset (manifest field proxy); older
        releases list proxy/version.dll among the files of the main ZIP.
    .PARAMETER Manifest
        Manifest dictionary.
    .OUTPUTS
        System.String (upper case)
    .EXAMPLE
        Get-RaManifestProxySha256 (Read-RaStoredManifest $state.Proxy.Version)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][System.Collections.IDictionary]$Manifest)
    if (-not $Manifest) { return $null }
    $source = Get-RaProxySource $Manifest
    if (-not $source) { return $null }
    return ([string]$source.Files[$script:RaProxyBundlePath]).ToUpperInvariant()
}

function Get-RaProxySource {
    <#
    .SYNOPSIS
        The ZIP (name, hash, files) that holds a release's proxy version.dll, or $null.
    .DESCRIPTION
        For a release with a separate proxy asset: a manifest-shaped dictionary of that asset
        (same Version, so its default download URL is the release's). For an older release the
        main manifest itself, whose ZIP also holds the proxy.
    .PARAMETER Manifest
        Manifest dictionary.
    .OUTPUTS
        System.Collections.IDictionary
    .EXAMPLE
        $source = Get-RaProxySource $manifest
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.IDictionary], [System.Collections.Specialized.OrderedDictionary])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest)
    if ($Manifest.Contains('Proxy') -and $Manifest.Proxy) {
        return [ordered]@{
            Version = $Manifest.Version; ZipName = $Manifest.Proxy.ZipName; ZipSha256 = $Manifest.Proxy.ZipSha256
            Files = $Manifest.Proxy.Files
        }
    }
    if ($Manifest.Files.Contains($script:RaProxyBundlePath)) { return $Manifest }
    return $null
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
        every file has its pinned SHA-256. The proxy version.dll (proxy/version.dll) is not part
        of this check: it is there only for the Proxy method, may be missing (never unpacked, or
        removed by an antivirus) and is verified by Install-RaProxy before every use, so the
        default Launcher method never reads it. An antivirus block of a checked file throws the
        player-facing explanation (Get-RaCheckedFileSha256).
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
    $proxyPath = [IO.Path]::GetFullPath((Join-Path $Dir ($script:RaProxyBundlePath -replace '/', '\')))
    $files = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force | Where-Object { $_.FullName -ine $proxyPath })
    $keys = @($Manifest.Files.Keys | Where-Object { $_ -ne $script:RaProxyBundlePath })
    if ($files.Count -ne $keys.Count) { return $false }
    foreach ($k in $keys) {
        $p = Join-Path $Dir ($k -replace '/', '\')
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return $false }
        $expected = $Manifest.Files[$k].ToUpperInvariant()
        if ((Get-RaCheckedFileSha256 $p -Sha256 $expected -Version $Manifest.Version) -ne $expected) { return $false }
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
        are skipped. With -Only, the other listed entries are checked for presence but never
        written (so the default Launcher method never unpacks the proxy version.dll of an older
        release). An antivirus that blocks or removes an extracted file is reported with the
        player-facing explanation, naming the file, its path and its signed SHA-256.
    .PARAMETER Manifest
        Manifest dictionary.
    .PARAMETER ZipPath
        ZIP file whose own hash was already verified.
    .PARAMETER Destination
        Empty staging folder.
    .PARAMETER Only
        Entries to extract; default all.
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
        [Parameter(Mandatory, Position = 2)][ValidateNotNullOrEmpty()][string]$Destination,
        [AllowNull()][string[]]$Only
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
            if ($null -ne $Only -and $Only -notcontains $name) { continue }
            $dest = Join-Path $Destination ($name -replace '/', '\')
            $parent = Split-Path -Parent $dest
            if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
            $expected = $Manifest.Files[$name].ToUpperInvariant()
            try { [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $false) } catch {
                if (Test-RaAntivirusError $_) { throw (Get-RaAntivirusText -Path $dest -Sha256 $expected -Version $Manifest.Version) }
                throw
            }
            $h = Get-RaCheckedFileSha256 $dest -Sha256 $expected -Version $Manifest.Version -JustWritten
            if ($h -ne $expected) { throw "SHA256 mismatch for $name (got $h)" }
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
    if ($Manifest.Contains('Proxy') -and $Manifest.Proxy) {
        $data['proxy'] = [pscustomobject][ordered]@{
            zipName = $Manifest.Proxy.ZipName; zipSha256 = $Manifest.Proxy.ZipSha256; files = [pscustomobject]$Manifest.Proxy.Files
        }
    }
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
    # Separate proxy asset (CAPIE 1.6.1+); older manifests list proxy/version.dll under files.
    $m['Proxy'] = $null
    if ($j.PSObject.Properties['proxy'] -and $j.proxy) {
        $proxyFiles = [ordered]@{}
        if ($j.proxy.PSObject.Properties['files'] -and $j.proxy.files) {
            foreach ($p in $j.proxy.files.PSObject.Properties) { $proxyFiles[$p.Name] = [string]$p.Value }
        }
        $zipNameValue = if ($j.proxy.PSObject.Properties['zipName']) { [string]$j.proxy.zipName } else { '' }
        $zipShaValue = if ($j.proxy.PSObject.Properties['zipSha256']) { [string]$j.proxy.zipSha256 } else { '' }
        $m['Proxy'] = [ordered]@{ ZipName = $zipNameValue; ZipSha256 = $zipShaValue; Files = $proxyFiles }
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
