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
        # Cleanup never replaces the outcome: a locked leftover is logged (and swept by a later run).
        Remove-RaTempItem $staging -Recurse
        if ($package.Downloaded) { Remove-RaTempItem $package.Path }
    }
    Save-RaStoredManifest $Manifest
    Write-RaLog "Installed version $($Manifest.Version) to $dir"
    return $dir
}

function Remove-RaTempItem {
    <#
    .SYNOPSIS
        Deletes a temporary file or folder of this run; logs instead of throwing on failure.
    .DESCRIPTION
        For cleanup in finally blocks: once the real work succeeded (or failed with its own
        error), a temporary item that cannot be deleted (locked by an antivirus scan, for
        example) must not turn the result into a failure or hide the first error. A
        reparse point is only unlinked, never recursed into. A missing path is a no-op.
    .PARAMETER Path
        Temporary item.
    .PARAMETER Recurse
        The item is a folder of our own (staging) and may be deleted with its content.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaTempItem $staging -Recurse
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][AllowNull()][string]$Path,
        [switch]$Recurse
    )
    if (-not $Path) { return }
    try {
        $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        if (-not $item) { return }
        if (Test-RaReparsePoint $item) { [IO.Directory]::Delete($item.FullName, $false) }
        elseif ($item.PSIsContainer) {
            if (-not $Recurse) { throw 'folder given without -Recurse' }
            Remove-Item -LiteralPath $item.FullName -Recurse -Force -ErrorAction Stop
        } else { [IO.File]::Delete($item.FullName) }
    } catch { Write-RaLog "Could not delete temporary $Path (a later run removes it): $($_.Exception.Message)" -Level WARN }
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
        Creates the new junction under a temporary name first (current.new-<id>), renames
        the old link aside (current.old-<id>), renames the new one to 'current', verifies it
        and only then deletes the old link. The old link is never deleted before the new one
        is in place: on any failure it is renamed back. (The two renames are back to back;
        Windows has no rename that replaces a directory link in one step.) A 'current' that
        is not a junction is refused, never deleted.
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
    $existing = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
    if ($existing -and -not (Test-RaReparsePoint $existing)) { throw "$link is not a junction; refusing to replace it." }
    $id = New-RaUniqueName
    $tmp = "$link$($script:RaJunctionTempInfix)$id"
    $aside = $null
    New-Item -ItemType Junction -Path $tmp -Target $target | Out-Null
    try {
        if ($existing) {
            $aside = "$link$($script:RaJunctionOldInfix)$id"
            [IO.Directory]::Move($link, $aside)
        }
        if ($script:RaBeforeCurrentSwap) { & $script:RaBeforeCurrentSwap $aside }
        [IO.Directory]::Move($tmp, $link)
        if ((Get-RaJunctionTarget $link) -ne [IO.Path]::GetFullPath($target)) { throw 'current junction verification failed.' }
    } catch {
        $err = $_.Exception.Message
        if (Test-Path -LiteralPath $tmp) { [IO.Directory]::Delete($tmp, $false) }
        if ($aside -and (Test-Path -LiteralPath $aside)) {
            if (Test-Path -LiteralPath $link) { Remove-RaJunction $link }
            [IO.Directory]::Move($aside, $link)
        } else {
            if ((Test-Path -LiteralPath $link) -and (Get-RaJunctionTarget $link) -ne $old) { Remove-RaJunction $link }
            if ($old -and -not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $old | Out-Null }
        }
        throw "Could not switch the current version: $err"
    }
    Write-RaLog "Active version: $Version ($link)"
    if ($aside) { Remove-RaTempItem $aside }
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
        A version folder that a recorded Steam launch option starts directly (older setups
        wrote <root>\<version>\RenameAnythingLauncher.exe) is kept too. The stored manifest
        of a removed version is deleted too.
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
    $keep = @(@($State.InstalledVersion, $State.PreviousVersion) | Where-Object { $_ })
    $rootFull = [IO.Path]::GetFullPath($script:RaRoot).TrimEnd('\')
    foreach ($entry in @($State.SteamEntries | Where-Object { $_ })) {
        $launcherPath = if ($entry.PSObject.Properties['LauncherPath']) { [string]$entry.LauncherPath } else { '' }
        if (-not $launcherPath) { continue }
        try {
            $versionDir = Split-Path -Parent ([IO.Path]::GetFullPath($launcherPath))
            if ((Split-Path -Parent $versionDir) -ieq $rootFull) { $keep += (Split-Path -Leaf $versionDir) }
        } catch { Write-Verbose "Unusable launcher path in state: $launcherPath" }
    }
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

function Test-RaLeftoverAge {
    <#
    .SYNOPSIS
        Tests whether a leftover is old enough to belong to a finished (crashed) run.
    .DESCRIPTION
        Uses the later of creation and last write time (a copied file keeps the source's write
        time), so the temporary files of a second setup window that runs right now are kept.
    .PARAMETER Item
        File or folder.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaLeftoverAge (Get-Item -LiteralPath $p -Force)
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][System.IO.FileSystemInfo]$Item)
    $touched = if ($Item.CreationTime -gt $Item.LastWriteTime) { $Item.CreationTime } else { $Item.LastWriteTime }
    return (((Get-RaNow) - $touched).TotalMinutes -ge $script:RaLeftoverMinAgeMinutes)
}

function Test-RaPlainTree {
    <#
    .SYNOPSIS
        Tests that a folder holds only plain files and folders (no junction or symlink anywhere).
    .DESCRIPTION
        A staging folder is deleted recursively only when this holds, so a planted link can never
        make the sweep delete something outside our root folder. Never follows reparse points.
    .PARAMETER Path
        Folder to check.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaPlainTree $staging
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $pending = New-Object System.Collections.Generic.Stack[string]
    $pending.Push($Path)
    while ($pending.Count -gt 0) {
        $dir = New-Object System.IO.DirectoryInfo($pending.Pop())
        if (Test-RaReparsePoint $dir) { return $false }
        foreach ($child in $dir.GetFileSystemInfos()) {
            if (Test-RaReparsePoint $child) { return $false }
            if ($child -is [System.IO.DirectoryInfo]) { $pending.Push($child.FullName) }
        }
    }
    return $true
}

function Get-RaKnownProxyHash {
    <#
    .SYNOPSIS
        Lists the SHA-256 of every signed proxy/version.dll this setup knows.
    .DESCRIPTION
        From the pinned release and every stored manifest under <root>\manifests. A leftover
        version.dll copy in the game folder is ours only when its hash is in this list.
    .OUTPUTS
        System.String[] (upper-case hashes)
    .EXAMPLE
        Get-RaKnownProxyHash
    #>
    [CmdletBinding()]
    [OutputType([string[]], [object[]])]
    param()
    $hashes = @()
    $manifests = @($script:RaPinnedRelease)
    $store = Join-Path $script:RaRoot $script:RaManifestStoreDirName
    if (Test-Path -LiteralPath $store -PathType Container) {
        foreach ($f in @(Get-ChildItem -LiteralPath $store -File -Force -Filter '*.json')) {
            $m = Read-RaStoredManifest ([IO.Path]::GetFileNameWithoutExtension($f.Name))
            if ($m) { $manifests += $m }
        }
    }
    foreach ($m in $manifests) {
        if ($m.Files.Contains($script:RaProxyBundlePath)) { $hashes += ([string]$m.Files[$script:RaProxyBundlePath]).ToUpperInvariant() }
    }
    return @($hashes | Sort-Object -Unique)
}

function Clear-RaLeftover {
    <#
    .SYNOPSIS
        Removes what an interrupted earlier run left behind, after proving each item is ours.
    .DESCRIPTION
        In the root folder: download-<id>.zip files, .staging-<id> folders that hold only plain
        files, current.new-<id> / current.old-<id> junctions that point into the root folder,
        and proxy.enabled.<id>.remove files whose bytes are exactly our activation marker. In the
        game folders of the recorded proxy and of the found game: the files version.dll.<id>.tmp,
        version.dll.<id>.bak and version.dll.<id>.remove whose SHA-256 is a signed CAPIE proxy
        (Get-RaKnownProxyHash); a .bak is
        kept while no version.dll exists next to it (it may be the only copy). <id> is the
        32-digit name New-RaUniqueName makes, and every item must be older than
        RaLeftoverMinAgeMinutes. Anything else is left alone. Never fails the run: a locked item
        is logged and tried again next time. Dry-run mode only lists what would go.
    .PARAMETER State
        State object (read only).
    .PARAMETER GameDir
        Game folder found through Steam, or empty.
    .OUTPUTS
        System.Int32 (number of removed, or in dry-run mode listed, items)
    .EXAMPLE
        [void](Clear-RaLeftover $state $gameDir)
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    $found = New-Object System.Collections.ArrayList
    if ($script:RaRoot -and (Test-Path -LiteralPath $script:RaRoot -PathType Container)) {
        $rootFull = [IO.Path]::GetFullPath($script:RaRoot).TrimEnd('\') + '\'
        foreach ($item in @(Get-ChildItem -LiteralPath $script:RaRoot -Force)) {
            try {
                $reparse = Test-RaReparsePoint $item
                $ours = $false
                $plainFile = -not $item.PSIsContainer -and -not $reparse
                if ($item.Name -match $script:RaLeftoverDownloadPattern) { $ours = $plainFile }
                elseif ($item.Name -match $script:RaLeftoverStagingPattern) {
                    $ours = $item.PSIsContainer -and -not $reparse -and (Test-RaPlainTree $item.FullName)
                } elseif ($item.Name -match $script:RaLeftoverJunctionPattern) {
                    $target = Get-RaJunctionTarget $item.FullName
                    $ours = $reparse -and $target -and ($target.TrimEnd('\') + '\').StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)
                } elseif ($item.Name -match $script:RaLeftoverMarkerPattern -and $plainFile) {
                    $ours = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($item.FullName)) -ceq $script:RaProxyConfigContent
                }
                if ($ours -and (Test-RaLeftoverAge $item)) { [void]$found.Add($item) }
            } catch { Write-RaLog "Leftover check of $($item.FullName) failed; kept: $($_.Exception.Message)" -Level WARN }
        }
    }
    $gameDirs = @()
    if ($State.Proxy -and $State.Proxy.Path) { $gameDirs += (Split-Path -Parent ([string]$State.Proxy.Path)) }
    if ($GameDir) { $gameDirs += $GameDir }
    $proxyHashes = $null
    foreach ($dir in @($gameDirs | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Sort-Object -Unique)) {
        foreach ($item in @(Get-ChildItem -LiteralPath $dir -File -Force | Where-Object { $_.Name -match $script:RaLeftoverProxyPattern })) {
            try {
                if ((Test-RaReparsePoint $item) -or -not (Test-RaLeftoverAge $item)) { continue }
                if ($null -eq $proxyHashes) { $proxyHashes = @(Get-RaKnownProxyHash) }
                if ($proxyHashes -notcontains (Get-RaFileSha256 $item.FullName)) { continue }
                if ($item.Name.EndsWith('.bak') -and -not (Test-Path -LiteralPath (Join-Path $dir $script:RaProxyFileName))) {
                    Write-RaLog "Kept $($item.FullName): no $($script:RaProxyFileName) next to it, so it may be the only copy." -Level WARN
                    continue
                }
                [void]$found.Add($item)
            } catch { Write-RaLog "Leftover check of $($item.FullName) failed; kept: $($_.Exception.Message)" -Level WARN }
        }
    }
    foreach ($item in $found) {
        if ($script:RaDry) { Write-RaLog "[dry-run] would remove leftover $($item.FullName)" -Level PLAN; continue }
        Remove-RaTempItem $item.FullName -Recurse
        if (-not (Test-Path -LiteralPath $item.FullName)) { Write-RaLog "Removed leftover of an interrupted run: $($item.FullName)" }
    }
    return $found.Count
}
