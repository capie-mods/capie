# ---------------------------------------------------------------------------
# Steam discovery (read-only): install folder, accounts, game folder, process.
# ---------------------------------------------------------------------------

function Get-RaSteamRoot {
    <#
    .SYNOPSIS
        Finds the Steam folder.
    .DESCRIPTION
        Uses -Override when given (must exist), otherwise HKCU\Software\Valve\Steam\SteamPath
        through the registry adapter. Throws when Steam cannot be found.
    .PARAMETER Override
        Folder passed with -SteamRoot.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSteamRoot ''
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Override)
    if ($Override) {
        if (-not (Test-Path -LiteralPath $Override -PathType Container)) { throw "Steam folder not found: $Override" }
        return (Resolve-Path -LiteralPath $Override).ProviderPath
    }
    $p = $null
    try {
        $p = & $script:RaGetRegistryValue $script:RaSteamRegistryKey $script:RaSteamRegistryValue
    } catch { Write-Verbose "Steam registry value unavailable: $($_.Exception.Message)" }
    if (-not $p) { throw 'Steam not found (HKCU\Software\Valve\Steam\SteamPath missing).' }
    $p = [IO.Path]::GetFullPath(($p -replace '/', '\'))
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw "Steam folder from registry does not exist: $p" }
    return $p
}

function Get-RaSteamPersonaName {
    <#
    .SYNOPSIS
        Maps Steam account ids to persona names from config\loginusers.vdf.
    .DESCRIPTION
        Best effort: a missing or unreadable file yields an empty table.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Collections.Hashtable (account id -> persona name)
    .EXAMPLE
        (Get-RaSteamPersonaName $steam)['111']
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $names = @{}
    $login = Join-Path $SteamRootPath $script:RaSteamLoginUsersRelPath
    if (-not (Test-Path -LiteralPath $login)) { return $names }
    try {
        $root = ConvertFrom-RaVdf (Read-RaTextFile $login).Text
        foreach ($u in @(Find-RaVdfPath $root @($script:RaLoginUsersRootKey))) {
            foreach ($c in $u.Children) {
                if ($c.IsBlock -and $c.Key -match $script:RaDigitsPattern) {
                    $acc = [string]([uint64]$c.Key - $script:RaSteamId64Base)
                    $pn = @(Find-RaVdfChild $c $script:RaPersonaNameKey)
                    if ($pn.Count -eq 1) { $names[$acc] = $pn[0].Value }
                }
            }
        }
    } catch { Write-Verbose "loginusers.vdf unreadable: $($_.Exception.Message)" }
    return $names
}

function Get-RaSteamAccount {
    <#
    .SYNOPSIS
        Lists Steam accounts that have a localconfig.vdf.
    .DESCRIPTION
        Scans userdata\<id> (numeric, non-zero ids), sorted by name, and attaches the
        persona name when known.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Id, Name, ConfigPath)
    .EXAMPLE
        @(Get-RaSteamAccount $steam)
    #>
    [CmdletBinding()]
    [OutputType([psobject], [object[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $names = Get-RaSteamPersonaName $SteamRootPath
    $result = @()
    $ud = Join-Path $SteamRootPath $script:RaSteamUserDataDirName
    if (Test-Path -LiteralPath $ud) {
        foreach ($d in (Get-ChildItem -LiteralPath $ud -Directory | Sort-Object Name)) {
            if ($d.Name -notmatch $script:RaDigitsPattern -or $d.Name -eq $script:RaSteamAnonymousAccountId) { continue }
            $cfg = Join-Path $d.FullName $script:RaSteamLocalConfigRelPath
            if (Test-Path -LiteralPath $cfg -PathType Leaf) {
                $result += [pscustomobject]@{ Id = $d.Name; Name = $names[$d.Name]; ConfigPath = $cfg }
            }
        }
    }
    return $result
}

function Format-RaAccountList {
    <#
    .SYNOPSIS
        Comma-separated account ids for error messages.
    .DESCRIPTION
        Used by the account selection errors.
    .PARAMETER Accounts
        Accounts from Get-RaSteamAccount.
    .OUTPUTS
        System.String
    .EXAMPLE
        Format-RaAccountList $accounts
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Accounts)
    return (($Accounts | ForEach-Object Id) -join ', ')
}

function Test-RaSteamRunning {
    <#
    .SYNOPSIS
        Tests whether Steam is running (process adapter).
    .DESCRIPTION
        Only reads the process list; the script never stops Steam.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (Test-RaSteamRunning) { 'close Steam first' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    return (@(& $script:RaGetSteamProcess).Count -gt 0)
}

function Wait-RaSteamClosed {
    <#
    .SYNOPSIS
        Waits until the user has closed Steam.
    .DESCRIPTION
        Returns $true when Steam is not running. With -Yes it returns $false at once
        (and logs why); interactively it asks again until Steam is closed or the user
        types Q. It never stops the process itself.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Wait-RaSteamClosed)) { return $false }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    if (-not (Test-RaSteamRunning)) { return $true }
    if ($script:RaAssumeYes) {
        Write-RaLog (Get-RaText 'SteamRunningNonInteractive') -Level WARN
        return $false
    }
    while (Test-RaSteamRunning) {
        $a = Read-RaAnswer (Get-RaText 'SteamRunning')
        if ($a -ieq $script:RaQuitAnswer) { return $false }
    }
    return $true
}

function Select-RaSteamAccount {
    <#
    .SYNOPSIS
        Chooses the Steam accounts to edit.
    .DESCRIPTION
        -Selector "all" or a list of ids wins. Otherwise a single account is used
        directly; several accounts need -SteamAccount with -Yes, or an interactive
        choice (numbers separated by commas, or A for all).
    .PARAMETER Accounts
        Accounts from Get-RaSteamAccount.
    .PARAMETER Selector
        Value of -SteamAccount.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Select-RaSteamAccount @(Get-RaSteamAccount $steam) 'all'
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Accounts,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$Selector
    )
    if ($Accounts.Count -eq 0) { throw 'No Steam account with config\localconfig.vdf was found.' }
    if ($Selector) {
        if ($Selector -ieq $script:RaAllAccountsSelector) { return $Accounts }
        $ids = $Selector -split $script:RaListSeparatorPattern | Where-Object { $_ }
        $sel = @($Accounts | Where-Object { $ids -contains $_.Id })
        if ($sel.Count -ne @($ids).Count) { throw "Unknown Steam account id in -SteamAccount '$Selector'. Known: $(Format-RaAccountList $Accounts)" }
        return $sel
    }
    if ($Accounts.Count -eq 1) { return $Accounts }
    if ($script:RaAssumeYes) { throw "Several Steam accounts found; pass -SteamAccount <id>|all. Known: $(Format-RaAccountList $Accounts)" }
    for ($i = 0; $i -lt $Accounts.Count; $i++) {
        $n = if ($Accounts[$i].Name) { " ($($Accounts[$i].Name))" } else { '' }
        Write-RaHost ('  {0} = {1}{2}' -f ($i + 1), $Accounts[$i].Id, $n)
    }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskAccount')
        if ($a -ieq $script:RaAllAnswer) { return $Accounts }
        $nums = @($a -split $script:RaListSeparatorPattern | Where-Object { $_ -match $script:RaDigitsPattern } | ForEach-Object { [int]$_ })
        if ($nums.Count -gt 0 -and -not ($nums | Where-Object { $_ -lt 1 -or $_ -gt $Accounts.Count })) {
            return @($nums | Sort-Object -Unique | ForEach-Object { $Accounts[$_ - 1] })
        }
    }
}

function Get-RaSteamLibrary {
    <#
    .SYNOPSIS
        Lists Steam library folders (the Steam folder first).
    .DESCRIPTION
        Reads steamapps\libraryfolders.vdf in both the current (block with "path") and
        the legacy ("1" "D:\Lib") format. An unreadable file is logged and ignored.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Get-RaSteamLibrary $steam
    #>
    [CmdletBinding()]
    [OutputType([string[]], [object[]])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    $libs = @($SteamRootPath)
    $lf = Join-Path $SteamRootPath $script:RaSteamLibraryFoldersRelPath
    if (Test-Path -LiteralPath $lf) {
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $lf).Text
            foreach ($top in $root.Children) {
                foreach ($entry in $top.Children) {
                    if ($entry.IsBlock) {
                        $p = @(Find-RaVdfChild $entry $script:RaLibraryPathKey)
                        if ($p.Count -eq 1) { $libs += $p[0].Value }
                    } elseif ($entry.Key -match $script:RaDigitsPattern) { $libs += $entry.Value } # legacy format
                }
            }
        } catch { Write-RaLog "libraryfolders.vdf unreadable: $($_.Exception.Message)" -Level WARN -NoConsole }
    }
    return @($libs | Select-Object -Unique)
}

function Get-RaGameDir {
    <#
    .SYNOPSIS
        Finds the HOI4 installation folder through the Steam libraries.
    .DESCRIPTION
        Reads appmanifest_394360.acf in each library and returns the first
        steamapps\common\<installdir> that contains hoi4.exe, or $null.
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaGameDir $steam
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$SteamRootPath)
    foreach ($lib in (Get-RaSteamLibrary $SteamRootPath)) {
        $acf = Join-Path $lib ($script:RaSteamAppManifestRelPathFormat -f $script:RaAppId)
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        try {
            $root = ConvertFrom-RaVdf (Read-RaTextFile $acf).Text
            $dir = @(Find-RaVdfPath $root $script:RaAppManifestInstallDirPath)
            if ($dir.Count -ne 1) { continue }
            $game = Join-Path $lib (Join-Path $script:RaSteamCommonRelPath $dir[0].Value)
            if (Test-Path -LiteralPath (Join-Path $game $script:RaGameExeName) -PathType Leaf) { return $game }
        } catch { Write-Verbose "appmanifest unreadable in ${lib}: $($_.Exception.Message)" }
    }
    return $null
}

function Get-RaGameSha256 {
    <#
    .SYNOPSIS
        SHA-256 of hoi4.exe (read-only), or $null when the game folder is unknown.
    .DESCRIPTION
        Used by -CheckUpdate to report whether a release supports the installed game build.
    .PARAMETER GameDir
        Game folder from Get-RaGameDir.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaGameSha256 (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$GameDir)
    if (-not $GameDir) { return $null }
    $exe = Join-Path $GameDir $script:RaGameExeName
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return $null }
    return (Get-RaFileSha256 $exe)
}
