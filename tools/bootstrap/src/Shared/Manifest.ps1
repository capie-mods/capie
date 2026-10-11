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
