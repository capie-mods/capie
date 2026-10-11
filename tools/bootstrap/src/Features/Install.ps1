# ---------------------------------------------------------------------------
# Slice: install (-Install) of the release pinned in this script.
# ---------------------------------------------------------------------------

function Install-RaPackage {
    <#
    .SYNOPSIS
        Installs and activates the release pinned in this script.
    .DESCRIPTION
        Refuses while the pinned manifest is a placeholder. A newer version that is
        already installed and verifies is kept (and re-activated) instead of being
        downgraded.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER LocalZip
        -PackagePath value or empty.
    .OUTPUTS
        None.
    .EXAMPLE
        Install-RaPackage $state $PackagePath
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalZip
    )
    $m = $script:RaPinnedRelease
    Test-RaManifest $m
    if ($State.InstalledVersion -and (Compare-RaVersion $State.InstalledVersion $m.Version) -gt 0) {
        $cur = Read-RaStoredManifest $State.InstalledVersion
        if ($cur -and (Test-RaInstalledVersion $cur (Get-RaVersionDir $State.InstalledVersion))) {
            Write-RaLog "A newer version ($($State.InstalledVersion)) is already installed; keeping it."
            Set-RaActiveVersion $State $State.InstalledVersion
            return
        }
    }
    [void](Install-RaVersion -Manifest $m -LocalZip $LocalZip -ZipUrl $null)
    Set-RaActiveVersion $State $m.Version
}
