# ---------------------------------------------------------------------------
# Steam discovery (install folder, accounts, game folder, process) and, on request, closing
# and restarting Steam the official way (steam.exe -shutdown; never killed). ADR-0037.
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

function Test-RaSteamAnyProcess {
    <#
    .SYNOPSIS
        Tests whether steam.exe or one of its steamwebhelper processes still runs.
    .DESCRIPTION
        Steam writes localconfig.vdf when it exits; the edit waits until every Steam process
        is gone.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaSteamAnyProcess
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    return ((@(& $script:RaGetSteamProcess).Count + @(& $script:RaGetSteamHelperProcess).Count) -gt 0)
}

function Get-RaSteamExe {
    <#
    .SYNOPSIS
        Path of steam.exe from the registry, or $null.
    .DESCRIPTION
        HKCU\Software\Valve\Steam\SteamExe first, then SteamPath\steam.exe. Only an existing
        file is returned.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSteamExe
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $candidates = @()
    foreach ($name in $script:RaSteamExeRegistryValue, $script:RaSteamRegistryValue) {
        try {
            $value = [string](& $script:RaGetRegistryValue $script:RaSteamRegistryKey $name)
            if (-not $value) { continue }
            $value = $value -replace '/', '\'
            $candidates += if ($name -eq $script:RaSteamRegistryValue) { Join-Path $value $script:RaSteamExeName } else { $value }
        } catch { Write-Verbose "Registry value $name unavailable: $($_.Exception.Message)" }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    return $null
}

function Stop-RaSteam {
    <#
    .SYNOPSIS
        Asks Steam to exit the official way and waits until it is gone.
    .DESCRIPTION
        Runs "steam.exe -shutdown" (Steam's own clean exit) and polls every second, for at most
        60 s, until steam.exe and steamwebhelper are gone, logging progress every 5 s. Nothing is
        killed: when Steam stays, the caller falls back to asking the player. Dry-run mode only
        logs the plan and reports success so the rest of the plan can be shown.
    .OUTPUTS
        System.Boolean ($true when Steam is closed)
    .EXAMPLE
        if (Stop-RaSteam) { 'edit localconfig.vdf' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    $exe = Get-RaSteamExe
    if (-not $exe) {
        Write-RaLog (Get-RaText 'SteamExeMissing') -Level WARN
        return $false
    }
    if ($script:RaDry) {
        $plan = ("[dry-run] would run `"{0}`" {1}, wait up to {2} s and offer to start Steam again" -f
            $exe, $script:RaSteamShutdownArgument, $script:RaSteamShutdownTimeoutSeconds)
        Write-RaLog $plan -Level PLAN
        $script:RaSteamClosedByUs = $true
        $script:RaSteamRestartExe = $exe
        return $true
    }
    Write-RaLog (Get-RaText 'SteamClosing')
    & $script:RaStartProcess $exe @($script:RaSteamShutdownArgument)
    for ($waited = 0; $waited -lt $script:RaSteamShutdownTimeoutSeconds; $waited++) {
        if (-not (Test-RaSteamAnyProcess)) {
            $script:RaSteamClosedByUs = $true
            $script:RaSteamRestartExe = $exe
            Write-RaLog (Get-RaText 'SteamClosed')
            return $true
        }
        if ($waited -gt 0 -and $waited % $script:RaSteamProgressEverySeconds -eq 0) {
            Write-RaLog (Get-RaText 'SteamWaiting' @($waited, $script:RaSteamShutdownTimeoutSeconds))
        }
        & $script:RaSleep $script:RaSteamShutdownPollMilliseconds
    }
    if (-not (Test-RaSteamAnyProcess)) {
        $script:RaSteamClosedByUs = $true
        $script:RaSteamRestartExe = $exe
        Write-RaLog (Get-RaText 'SteamClosed')
        return $true
    }
    Write-RaLog (Get-RaText 'SteamCloseTimeout' @($script:RaSteamShutdownTimeoutSeconds)) -Level WARN
    return $false
}

function Start-RaSteamAgain {
    <#
    .SYNOPSIS
        Starts Steam again after this run closed it.
    .DESCRIPTION
        Same steam.exe, no arguments, no waiting. Does nothing when this run did not close
        Steam. Dry-run mode only logs the plan.
    .OUTPUTS
        None.
    .EXAMPLE
        Start-RaSteamAgain
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param()
    if (-not $script:RaSteamClosedByUs -or -not $script:RaSteamRestartExe) { return }
    $exe = $script:RaSteamRestartExe
    $script:RaSteamClosedByUs = $false
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would start `"$exe`"" -Level PLAN
        return
    }
    & $script:RaStartProcess $exe @()
    Write-RaLog (Get-RaText 'SteamStarted')
}

function Wait-RaSteamClosed {
    <#
    .SYNOPSIS
        Makes sure Steam is closed before localconfig.vdf is edited.
    .DESCRIPTION
        Returns $true when Steam is not running. Interactively it first offers to close Steam
        (steam.exe -shutdown, see Stop-RaSteam; default yes); when that is declined or Steam
        does not exit in time, it asks the player to close Steam until it is closed or the
        player types Q. With -Yes it closes Steam only when -CloseSteam was given, and otherwise
        returns $false at once (and logs why). Steam is never killed.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Wait-RaSteamClosed)) { return $false }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()
    if ($script:RaDry -and $script:RaSteamClosedByUs) { return $true }
    if (-not (Test-RaSteamRunning)) { return $true }
    if ($script:RaAssumeYes) {
        if ($script:RaCloseSteam) { return (Stop-RaSteam) }
        Write-RaLog (Get-RaText 'SteamRunningNonInteractive') -Level WARN
        return $false
    }
    if ((Confirm-Ra (Get-RaText 'AskCloseSteam') $true) -and (Stop-RaSteam)) { return $true }
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
