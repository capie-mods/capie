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

function Install-RaLatest {
    <#
    .SYNOPSIS
        Installs and activates the newest signed release from GitHub Releases (-Install -Latest).
    .DESCRIPTION
        The path of the release installer: one GitHub API request for the latest release, its
        release-manifest.json and .sig (RSA signature checked with the key pinned in this
        script, tag = manifest version, asset URLs inside this repository), then the ZIP: its
        SHA-256 before extraction, every file's SHA-256 after extraction, and a full
        re-verification before the version folder is moved into place under
        %LOCALAPPDATA%\RenameAnything. Nothing is written to the game folder. Offline:
        -ManifestPath (with the .sig next to it) and -PackagePath.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER LocalManifestPath
        Signed release-manifest.json for an offline install, or empty.
    .PARAMETER LocalZip
        Release ZIP for an offline install, or empty.
    .OUTPUTS
        System.String (the installed version)
    .EXAMPLE
        Install-RaLatest -State $state -LocalManifestPath $null -LocalZip $null
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$LocalManifestPath,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$LocalZip
    )
    $remote = Get-RaRemoteRelease $LocalManifestPath
    $m = $remote.Manifest
    [void](Install-RaVersion -Manifest $m -LocalZip $LocalZip -ZipUrl $remote.ZipUrl)
    Set-RaActiveVersion $State $m.Version
    return [string]$m.Version
}