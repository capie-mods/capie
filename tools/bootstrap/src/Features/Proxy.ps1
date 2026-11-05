# ---------------------------------------------------------------------------
# Proxy installation. No foreign file is overwritten, renamed or removed.
# ---------------------------------------------------------------------------

function Get-RaProxyPath {
    <#
    .SYNOPSIS
        Resolves the proxy destination in a verified game directory.
    .DESCRIPTION
        Requires a normal directory containing hoi4.exe; refuses reparse points at
        the directory, executable or destination. Never creates a game directory.
    .PARAMETER GameDir
        Game directory discovered through Steam.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaProxyPath (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$GameDir)
    $dir = Get-Item -LiteralPath $GameDir -Force -ErrorAction Stop
    $exe = Get-Item -LiteralPath (Join-Path $dir.FullName $script:RaGameExeName) -Force -ErrorAction Stop
    if (-not $dir.PSIsContainer -or $exe.PSIsContainer -or (Test-RaReparsePoint $dir) -or (Test-RaReparsePoint $exe)) {
        throw 'Proxy destination must be a normal HOI4 directory containing a normal hoi4.exe.'
    }
    $path = Join-Path $dir.FullName $script:RaProxyFileName
    $existing = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if ($existing -and ($existing.PSIsContainer -or (Test-RaReparsePoint $existing))) {
        throw "Proxy destination is a directory or link; left untouched: $path"
    }
    return $path
}

function Test-RaOwnProxy {
    <#
    .SYNOPSIS
        Verifies an installed proxy against its ownership record and release manifest.
    .DESCRIPTION
        Both the recorded path and hash must match; the hash must also occur in the
        stored verified release manifest. Missing, changed or linked files fail closed.
    .PARAMETER State
        Installer ownership state.
    .PARAMETER Path
        Expected game proxy path.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaOwnProxy $state $path
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][string]$Path)
    if (-not $State.Proxy) { return $false }
    try {
        if ([IO.Path]::GetFullPath($State.Proxy.Path) -ine [IO.Path]::GetFullPath($Path)) { return $false }
        if ([IO.Path]::GetFileName($Path) -ine $script:RaProxyFileName) { return $false }
        if (-not (Test-RaSha256Text $State.Proxy.Sha256)) { return $false }
        $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
        if ($item.PSIsContainer -or (Test-RaReparsePoint $item)) { return $false }
        $manifest = Read-RaStoredManifest $State.Proxy.Version
        return ($manifest -and (Get-RaManifestProxySha256 $manifest) -ieq $State.Proxy.Sha256 -and
            (Get-RaFileSha256 $Path) -ieq $State.Proxy.Sha256)
    } catch { return $false }
}

function Remove-RaOwnedFile {
    <#
    .SYNOPSIS
        Quarantines and re-verifies an owned file before deleting it.
    .DESCRIPTION
        Moving within its directory makes a racing replacement observable. Only the
        random quarantine path with the expected hash is deleted. A different file
        is restored, or preserved under the reported recovery path if restoration fails.
    .PARAMETER Path
        Recorded owned file path.
    .PARAMETER Sha256
        Hash established by the ownership record.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaOwnedFile -Path $state.Proxy.Path -Sha256 $state.Proxy.Sha256
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Sha256
    )
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or (Test-RaReparsePoint $item) -or (Get-RaFileSha256 $Path) -ine $Sha256) {
        throw "Owned file changed before removal; left untouched: $Path"
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned file $Path" -Level PLAN; return }
    $quarantine = $Path + '.' + (New-RaUniqueName) + '.remove'
    if ($script:RaBeforeOwnedRemove) { & $script:RaBeforeOwnedRemove $Path }
    try {
        [IO.File]::Move($Path, $quarantine)
        $moved = Get-Item -LiteralPath $quarantine -Force
        if ($moved.PSIsContainer -or (Test-RaReparsePoint $moved) -or (Get-RaFileSha256 $quarantine) -ine $Sha256) {
            throw "File changed during removal; preserving it: $Path"
        }
        [IO.File]::Delete($quarantine)
    } catch {
        $errorMessage = $_.Exception.Message
        if ((Test-Path -LiteralPath $quarantine) -and -not (Test-Path -LiteralPath $Path)) {
            try { [IO.File]::Move($quarantine, $Path) } catch {
                throw "Removal failed: $errorMessage. Recovery file preserved at $quarantine"
            }
        }
        if (Test-Path -LiteralPath $quarantine) { throw "Removal failed: $errorMessage. Recovery file preserved at $quarantine" }
        throw $errorMessage
    }
}

function Set-RaProxyActivation {
    <#
    .SYNOPSIS
        Enables the explicit config fallback or removes our previous fallback marker.
    .DESCRIPTION
        Flag mode is the default. Config mode writes only the documented ASCII marker.
        An existing marker is changed or deleted only when its saved hash still matches.
    .PARAMETER State
        Installer state, modified in memory.
    .PARAMETER Activation
        Flag or Config.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaProxyActivation $state Config
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][ValidateSet('Flag', 'Config')][string]$Activation)
    $path = Join-Path $script:RaRoot $script:RaProxyConfigName
    $exists = Test-Path -LiteralPath $path
    if ($exists) {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer -or (Test-RaReparsePoint $item) -or -not $State.ProxyConfigSha256 -or
            (Get-RaFileSha256 $path) -ine $State.ProxyConfigSha256) { throw "Foreign or modified activation config left untouched: $path" }
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] proxy activation: $Activation ($path)" -Level PLAN; return }
    if ($Activation -eq 'Config') {
        Initialize-RaRoot
        $bytes = [Text.Encoding]::ASCII.GetBytes($script:RaProxyConfigContent)
        if (-not $exists) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
        }
        $State.ProxyConfigSha256 = Get-RaFileSha256 $path
    } else {
        if ($exists) { Remove-RaOwnedFile -Path $path -Sha256 $State.ProxyConfigSha256 }
        $State.ProxyConfigSha256 = $null
    }
    $State.ProxyActivation = $Activation
}

function Find-RaLocalProxyZip {
    <#
    .SYNOPSIS
        A local ZIP (from -PackagePath) that holds a release's proxy, or $null.
    .DESCRIPTION
        Candidates: the -PackagePath ZIP itself (an older release ZIP holds the proxy) and a file
        named like the proxy asset next to it. Only a candidate whose SHA-256 is the signed one is
        returned; without a match the caller downloads the asset.
    .PARAMETER Source
        Proxy source from Get-RaProxySource.
    .OUTPUTS
        System.String
    .EXAMPLE
        Find-RaLocalProxyZip (Get-RaProxySource $manifest)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Source)
    $package = $script:RaLocalPackagePath
    if (-not $package -or -not (Test-Path -LiteralPath $package -PathType Leaf)) { return $null }
    $full = (Resolve-Path -LiteralPath $package).ProviderPath
    foreach ($candidate in @($full, (Join-Path (Split-Path -Parent $full) $Source.ZipName))) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        if ((Get-RaCheckedFileSha256 $candidate -Sha256 $Source.ZipSha256 -Version $Source.Version) -eq $Source.ZipSha256.ToUpperInvariant()) {
            return $candidate
        }
    }
    return $null
}

function Add-RaProxyFile {
    <#
    .SYNOPSIS
        Puts a release's verified proxy version.dll into its version folder (Proxy method only).
    .DESCRIPTION
        Called when <root>\<version>\proxy\version.dll is missing: the release ships the proxy as
        a separate asset (CAPIE 1.6.1+), the default Launcher method skipped it, or an antivirus
        removed it. Takes the proxy ZIP from next to -PackagePath or downloads it from the
        release (on request only: this runs only for -LoadMethod Proxy), checks the ZIP's signed
        SHA-256, extracts only proxy/version.dll with its own hash check and moves it into place.
    .PARAMETER Manifest
        Verified manifest of the installed version.
    .OUTPUTS
        System.String (path of the proxy in the version folder)
    .EXAMPLE
        Add-RaProxyFile $manifest
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest)
    $source = Get-RaProxySource $Manifest
    if (-not $source) { throw 'Release does not contain the signed proxy/version.dll asset. Use -LoadMethod Launcher for an older release.' }
    $target = Join-Path (Get-RaVersionDir $Manifest.Version) ($script:RaProxyBundlePath -replace '/', '\')
    Initialize-RaRoot
    $package = Get-RaPackageZip -Manifest $source -LocalZip (Find-RaLocalProxyZip $source) -ZipUrl $null
    $staging = Join-Path $script:RaRoot ($script:RaStagingPrefix + (New-RaUniqueName))
    try {
        $hash = Get-RaCheckedFileSha256 $package.Path -Sha256 $source.ZipSha256 -Version $source.Version -JustWritten:$package.Downloaded
        if ($hash -ne $source.ZipSha256.ToUpperInvariant()) {
            throw "Proxy package SHA256 mismatch: expected $($source.ZipSha256), got $hash. The proxy was not installed."
        }
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-RaVerifiedZip -Manifest $source -ZipPath $package.Path -Destination $staging -Only @($script:RaProxyBundlePath)
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
        [IO.File]::Move((Join-Path $staging ($script:RaProxyBundlePath -replace '/', '\')), $target)
    } finally {
        Remove-RaTempItem $staging -Recurse
        if ($package.Downloaded) { Remove-RaTempItem $package.Path }
    }
    Write-RaLog "Proxy of $($Manifest.Version) verified: $target"
    return $target
}

function Install-RaProxy {
    <#
    .SYNOPSIS
        Installs or updates the proxy from a fully verified release.
    .DESCRIPTION
        Uses a new temporary file and an atomic replace for a previously owned file.
        New destinations use File.Move, which cannot overwrite a racing foreign file.
        The previous proxy is restored if installation or verification fails. When the version
        folder has no proxy/version.dll yet, Add-RaProxyFile fetches the signed proxy asset first.
        An antivirus block is reported with the player-facing explanation.
    .PARAMETER State
        Installer state, modified in memory.
    .PARAMETER GameDir
        Verified HOI4 installation directory.
    .PARAMETER Version
        Installed release to use (defaults to the state's active version).
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaProxy $state $gameDir
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$GameDir,
        [Parameter(Position = 2)][AllowNull()][AllowEmptyString()][string]$Version
    )
    $path = Get-RaProxyPath $GameDir
    if (-not $Version) { $Version = $State.InstalledVersion }
    $manifest = Read-RaStoredManifest $Version
    $exists = Test-Path -LiteralPath $path
    if ($exists -and -not (Test-RaOwnProxy $State $path)) {
        throw "Foreign or modified version.dll left untouched: $path. Choose -LoadMethod Launcher or resolve the conflict manually."
    }
    if ($script:RaDry) { Write-RaLog "[dry-run] would install verified proxy $path from $Version" -Level PLAN; return }
    $sha = Get-RaManifestProxySha256 $manifest
    if (-not $sha) {
        throw 'Release does not contain the signed proxy/version.dll asset. Use -LoadMethod Launcher for an older release.'
    }
    $sourceDir = Get-RaVersionDir $Version
    if (-not (Test-RaInstalledVersion $manifest $sourceDir)) { throw 'Release bundle failed verification; proxy was not installed.' }
    if ($State.Proxy -and $State.Proxy.Path -ine $path) {
        throw 'A proxy is already recorded in another game directory. Uninstall it before choosing another directory.'
    }
    if ($exists -and (Get-RaFileSha256 $path) -eq $sha) { $State.Proxy = [pscustomobject]@{ Path = $path; Sha256 = $sha; Version = $Version }; return }
    $source = Join-Path $sourceDir ($script:RaProxyBundlePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { $source = Add-RaProxyFile $manifest }
    $tmp = $path + '.' + (New-RaUniqueName) + '.tmp'
    $backup = $path + '.' + (New-RaUniqueName) + '.bak'
    $installed = $false
    $success = $false
    $oldRecord = $State.Proxy
    try {
        try { [IO.File]::Copy($source, $tmp, $false) } catch {
            if (Test-RaAntivirusError $_) { throw (Get-RaAntivirusText -Path $source -Sha256 $sha -Version $Version) }
            throw
        }
        if ((Get-RaCheckedFileSha256 $tmp -Sha256 $sha -Version $Version -JustWritten) -ne $sha) { throw 'Staged proxy hash mismatch.' }
        if ($exists) {
            if (-not (Test-RaOwnProxy $State $path)) { throw 'Proxy changed during installation; left untouched.' }
            [IO.File]::Replace($tmp, $path, $backup)
        } else { [IO.File]::Move($tmp, $path) }
        $installed = $true
        if ($exists -and (Get-RaFileSha256 $backup) -ine $oldRecord.Sha256) { throw 'Proxy changed during replacement; restoring the preserved file.' }
        if ((Get-RaFileSha256 $path) -ne $sha) { throw 'Installed proxy hash mismatch.' }
        $State.Proxy = [pscustomobject]@{ Path = $path; Sha256 = $sha; Version = $Version }
        Write-RaLog "Installed CAPIE proxy: $path ($sha)"
        $success = $true
    } catch {
        $State.Proxy = $oldRecord
        if ($installed -and (Test-Path -LiteralPath $backup)) { [IO.File]::Replace($backup, $path, $null) }
        elseif ($installed -and (Test-Path -LiteralPath $path) -and (Get-RaFileSha256 $path) -eq $sha) { [IO.File]::Delete($path) }
        throw
    } finally {
        # Cleanup is logged, never thrown: after the success point a locked temporary file must not
        # report a working proxy as failed, and on failure it must not hide the first error.
        Remove-RaTempItem $tmp
        # A failed restoration must retain its recovery copy.
        if ($success) { Remove-RaTempItem $backup }
    }
}

function Remove-RaProxy {
    <#
    .SYNOPSIS
        Removes only a hash-proven owned proxy and its activation marker.
    .DESCRIPTION
        Modified DLLs remain in place. A locked owned DLL causes failure before bundle
        removal, preserving the state needed to retry after closing the game.
    .PARAMETER State
        Installer state, modified in memory.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaProxy $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if ($State.Proxy) {
        $path = $State.Proxy.Path
        if (Test-Path -LiteralPath $path) {
            if (Test-RaOwnProxy $State $path) {
                [void](Get-RaProxyPath (Split-Path -Parent $path))
                if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned proxy $path" -Level PLAN }
                else { Remove-RaOwnedFile -Path $path -Sha256 $State.Proxy.Sha256; Write-RaLog "Removed owned proxy $path" }
            } else { Write-RaLog "Foreign or modified proxy left untouched: $path" -Level WARN }
        }
        if (-not $script:RaDry) { $State.Proxy = $null }
    }
    $config = Join-Path $script:RaRoot $script:RaProxyConfigName
    if (Test-Path -LiteralPath $config) {
        $item = Get-Item -LiteralPath $config -Force
        if (-not $item.PSIsContainer -and -not (Test-RaReparsePoint $item) -and $State.ProxyConfigSha256 -and
            (Get-RaFileSha256 $config) -ieq $State.ProxyConfigSha256) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would remove owned activation marker $config" -Level PLAN }
            else { Remove-RaOwnedFile -Path $config -Sha256 $State.ProxyConfigSha256; $State.ProxyConfigSha256 = $null }
        } else { throw "Foreign or modified activation config left untouched: $config. Move it out before uninstalling the bundle." }
    }
}
