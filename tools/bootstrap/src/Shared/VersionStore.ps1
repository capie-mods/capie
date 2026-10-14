# ---------------------------------------------------------------------------
# Version store: verified install of one version, the 'current' junction and
# pruning. Shared by the install, update and rollback slices.
# ---------------------------------------------------------------------------

function Get-RaPackageZip {
    <#
    .SYNOPSIS
        Returns the path of the release ZIP to verify, downloading it when needed.
    .DESCRIPTION
        A local -PackagePath wins. Otherwise -ZipUrl (from the signed manifest) or the
        default GitHub release URL is downloaded into the root folder. The result tells
        the caller whether the file must be deleted afterwards.
    .PARAMETER Manifest
        Manifest of the version being installed.
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .PARAMETER ZipUrl
        Asset URL from the update channel or empty.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Path, Downloaded)
    .EXAMPLE
        Get-RaPackageZip $m '' $null
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$ZipUrl
    )
    if ($LocalZip) {
        return [pscustomobject]@{ Path = (Resolve-Path -LiteralPath $LocalZip).ProviderPath; Downloaded = $false }
    }
    if (-not $ZipUrl) {
        if ($script:RaGitHubRepo -match $script:RaRepoPlaceholderMarker) {
            throw 'Release URL is still a placeholder; use -PackagePath with a downloaded release ZIP.'
        }
        $ZipUrl = ($script:RaGitHubDownloadPrefixFormat -f $script:RaGitHubRepo) + "$($script:RaReleaseTagPrefix)$($Manifest.Version)/$($Manifest.ZipName)"
    }
    Write-RaLog "Downloading $ZipUrl"
    $downloaded = Join-Path $script:RaRoot ($script:RaDownloadPrefix + (New-RaUniqueName) + '.zip')
    [IO.File]::WriteAllBytes($downloaded, (Get-RaHttpContent $ZipUrl))
    return [pscustomobject]@{ Path = $downloaded; Downloaded = $true }
}

function Install-RaVersion {
    <#
    .SYNOPSIS
        Installs one verified version into <root>\<version>.
    .DESCRIPTION
        Skips work when the folder already verifies. Otherwise obtains the ZIP, checks
        its SHA-256 before extraction, extracts into a staging folder with per-file
        checks, re-verifies the staging folder and only then moves it into place (a
        broken existing folder is moved aside). Stores the manifest for later rollback.
    .PARAMETER Manifest
        Manifest of the version.
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .PARAMETER ZipUrl
        Asset URL from the update channel or empty.
    .OUTPUTS
        System.String (the version folder)
    .EXAMPLE
        Install-RaVersion $m $PackagePath $null
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][System.Collections.IDictionary]$Manifest,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$ZipUrl
    )
    Test-RaManifest $Manifest
    $dir = Get-RaVersionDir $Manifest.Version
    if (Test-RaInstalledVersion $Manifest $dir) {
        Write-RaLog "Version $($Manifest.Version) already present and verified: $dir"
        return $dir
    }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would verify $($Manifest.ZipName) (sha256 $($Manifest.ZipSha256)) and install to $dir" -Level PLAN
        return $dir
    }
    Initialize-RaRoot
    $package = Get-RaPackageZip -Manifest $Manifest -LocalZip $LocalZip -ZipUrl $ZipUrl
    $staging = Join-Path $script:RaRoot ($script:RaStagingPrefix + (New-RaUniqueName))
    try {
        $hash = Get-RaFileSha256 $package.Path
        if ($hash -ne $Manifest.ZipSha256.ToUpperInvariant()) {
            throw "Package SHA256 mismatch: expected $($Manifest.ZipSha256), got $hash. Nothing was installed."
        }
        Write-RaLog "Package $($Manifest.ZipName): SHA256 matches the manifest."
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-RaVerifiedZip -Manifest $Manifest -ZipPath $package.Path -Destination $staging
        if (-not (Test-RaInstalledVersion $Manifest $staging)) { throw 'Staged files failed verification.' }
        if (Test-Path -LiteralPath $dir) {
            $broken = "$dir$($script:RaBrokenInfix)$((Get-RaNow).ToString($script:RaBrokenTimestampFormat))"
            Write-RaLog "Existing $dir failed verification; moved to $broken" -Level WARN
            Move-Item -LiteralPath $dir -Destination $broken
        }
        Move-Item -LiteralPath $staging -Destination $dir
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
        if ($package.Downloaded -and (Test-Path -LiteralPath $package.Path)) { Remove-Item -LiteralPath $package.Path -Force }
    }
    Save-RaStoredManifest $Manifest
    Write-RaLog "Installed version $($Manifest.Version) to $dir"
    return $dir
}

function Test-RaReparsePoint {
    <#
    .SYNOPSIS
        Tests whether a file system item is a reparse point (junction or symlink).
    .DESCRIPTION
        Used so that junctions are only ever unlinked, never recursed into.
    .PARAMETER Item
        File or directory info.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaReparsePoint (Get-Item -LiteralPath $p -Force)
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][System.IO.FileSystemInfo]$Item)
    return [bool]($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)
}

function Get-RaJunctionTarget {
    <#
    .SYNOPSIS
        Returns the full target path of a junction, or $null.
    .DESCRIPTION
        $null when the path does not exist or is not a reparse point.
    .PARAMETER Path
        Junction path.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaJunctionTarget (Get-RaCurrentLink)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item -or -not (Test-RaReparsePoint $item)) { return $null }
    $t = @($item.Target) | Select-Object -First 1
    if ($t) { return [IO.Path]::GetFullPath(($t -replace '^\\\\\?\\', '')) }
    return $null
}

function Remove-RaJunction {
    <#
    .SYNOPSIS
        Deletes a junction (the link only, never the target's content).
    .DESCRIPTION
        Refuses to delete anything that is not a reparse point. A missing path is a no-op.
    .PARAMETER Path
        Junction path.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaJunction (Get-RaCurrentLink)
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return }
    if (-not (Test-RaReparsePoint $item)) { throw "$Path is not a junction; refusing to delete it." }
    [IO.Directory]::Delete($Path, $false)
}

function Switch-RaCurrent {
    <#
    .SYNOPSIS
        Points <root>\current at <root>\<version>.
    .DESCRIPTION
        Creates the new junction under a temporary name first, then replaces the old one;
        on failure the previous link is restored. Verifies the result.
    .PARAMETER Version
        Version to activate (folder must exist).
    .OUTPUTS
        None.
    .EXAMPLE
        Switch-RaCurrent '0.2.0'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Version)
    $link = Get-RaCurrentLink
    $target = Get-RaVersionDir $Version
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would point $link -> $target" -Level PLAN
        return
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Version folder missing: $target" }
    $old = Get-RaJunctionTarget $link
    $tmp = "$link$($script:RaJunctionTempInfix)" + (New-RaUniqueName)
    New-Item -ItemType Junction -Path $tmp -Target $target | Out-Null
    try {
        if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
        [IO.Directory]::Move($tmp, $link)
        if ((Get-RaJunctionTarget $link) -ne [IO.Path]::GetFullPath($target)) { throw 'current junction verification failed.' }
        Write-RaLog "Active version: $Version ($link)"
    } catch {
        $err = $_.Exception.Message
        if (Test-Path -LiteralPath $tmp) { [IO.Directory]::Delete($tmp, $false) }
        if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
        if ($old) { New-Item -ItemType Junction -Path $link -Target $old | Out-Null }
        throw "Could not switch the current version: $err"
    }
}

function Set-RaActiveVersion {
    <#
    .SYNOPSIS
        Activates a version and records the previous one for rollback.
    .DESCRIPTION
        No-op when the version is already active and the junction points at it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER Version
        Version to activate.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaActiveVersion $state $m.Version
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Version
    )
    $link = Get-RaCurrentLink
    $oldTarget = Get-RaJunctionTarget $link
    $already = ($State.InstalledVersion -eq $Version) -and ($oldTarget -eq [IO.Path]::GetFullPath((Get-RaVersionDir $Version)))
    if ($already) { return }
    $oldProxy = $State.Proxy
    if ($oldProxy) { Install-RaProxy -State $State -GameDir (Split-Path -Parent $oldProxy.Path) -Version $Version }
    try { Switch-RaCurrent $Version } catch {
        if (-not $script:RaDry -and (Get-RaJunctionTarget $link) -ne $oldTarget) {
            if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
            if ($oldTarget) { New-Item -ItemType Junction -Path $link -Target $oldTarget | Out-Null }
        }
        if ($oldProxy) { Install-RaProxy -State $State -GameDir (Split-Path -Parent $oldProxy.Path) -Version $oldProxy.Version }
        throw
    }
    if ($State.InstalledVersion -and $State.InstalledVersion -ne $Version) { $State.PreviousVersion = $State.InstalledVersion }
    $State.InstalledVersion = $Version
}

function Remove-RaUnusedVersion {
    <#
    .SYNOPSIS
        Deletes installed versions other than the active and the previous one.
    .DESCRIPTION
        Never fails the run: a folder in use is logged and kept. Junctions are skipped.
        The stored manifest of a removed version is deleted too.
    .PARAMETER State
        State object (read only).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaUnusedVersion $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $keep = @($State.InstalledVersion, $State.PreviousVersion) | Where-Object { $_ }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    foreach ($d in Get-ChildItem -LiteralPath $script:RaRoot -Directory -Force) {
        if (Test-RaReparsePoint $d) { continue }
        if ($d.Name -match $script:RaVersionPattern -and $keep -notcontains $d.Name) {
            if ($script:RaDry) {
                Write-RaLog "[dry-run] would remove old version $($d.FullName)" -Level PLAN
                continue
            }
            try {
                Remove-Item -LiteralPath $d.FullName -Recurse -Force
                $mf = Get-RaManifestStorePath $d.Name
                if (Test-Path -LiteralPath $mf) { Remove-Item -LiteralPath $mf -Force }
                Write-RaLog "Removed old version $($d.FullName)"
            } catch { Write-RaLog "Could not remove $($d.FullName) (in use?): $($_.Exception.Message)" -Level WARN }
        }
    }
}
