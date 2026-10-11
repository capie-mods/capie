<#
.SYNOPSIS
    Maintainer tool (offline): create the update signing key, build release-manifest.json
    from a release ZIP, and sign it. Never uploads anything.

.EXAMPLE
    # once; keep the private key OUTSIDE the repository (ideally on removable/encrypted storage)
    .\sign-release-manifest.ps1 -NewKey -PrivateKeyPath D:\keys\ra-update.private.xml
    # prints the public key XML -> paste into $RaUpdatePublicKeyXml in install.ps1

    # per release
    .\sign-release-manifest.ps1 -MakeManifest -ZipPath .\RenameAnything-0.2.0-win64.zip -Version 0.2.0 `
        -SupportedGameSha256 7DC947BE... -OutPath .\release-manifest.json
    .\sign-release-manifest.ps1 -Sign -ManifestPath .\release-manifest.json -PrivateKeyPath D:\keys\ra-update.private.xml
    # upload release-manifest.json, release-manifest.json.sig and the ZIP as release assets
#>
[CmdletBinding()]
param(
    [switch]$NewKey,
    [switch]$MakeManifest,
    [switch]$Sign,
    [string]$PrivateKeyPath,
    [string]$ZipPath,
    [string]$Version,
    [string[]]$SupportedGameSha256 = @(),
    [string]$OutPath,
    [string]$ManifestPath,
    [int]$KeySize = 3072
)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)

if ($NewKey) {
    if (Test-Path -LiteralPath $PrivateKeyPath) { throw "Refusing to overwrite $PrivateKeyPath" }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider($KeySize)
    try {
        [IO.File]::WriteAllText($PrivateKeyPath, $rsa.ToXmlString($true), $utf8)
        Write-Output $rsa.ToXmlString($false)
    } finally { $rsa.PersistKeyInCsp = $false; $rsa.Dispose() }
    return
}

if ($MakeManifest) {
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $files = [ordered]@{}
    $zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ZipPath).ProviderPath)
    try {
        foreach ($e in $zip.Entries) {
            $name = $e.FullName -replace '\\', '/'
            if ($name.EndsWith('/')) { continue }
            $sha = [System.Security.Cryptography.SHA256]::Create()
            $s = $e.Open()
            try { $files[$name] = ([BitConverter]::ToString($sha.ComputeHash($s))).Replace('-', '') } finally { $s.Dispose(); $sha.Dispose() }
        }
    } finally { $zip.Dispose() }
    $manifest = [pscustomobject]@{
        schema = 1; version = $Version; zipName = [IO.Path]::GetFileName($ZipPath)
        zipSha256 = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
        files = [pscustomobject]$files
        supportedGameSha256 = @($SupportedGameSha256 | ForEach-Object { $_.ToUpperInvariant() })
    }
    [IO.File]::WriteAllText($OutPath, ($manifest | ConvertTo-Json -Depth 5), $utf8)
    Write-Output "Wrote $OutPath"
    return
}

if ($Sign) {
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    try {
        $rsa.FromXmlString([IO.File]::ReadAllText($PrivateKeyPath))
        $sig = $rsa.SignData([IO.File]::ReadAllBytes($ManifestPath), 'SHA256')
        [IO.File]::WriteAllText("$ManifestPath.sig", [Convert]::ToBase64String($sig), $utf8)
        Write-Output "Wrote $ManifestPath.sig"
    } finally { $rsa.PersistKeyInCsp = $false; $rsa.Dispose() }
    return
}

throw 'Choose one of -NewKey, -MakeManifest, -Sign.'
