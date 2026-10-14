# ---------------------------------------------------------------------------
# Slice: the "CAPIE" switch mod in the player's mod folder
# (Documents\Paradox Interactive\Hearts of Iron IV\mod). The release bundle carries it under
# mod/ (mod/capie.mod and mod/capie/...). -Install, -Update and -Rollback copy it from the
# verified version folder; -Uninstall removes it again. Only files recorded in state.json with
# their SHA-256 are ever replaced or removed; a file that was there before is backed up first
# (byte-verified copy) and restored by -Uninstall; a file the player changed is left alone.
# ---------------------------------------------------------------------------

function Get-RaPlaysetModRoot {
    <#
    .SYNOPSIS
        The game's user mod folder.
    .DESCRIPTION
        <Documents>\Paradox Interactive\Hearts of Iron IV\mod, Documents from the adapter.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaPlaysetModRoot
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path ([string](& $script:RaDocumentsPath)) $script:RaGameUserModRelPath)
}

function Get-RaPlaysetModTarget {
    <#
    .SYNOPSIS
        Maps a bundle path under mod/ to its place in the mod folder.
    .DESCRIPTION
        mod/capie.mod -> <mod folder>\capie.mod, mod/capie/x -> <mod folder>\capie\x.
    .PARAMETER BundlePath
        Manifest path starting with mod/.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaPlaysetModTarget 'mod/capie.mod'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][string]$BundlePath)
    $relative = $BundlePath.Substring($script:RaModBundlePrefix.Length) -replace '/', '\'
    return (Join-Path (Get-RaPlaysetModRoot) $relative)
}

function Find-RaPlaysetModRecord {
    <#
    .SYNOPSIS
        The state.json record of one mod file, or $null.
    .DESCRIPTION
        Records: Path, Sha256 (what we wrote), Backup (our copy of a file that was there before, or $null).
    .PARAMETER State
        State object.
    .PARAMETER Path
        Target path.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        Find-RaPlaysetModRecord $state $target
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State, [Parameter(Mandatory, Position = 1)][string]$Path)
    return (@($State.ModFiles) | Where-Object { $_ -and $_.Path -ieq $Path } | Select-Object -First 1)
}

function Write-RaVerifiedFile {
    <#
    .SYNOPSIS
        Writes bytes through a temporary file and verifies the result by SHA-256.
    .DESCRIPTION
        The temporary file is moved over the target (replace) and the target hash must equal
        the expected one, else the call throws.
    .PARAMETER Source
        Verified source file (inside the version folder).
    .PARAMETER Target
        Destination.
    .PARAMETER Sha256
        Expected hash.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaVerifiedFile -Source $src -Target $dest -Sha256 $hash
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Source,
        [Parameter(Mandatory, Position = 1)][string]$Target,
        [Parameter(Mandatory, Position = 2)][string]$Sha256
    )
    $parent = Split-Path -Parent $Target
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $tmp = "$Target$($script:RaWriteTempSuffix)"
    & $script:RaCopyFile $Source $tmp
    & $script:RaCopyFile $tmp $Target
    Remove-Item -LiteralPath $tmp -Force
    if ((Get-RaFileSha256 $Target) -ine $Sha256) { throw "Verification failed after writing $Target" }
}

function Install-RaPlaysetMod {
    <#
    .SYNOPSIS
        Copies the "CAPIE" switch mod of the active version into the player's mod folder.
    .DESCRIPTION
        For each mod/ file of the active version's manifest: identical file present -> only
        recorded; our older file (hash recorded) -> replaced; a file that was there before ->
        backed up (byte-verified copy next to it) and replaced; every written file is verified
        by SHA-256. Our files that the new version no longer has are removed (hash-checked).
        A release without mod/ files changes nothing. No-op in dry-run mode except for plan lines.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaPlaysetMod $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if (-not $State.InstalledVersion) { return }
    $manifest = Read-RaStoredManifest $State.InstalledVersion
    if (-not $manifest) { return }
    $bundle = @($manifest.Files.Keys | Where-Object { ([string]$_).StartsWith($script:RaModBundlePrefix) })
    if ($bundle.Count -eq 0) {
        Write-RaLog "Release $($State.InstalledVersion) has no CAPIE switch mod; mod folder unchanged." -NoConsole
        return
    }
    $versionDir = Get-RaVersionDir $State.InstalledVersion
    $records = @()
    foreach ($entry in $bundle) {
        $target = Get-RaPlaysetModTarget $entry
        $hash = ([string]$manifest.Files[$entry]).ToUpperInvariant()
        $record = Find-RaPlaysetModRecord $State $target
        $backup = if ($record) { $record.Backup } else { $null }
        if ($script:RaDry) {
            Write-RaLog "[dry-run] would place the CAPIE mod file $target" -Level PLAN
            continue
        }
        if (Test-Path -LiteralPath $target -PathType Container) { throw "A folder is in the way of the CAPIE mod file: $target" }
        $current = if (Test-Path -LiteralPath $target) { Get-RaFileSha256 $target } else { $null }
        if ($current -ne $hash) {
            if ($current -and -not ($record -and $current -ieq $record.Sha256)) {
                $backup = New-RaBackupPath $target
                $original = [byte[]](& $script:RaReadAllBytes $target)
                & $script:RaWriteAllBytes $backup $original
                if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $backup)) $original)) { throw "Backup verification failed: $backup" }
                Write-RaLog "Backup of an existing mod file: $backup"
            }
            Write-RaVerifiedFile -Source (Join-Path $versionDir ($entry -replace '/', '\')) -Target $target -Sha256 $hash
        }
        $records += [pscustomobject]@{ Path = $target; Sha256 = $hash; Backup = $backup }
    }
    if ($script:RaDry) { return }
    foreach ($old in @($State.ModFiles | Where-Object { $_ })) {
        if (@($records | Where-Object { $_.Path -ieq $old.Path }).Count -gt 0) { continue }
        $records += $old   # no longer in the release: kept on record so -Uninstall still handles it
    }
    $State.ModFiles = $records
    Write-RaLog ("CAPIE switch mod installed in {0}: enable 'CAPIE' in your playset." -f (Get-RaPlaysetModRoot))
}

function Remove-RaPlaysetMod {
    <#
    .SYNOPSIS
        Removes our switch mod files and restores what was there before.
    .DESCRIPTION
        A file still carrying our recorded hash is removed through the owned-file quarantine;
        a changed file is kept and reported. A backup we made is moved back afterwards. Empty
        folders that held our files are removed, never the mod folder itself.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaPlaysetMod $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $kept = @()
    $root = Get-RaPlaysetModRoot
    foreach ($record in @($State.ModFiles | Where-Object { $_ })) {
        $path = [string]$record.Path
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            if ((Get-RaFileSha256 $path) -ine $record.Sha256) {
                Write-RaLog "Mod file changed since installation; left in place: $path" -Level WARN
                $kept += $record
                continue
            }
            Remove-RaOwnedFile -Path $path -Sha256 $record.Sha256
        }
        if ($record.Backup -and (Test-Path -LiteralPath $record.Backup -PathType Leaf)) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would restore $path from $($record.Backup)" -Level PLAN; continue }
            $original = [byte[]](& $script:RaReadAllBytes $record.Backup)
            & $script:RaWriteAllBytes $path $original
            if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $path)) $original)) { throw "Restore verification failed: $path" }
            [IO.File]::Delete($record.Backup)
            Write-RaLog "Restored the earlier file $path"
        }
    }
    if (-not $script:RaDry) {
        $folders = @{}
        foreach ($record in @($State.ModFiles | Where-Object { $_ })) {
            $folder = Split-Path -Parent ([string]$record.Path)
            while ($folder -and $folder.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                $folders[$folder] = $true
                $folder = Split-Path -Parent $folder
            }
        }
        foreach ($folder in @($folders.Keys | Sort-Object { $_.Length } -Descending)) {
            if ((Test-Path -LiteralPath $folder) -and @(Get-ChildItem -LiteralPath $folder -Force).Count -eq 0) {
                [IO.Directory]::Delete($folder, $false)
            }
        }
        $State.ModFiles = $kept
    }
}
