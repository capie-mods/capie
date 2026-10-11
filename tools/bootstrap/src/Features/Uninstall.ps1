# ---------------------------------------------------------------------------
# Slice: uninstall (-Uninstall). Keeps bootstrap.log.
# ---------------------------------------------------------------------------

function Invoke-RaUninstall {
    <#
    .SYNOPSIS
        Removes the Steam launch option part, our shortcuts and the installed files.
    .DESCRIPTION
        Steam first: while Steam keeps the launch option pointing at the launcher, no
        file is removed. Then shortcuts, then the 'current' junction (so nothing can be
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
    Remove-RaOwnShortcut $State
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
