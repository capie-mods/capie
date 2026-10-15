<#
.SYNOPSIS
    Maintainer tool (offline): create the update signing key, build release-manifest.json
    from a release ZIP, and sign it. Never uploads anything.

.DESCRIPTION
    Three independent modes:
      -NewKey        creates an RSA key pair, writes the PRIVATE key XML to -PrivateKeyPath
                     (refuses to overwrite) and prints the PUBLIC key XML for install.ps1
                     ($RaUpdatePublicKeyXml in tools/bootstrap/src/Shared/Configuration.ps1).
      -MakeManifest  hashes every file inside -ZipPath plus the ZIP itself and writes the
                     schema-1 manifest JSON to -OutPath.
      -Sign          signs the exact bytes of -ManifestPath (RSA PKCS#1 v1.5, SHA-256) and
                     writes <manifest>.sig (Base64).
    Keep the private key OUTSIDE the repository (ideally on removable/encrypted storage).

.PARAMETER NewKey
    Create a new key pair.

.PARAMETER MakeManifest
    Build release-manifest.json from a ZIP.

.PARAMETER Sign
    Sign a manifest.

.PARAMETER PrivateKeyPath
    Private key XML file (written by -NewKey, read by -Sign).

.PARAMETER ZipPath
    Release ZIP for -MakeManifest.

.PARAMETER Version
    Release version for -MakeManifest (x.y.z).

.PARAMETER SupportedGameSha256
    hoi4.exe SHA-256 values the release supports.

.PARAMETER CapieApiLevel
    Script API level of the CAPIE release (manifest field capie_api_level), for -MakeManifest.

.PARAMETER GameVersion
    Supported game version as text, e.g. 1.19.3.0 (manifest field game_version).

.PARAMETER ChangelogSummaryBase64
    Base64 of a UTF-8 JSON array of short change lines (manifest field changelog_summary). Base64
    keeps the text intact on any command line.

.PARAMETER OutPath
    Manifest output path for -MakeManifest.

.PARAMETER ManifestPath
    Manifest to sign for -Sign.

.PARAMETER KeySize
    RSA key size in bits for -NewKey (install.ps1 refuses keys under 2048).

.OUTPUTS
    System.String. The public key XML (-NewKey) or a "Wrote <path>" line.

.EXAMPLE
    # once
    .\sign-release-manifest.ps1 -NewKey -PrivateKeyPath D:\keys\ra-update.private.xml

.EXAMPLE
    # per release
    .\sign-release-manifest.ps1 -MakeManifest -ZipPath .\RenameAnything-0.2.0-win64.zip -Version 0.2.0 `
        -SupportedGameSha256 7DC947BE... -OutPath .\release-manifest.json
    .\sign-release-manifest.ps1 -Sign -ManifestPath .\release-manifest.json -PrivateKeyPath D:\keys\ra-update.private.xml
    # upload release-manifest.json, release-manifest.json.sig and the ZIP as release assets
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [switch]$NewKey,
    [switch]$MakeManifest,
    [switch]$Sign,
    [string]$PrivateKeyPath,
    [string]$ZipPath,
    [string]$Version,
    [string[]]$SupportedGameSha256 = @(),
    [ValidateRange(0, 1000)][int]$CapieApiLevel = 0,
    [string]$GameVersion = '',
    [string]$ChangelogSummaryBase64 = '',
    [string]$OutPath,
    [string]$ManifestPath,
    [ValidateRange(2048, 16384)][int]$KeySize = 3072
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:SignManifestSchema = 1
$script:SignJsonDepth = 5
$script:SignHashAlgorithm = 'SHA256'
$script:SignSignatureSuffix = '.sig'
$script:SignUtf8 = New-Object System.Text.UTF8Encoding($false)

function Assert-SignArgument {
    <#
    .SYNOPSIS
        Throws when a required argument of the chosen mode is empty.
    .DESCRIPTION
        Gives a clear message instead of a .NET null-path error.
    .PARAMETER Name
        Parameter name for the message.
    .PARAMETER Value
        Value to check.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-SignArgument -Name 'ZipPath' -Value $ZipPath
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][string]$Name,
        [AllowEmptyString()][AllowNull()][string]$Value
    )
    if (-not $Value) { throw "-$Name is required for this mode." }
}

function New-SignKeyPair {
    <#
    .SYNOPSIS
        Creates an RSA key pair, stores the private key and returns the public key XML.
    .DESCRIPTION
        Refuses to overwrite an existing private key file. The key is never persisted in
        the Windows key container.
    .PARAMETER Path
        Private key output file.
    .PARAMETER Bits
        Key size in bits.
    .OUTPUTS
        System.String (public key XML)
    .EXAMPLE
        New-SignKeyPair -Path D:\keys\ra.private.xml -Bits 3072
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][int]$Bits
    )
    if (Test-Path -LiteralPath $Path) { throw "Refusing to overwrite $Path" }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider($Bits)
    try {
        [IO.File]::WriteAllText($Path, $rsa.ToXmlString($true), $script:SignUtf8)
        return $rsa.ToXmlString($false)
    } finally {
        $rsa.PersistKeyInCsp = $false
        $rsa.Dispose()
    }
}

function Get-SignZipEntryHash {
    <#
    .SYNOPSIS
        SHA-256 of every file entry inside a ZIP, keyed by its forward-slash path.
    .DESCRIPTION
        Directory entries are skipped; the order is the ZIP order.
    .PARAMETER Path
        ZIP file.
    .OUTPUTS
        System.Collections.Specialized.OrderedDictionary
    .EXAMPLE
        Get-SignZipEntryHash -Path .\RenameAnything-0.2.0-win64.zip
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Specialized.OrderedDictionary])]
    param([Parameter(Mandatory)][string]$Path)
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $files = [ordered]@{}
    $zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).ProviderPath)
    try {
        foreach ($e in $zip.Entries) {
            $name = $e.FullName -replace '\\', '/'
            if ($name.EndsWith('/')) { continue }
            $sha = [System.Security.Cryptography.SHA256]::Create()
            $s = $e.Open()
            try { $files[$name] = ([BitConverter]::ToString($sha.ComputeHash($s))).Replace('-', '') } finally {
                $s.Dispose()
                $sha.Dispose()
            }
        }
    } finally { $zip.Dispose() }
    return $files
}

function ConvertTo-SignManifestJson {
    <#
    .SYNOPSIS
        Builds the schema-1 release manifest JSON for a ZIP.
    .DESCRIPTION
        Same schema install.ps1 parses (ConvertFrom-RaManifestJson). Game hashes are upper-cased.
    .PARAMETER Path
        Release ZIP.
    .PARAMETER ReleaseVersion
        Version string.
    .PARAMETER GameSha256
        Supported hoi4.exe hashes.
    .PARAMETER ApiLevel
        CAPIE script API level; 0 leaves capie_api_level out.
    .PARAMETER GameVersionText
        Supported game version text; empty leaves game_version out.
    .PARAMETER Summary
        Short change lines; none leaves changelog_summary out.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-SignManifestJson -Path .\a.zip -ReleaseVersion 0.2.0 -GameSha256 @()
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ReleaseVersion,
        [AllowEmptyCollection()][string[]]$GameSha256 = @(),
        [int]$ApiLevel = 0,
        [AllowEmptyString()][string]$GameVersionText = '',
        [AllowEmptyCollection()][string[]]$Summary = @()
    )
    $manifest = [ordered]@{
        schema              = $script:SignManifestSchema
        version             = $ReleaseVersion
        zipName             = [IO.Path]::GetFileName($Path)
        zipSha256           = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        files               = [pscustomobject](Get-SignZipEntryHash -Path $Path)
        supportedGameSha256 = @($GameSha256 | ForEach-Object { $_.ToUpperInvariant() })
    }
    # Optional display data for the installer's version picker (schema 1 stays valid without it).
    if ($ApiLevel -gt 0) { $manifest['capie_api_level'] = $ApiLevel }
    if ($GameVersionText) { $manifest['game_version'] = $GameVersionText }
    if (@($Summary).Count -gt 0) { $manifest['changelog_summary'] = @($Summary) }
    return ([pscustomobject]$manifest | ConvertTo-Json -Depth $script:SignJsonDepth)
}

function Write-SignManifestSignature {
    <#
    .SYNOPSIS
        Signs the exact bytes of a manifest and writes <manifest>.sig.
    .DESCRIPTION
        RSA PKCS#1 v1.5 with SHA-256, Base64 text without BOM.
    .PARAMETER Path
        Manifest to sign.
    .PARAMETER KeyPath
        Private key XML.
    .OUTPUTS
        System.String (signature file path)
    .EXAMPLE
        Write-SignManifestSignature -Path .\release-manifest.json -KeyPath D:\keys\ra.private.xml
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$KeyPath
    )
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    try {
        $rsa.FromXmlString([IO.File]::ReadAllText($KeyPath))
        $sig = $rsa.SignData([IO.File]::ReadAllBytes($Path), $script:SignHashAlgorithm)
        $sigPath = $Path + $script:SignSignatureSuffix
        [IO.File]::WriteAllText($sigPath, [Convert]::ToBase64String($sig), $script:SignUtf8)
        return $sigPath
    } finally {
        $rsa.PersistKeyInCsp = $false
        $rsa.Dispose()
    }
}

if ($NewKey) {
    Assert-SignArgument -Name 'PrivateKeyPath' -Value $PrivateKeyPath
    Write-Output (New-SignKeyPair -Path $PrivateKeyPath -Bits $KeySize)
    return
}
if ($MakeManifest) {
    Assert-SignArgument -Name 'ZipPath' -Value $ZipPath
    Assert-SignArgument -Name 'Version' -Value $Version
    Assert-SignArgument -Name 'OutPath' -Value $OutPath
    $summary = @()
    if ($ChangelogSummaryBase64) {
        $summary = @([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($ChangelogSummaryBase64)) | ConvertFrom-Json)
    }
    $json = ConvertTo-SignManifestJson -Path $ZipPath -ReleaseVersion $Version -GameSha256 $SupportedGameSha256 `
        -ApiLevel $CapieApiLevel -GameVersionText $GameVersion -Summary $summary
    [IO.File]::WriteAllText($OutPath, $json, $script:SignUtf8)
    Write-Output "Wrote $OutPath"
    return
}
if ($Sign) {
    Assert-SignArgument -Name 'ManifestPath' -Value $ManifestPath
    Assert-SignArgument -Name 'PrivateKeyPath' -Value $PrivateKeyPath
    Write-Output "Wrote $(Write-SignManifestSignature -Path $ManifestPath -KeyPath $PrivateKeyPath)"
    return
}
throw 'Choose one of -NewKey, -MakeManifest, -Sign.'
