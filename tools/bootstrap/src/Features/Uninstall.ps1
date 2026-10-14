# ---------------------------------------------------------------------------
# Slice: uninstall (-Uninstall, -Uninstall -Purge) and disable (-Disable).
# ---------------------------------------------------------------------------

function Invoke-RaUninstall {
    <#
    .SYNOPSIS
        Removes the Steam launch option part, our shortcuts and the installed files.
    .DESCRIPTION
        Steam first: while Steam keeps the launch option pointing at the launcher, no
        file is removed. Then the proxy, shortcuts and the "CAPIE" switch mod (earlier files
        restored), then the 'current' junction (so nothing can be
        deleted through it), then everything under the root folder except bootstrap.log.
    .PARAMETER State
        State object (modified).
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-RaUninstall $state $steam
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    if ($SteamRootPath) {
        if (-not (Remove-RaSteamLaunchOption $State $SteamRootPath)) {
            throw 'Steam is running: the launch option still points to the launcher, so no files were removed. Close Steam and run -Uninstall again.'
        }
    } else {
        Write-RaLog 'Steam not found; skipping launch options.' -Level WARN
    }
    Remove-RaProxy $State
    Remove-RaOwnShortcut $State
    Remove-RaPlaysetMod $State
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return }
    $log = Get-RaLogPath
    $link = Get-RaCurrentLink
    if (Test-Path -LiteralPath $link) {
        if ($script:RaDry) { Write-RaLog "[dry-run] would delete junction $link" -Level PLAN } else { Remove-RaJunction $link }
    }
    foreach ($item in Get-ChildItem -LiteralPath $script:RaRoot -Force) {
        if ($item.FullName -ieq $log -or $item.FullName -ieq $link) { continue }
        if ($script:RaDry) {
            Write-RaLog "[dry-run] would delete $($item.FullName)" -Level PLAN
            continue
        }
        if (Test-RaReparsePoint $item) {
            [IO.Directory]::Delete($item.FullName, $false)
            continue
        }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
    Write-RaLog "Removed files in $($script:RaRoot) (kept $($script:RaLogFileName))"
}

function Invoke-RaDisable {
    <#
    .SYNOPSIS
        Turns CAPIE off without removing files (-Disable).
    .DESCRIPTION
        Removes only our part of the HOI4 Steam launch options (restoring the exact earlier
        value when nobody changed it since) and our proxy activation marker, if any. The next
        start from Steam is the clean game. Installed files stay, so -SteamLaunchOption turns
        CAPIE on again without a download.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        None.
    .EXAMPLE
        Invoke-RaDisable $state $steam
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$SteamRootPath
    )
    if ($SteamRootPath) {
        if (-not (Remove-RaSteamLaunchOption $State $SteamRootPath)) { throw 'Steam is running; launch option not changed.' }
    } else {
        Write-RaLog 'Steam not found; launch options unchanged.' -Level WARN
    }
    if ($State.ProxyActivation -eq 'Config') { Set-RaProxyActivation $State 'Flag' }
}

function Remove-RaSteamBackup {
    <#
    .SYNOPSIS
        Deletes the localconfig.vdf backups this script made in every Steam account.
    .DESCRIPTION
        Only files named exactly like New-RaBackupPath names them
        (localconfig.vdf.renameanything-backup-<yyyyMMdd-HHmmss>[-n]) in userdata\<id>\config
        are removed; any other file is left alone. Called by -Uninstall -Purge after the launch
        options were restored.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Int32 (number of deleted or, in dry-run mode, planned files)
    .EXAMPLE
        Remove-RaSteamBackup $steam
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $count = 0
    foreach ($acc in @(Get-RaSteamAccount $SteamRootPath)) {
        $dir = Split-Path -Parent $acc.ConfigPath
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        foreach ($item in @(Get-ChildItem -LiteralPath $dir -File -Force | Where-Object { $_.Name -match $script:RaBackupNamePattern })) {
            if ($script:RaDry) { Write-RaLog "[dry-run] would delete backup $($item.FullName)" -Level PLAN } else {
                Remove-Item -LiteralPath $item.FullName -Force
                Write-RaLog "Deleted backup $($item.FullName)"
            }
            $count++
        }
    }
    return $count
}

function Invoke-RaPurge {
    <#
    .SYNOPSIS
        Removes the last traces after a successful uninstall (-Uninstall -Purge).
    .DESCRIPTION
        Deletes our localconfig.vdf backups, then bootstrap.log, then the root folder when it
        is empty. Must run last: any later log line would create the log again. A root folder
        that still holds foreign files is kept and reported.
    .PARAMETER SteamRootPath
        Steam folder or empty when Steam was not found.
    .OUTPUTS
        System.Boolean ($true when nothing of the installation is left)
    .EXAMPLE
        Invoke-RaPurge $steam
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$SteamRootPath)
    if ($SteamRootPath) { [void](Remove-RaSteamBackup $SteamRootPath) }
    if (-not (Test-Path -LiteralPath $script:RaRoot)) { return $true }
    $rest = @(Get-ChildItem -LiteralPath $script:RaRoot -Force | Where-Object { $_.FullName -ine (Get-RaLogPath) })
    if ($rest.Count -gt 0) {
        Write-RaLog "Kept $($script:RaRoot): it contains files that are not ours ($($rest[0].Name))." -Level WARN
        return $false
    }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would delete $(Get-RaLogPath) and $($script:RaRoot)" -Level PLAN
        return $true
    }
    if (Test-Path -LiteralPath (Get-RaLogPath)) { Remove-Item -LiteralPath (Get-RaLogPath) -Force }
    [IO.Directory]::Delete($script:RaRoot, $false)
    return $true
}