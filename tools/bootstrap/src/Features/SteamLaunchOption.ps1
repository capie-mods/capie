# ---------------------------------------------------------------------------
# Slice: Steam launch option.
#   1. compose/remove our wrapper in a LaunchOptions string (pure),
#   2. edit the LaunchOptions value inside localconfig.vdf text (pure),
#   3. update the file with backup, byte-exact verification and restore,
#   4. apply to the selected accounts and record what was written in state.json.
# ---------------------------------------------------------------------------

function Test-RaHasWrapper {
    <#
    .SYNOPSIS
        Tests whether launch options contain our launcher (quoted or unquoted).
    .DESCRIPTION
        Matches any folder, so an older install path is recognized too.
    .PARAMETER Value
        LaunchOptions value; $null and '' contain nothing.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaHasWrapper '"C:\RA\RenameAnythingLauncher.exe" %command%'
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    if ([string]::IsNullOrEmpty($Value)) { return $false }
    return [regex]::IsMatch($Value, $script:RaWrapperPattern, 'IgnoreCase')
}

function Remove-RaWrapperFragment {
    <#
    .SYNOPSIS
        Deletes every occurrence of our launcher fragment from launch options.
    .DESCRIPTION
        Pure string operation; whitespace right after the fragment is removed with it.
    .PARAMETER Value
        LaunchOptions value.
    .OUTPUTS
        System.String
    .EXAMPLE
        Remove-RaWrapperFragment '"C:\RA\RenameAnythingLauncher.exe" %command% -x'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Value)
    if ($null -eq $Value) { return $null }
    return [regex]::Replace($Value, $script:RaWrapperPattern, '', 'IgnoreCase')
}

function Add-RaWrapper {
    <#
    .SYNOPSIS
        Returns launch options with our wrapper directly before %command%.
    .DESCRIPTION
        Idempotent: an existing fragment (any path) is replaced. Options without
        %command% keep their meaning ("<ours> %command% <options>"). A second
        %command% is refused, as are quotes in the launcher path.
    .PARAMETER Current
        Existing LaunchOptions value or $null when the key is missing.
    .PARAMETER LauncherPath
        Absolute path of RenameAnythingLauncher.exe.
    .OUTPUTS
        System.String
    .EXAMPLE
        Add-RaWrapper '-debug_mode' 'C:\RA\current\RenameAnythingLauncher.exe'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$LauncherPath
    )
    if ($LauncherPath.Contains('"')) { throw 'Launcher path must not contain quotes' }
    if ([IO.Path]::GetFileName($LauncherPath) -ne $script:RaLauncherExeName) { throw 'Unexpected launcher file name' }
    $v = if ($null -eq $Current) { '' } else { Remove-RaWrapperFragment $Current }
    $fragment = '"' + $LauncherPath + '"'
    $commandMatches = [regex]::Matches($v, [regex]::Escape($script:RaSteamCommandToken), 'IgnoreCase')
    if ($commandMatches.Count -gt 1) { throw 'Existing launch options contain %command% more than once; refusing to guess. Edit them manually.' }
    if ($commandMatches.Count -eq 1) {
        $idx = $commandMatches[0].Index
        return $v.Substring(0, $idx) + $fragment + ' ' + $v.Substring($idx)
    }
    $rest = $v.Trim()
    if ($rest -eq '') { return "$fragment $($script:RaSteamCommandToken)" }
    # Without %command% Steam appends the options to the game command; keep that meaning.
    return "$fragment $($script:RaSteamCommandToken) $rest"
}

function Remove-RaWrapper {
    <#
    .SYNOPSIS
        Removes only our fragment from launch options.
    .DESCRIPTION
        A "%command%" left alone at the start is collapsed, which is equivalent in Steam
        to the options without it. Options without our fragment are returned unchanged.
    .PARAMETER Current
        LaunchOptions value.
    .OUTPUTS
        System.String
    .EXAMPLE
        Remove-RaWrapper '"C:\RA\RenameAnythingLauncher.exe" %command% -debug_mode'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current)
    if (-not (Test-RaHasWrapper $Current)) { return $Current }
    $t = (Remove-RaWrapperFragment $Current).Trim()
    if ($t -ieq $script:RaSteamCommandToken) { return '' }
    if ($t -match '^(?i)%command%\s+(.*)$') {
        $rest = $Matches[1]
        if ($rest -notmatch '(?i)%command%') { return $rest.Trim() }
    }
    return $t
}

function Get-RaAppLaunchInfo {
    <#
    .SYNOPSIS
        Locates the HOI4 app block and its LaunchOptions in localconfig.vdf text.
    .DESCRIPTION
        Only UserLocalConfigStore/Software/Valve/Steam/apps counts (WebStorage and depots
        are decoys). Throws when the file is corrupt or ambiguous: no or several apps
        blocks, several 394360 blocks, 394360 not a block, several LaunchOptions keys,
        or LaunchOptions being a block.
    .PARAMETER Text
        localconfig.vdf text.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Apps, App, Option, Value)
    .EXAMPLE
        (Get-RaAppLaunchInfo $text).Value
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text)
    $root = ConvertFrom-RaVdf $Text
    $apps = @(Find-RaVdfPath $root $script:RaAppsPath | Where-Object { $_.IsBlock })
    if ($apps.Count -eq 0) {
        throw 'No UserLocalConfigStore/Software/Valve/Steam/apps block in localconfig.vdf (start the game once from Steam, or set the launch option manually).'
    }
    if ($apps.Count -gt 1) { throw 'Multiple apps blocks in localconfig.vdf; refusing to edit.' }
    $app = @(Find-RaVdfChild $apps[0] $script:RaAppId)
    if ($app.Count -gt 1) { throw "Multiple $($script:RaAppId) blocks; refusing to edit." }
    if ($app.Count -eq 1 -and -not $app[0].IsBlock) { throw "$($script:RaAppId) is not a block; refusing to edit." }
    $lo = @()
    if ($app.Count -eq 1) { $lo = @(Find-RaVdfChild $app[0] $script:RaLaunchOptionsKey) }
    if ($lo.Count -gt 1) { throw 'Multiple LaunchOptions keys; refusing to edit.' }
    if ($lo.Count -eq 1 -and $lo[0].IsBlock) { throw 'LaunchOptions is a block; refusing to edit.' }
    return [pscustomobject]@{
        Apps   = $apps[0]
        App    = if ($app.Count -eq 1) { $app[0] } else { $null }
        Option = if ($lo.Count -eq 1) { $lo[0] } else { $null }
        Value  = if ($lo.Count -eq 1) { $lo[0].Value } else { $null }
    }
}

function Get-RaLineIndentBefore {
    <#
    .SYNOPSIS
        Returns the start and indentation of the line that holds a closing brace.
    .DESCRIPTION
        Used to insert a key or block with matching indentation. Refuses a brace that
        is not alone on its line.
    .PARAMETER Text
        Whole file text.
    .PARAMETER Index
        Offset of the closing brace.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (LineStart, Indent)
    .EXAMPLE
        Get-RaLineIndentBefore $text $info.App.CloseIndex
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][int]$Index
    )
    $nl = if ($Index -gt 0) { $Text.LastIndexOf("`n", $Index - 1) } else { -1 }
    $lineStart = $nl + 1
    $prefix = $Text.Substring($lineStart, $Index - $lineStart)
    if ($prefix -notmatch '^[\t ]*$') { throw 'Closing brace is not on its own line; refusing to edit.' }
    return [pscustomobject]@{ LineStart = $lineStart; Indent = $prefix }
}

function Set-RaLaunchOptionsInText {
    <#
    .SYNOPSIS
        Returns localconfig.vdf text with a new LaunchOptions value (minimal edit).
    .DESCRIPTION
        Replaces only the value token when the key exists (ReplacedValue), otherwise
        inserts the key before the app block's closing brace (InsertedKey), or a whole
        394360 block before the apps block's closing brace (InsertedBlock). Pure function.
    .PARAMETER Text
        Original text.
    .PARAMETER NewValue
        New LaunchOptions value (unescaped).
    .PARAMETER Newline
        Line ending used for inserted lines.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Text, Action, OldValue)
    .EXAMPLE
        Set-RaLaunchOptionsInText $file.Text $newValue $file.Newline
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$NewValue,
        [Parameter(Position = 2)][ValidateSet("`n", "`r`n")][string]$Newline = "`n"
    )
    $info = Get-RaAppLaunchInfo $Text
    $escaped = '"' + (ConvertTo-RaVdfString $NewValue) + '"'
    $keyToken = '"' + $script:RaLaunchOptionsKey + '"' + "`t`t"
    if ($null -ne $info.Option) {
        $o = $info.Option
        $new = $Text.Substring(0, $o.ValueStart) + $escaped + $Text.Substring($o.ValueEnd)
        return [pscustomobject]@{ Text = $new; Action = 'ReplacedValue'; OldValue = $info.Value }
    }
    if ($null -ne $info.App) {
        $pos = Get-RaLineIndentBefore $Text $info.App.CloseIndex
        $line = $pos.Indent + "`t" + $keyToken + $escaped + $Newline
        $new = $Text.Insert($pos.LineStart, $line)
        return [pscustomobject]@{ Text = $new; Action = 'InsertedKey'; OldValue = $null }
    }
    $pos = Get-RaLineIndentBefore $Text $info.Apps.CloseIndex
    $ind = $pos.Indent + "`t"
    $block = @(
        $ind, '"', $script:RaAppId, '"', $Newline,
        $ind, '{', $Newline,
        $ind, "`t", $keyToken, $escaped, $Newline,
        $ind, '}', $Newline
    ) -join ''
    $new = $Text.Insert($pos.LineStart, $block)
    return [pscustomobject]@{ Text = $new; Action = 'InsertedBlock'; OldValue = $null }
}

function New-RaBackupPath {
    <#
    .SYNOPSIS
        Returns a not-yet-existing backup path next to a config file.
    .DESCRIPTION
        <file>.renameanything-backup-<yyyyMMdd-HHmmss>[-n], time from the clock adapter.
    .PARAMETER ConfigPath
        File to back up.
    .OUTPUTS
        System.String
    .EXAMPLE
        New-RaBackupPath $configPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$ConfigPath)
    $stamp = (Get-RaNow).ToString($script:RaBackupTimestampFormat)
    $backup = "$ConfigPath$($script:RaBackupInfix)$stamp"
    $n = 1
    while (Test-Path -LiteralPath $backup) {
        $backup = "$ConfigPath$($script:RaBackupInfix)$stamp-$n"
        $n++
    }
    return $backup
}

function New-RaUpdateResult {
    <#
    .SYNOPSIS
        Builds the result object of Update-RaLocalConfig.
    .DESCRIPTION
        One shape for every outcome (unchanged, dry-run, written).
    .PARAMETER Changed
        Whether the value changes (or would change in dry-run mode).
    .PARAMETER OldValue
        Previous value or $null.
    .PARAMETER NewValue
        Resulting value.
    .PARAMETER Backup
        Backup path or $null.
    .PARAMETER Action
        Unchanged, ReplacedValue, InsertedKey or InsertedBlock.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        New-RaUpdateResult -Changed $false -OldValue 'x' -NewValue 'x' -Backup $null -Action 'Unchanged'
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][bool]$Changed,
        [AllowNull()][object]$OldValue,
        [AllowNull()][object]$NewValue,
        [AllowNull()][object]$Backup,
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Action
    )
    return [pscustomobject]@{ Changed = $Changed; OldValue = $OldValue; NewValue = $NewValue; Backup = $Backup; Action = $Action }
}

function Update-RaLocalConfig {
    <#
    .SYNOPSIS
        Changes LaunchOptions in one localconfig.vdf with backup, verification and restore.
    .DESCRIPTION
        Reads the file strictly, asks -Compute for the new value, edits only the value
        bytes, then (unless dry-run) re-checks that Steam is closed, writes a verified
        byte-exact backup, writes through a temporary file, compares the result byte for
        byte and parses it again. Any failure restores the backup.
    .PARAMETER ConfigPath
        localconfig.vdf of one account.
    .PARAMETER Compute
        Scriptblock(oldValueOrNull) returning the new value, or $null for "no change".
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Changed, OldValue, NewValue, Backup, Action)
    .EXAMPLE
        Update-RaLocalConfig -ConfigPath $cfg -Compute { param($old) Add-RaWrapper $old $launcher }
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$ConfigPath,
        [Parameter(Mandatory)][scriptblock]$Compute
    )
    $file = Read-RaTextFile $ConfigPath
    $info = Get-RaAppLaunchInfo $file.Text
    $newValue = & $Compute $info.Value
    if ($null -eq $newValue -or ($null -ne $info.Value -and $newValue -ceq $info.Value)) {
        return (New-RaUpdateResult -Changed $false -OldValue $info.Value -NewValue $info.Value -Backup $null -Action 'Unchanged')
    }
    $edit = Set-RaLaunchOptionsInText -Text $file.Text -NewValue $newValue -Newline $file.Newline
    $check = Get-RaAppLaunchInfo $edit.Text
    if ($check.Value -cne $newValue) { throw 'Internal check failed: edited text does not contain the expected value.' }
    $expected = ConvertTo-RaFileContent $edit.Text $file.HasBom
    if ($script:RaDry) {
        Write-RaLog ("[dry-run] {0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action) -Level PLAN
        return (New-RaUpdateResult -Changed $true -OldValue $info.Value -NewValue $newValue -Backup $null -Action $edit.Action)
    }
    if (Test-RaSteamRunning) { throw 'Steam started while editing; aborting before write.' }
    $backup = New-RaBackupPath $ConfigPath
    & $script:RaWriteAllBytes $backup $file.Bytes
    if (-not (Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $backup)) $file.Bytes)) { throw "Backup verification failed: $backup" }
    Write-RaLog "Backup: $backup (sha256 $(Get-RaSha256OfData $file.Bytes))"
    $tmp = "$ConfigPath$($script:RaWriteTempSuffix)"
    try {
        & $script:RaWriteAllBytes $tmp $expected
        & $script:RaCopyFile $tmp $ConfigPath
        Remove-Item -LiteralPath $tmp -Force
        if ($script:RaAfterWriteHook) { & $script:RaAfterWriteHook $ConfigPath }
        $after = [byte[]](& $script:RaReadAllBytes $ConfigPath)
        if (-not (Test-RaBytesEqual $after $expected)) { throw 'Post-write verification failed: file content differs from expected.' }
        $verify = Get-RaAppLaunchInfo (Read-RaTextFile $ConfigPath).Text
        if ($verify.Value -cne $newValue) { throw 'Post-write verification failed: LaunchOptions value differs.' }
    } catch {
        $err = $_.Exception.Message
        try {
            if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
            & $script:RaCopyFile $backup $ConfigPath
            $restored = Test-RaBytesEqual ([byte[]](& $script:RaReadAllBytes $ConfigPath)) $file.Bytes
        } catch { $restored = $false }
        if ($restored) { throw "$err Original file restored from backup." }
        throw "$err RESTORE FAILED - copy the backup back manually: $backup"
    }
    Write-RaLog ("{0}: LaunchOptions '{1}' -> '{2}' ({3})" -f $ConfigPath, $info.Value, $newValue, $edit.Action)
    return (New-RaUpdateResult -Changed $true -OldValue $info.Value -NewValue $newValue -Backup $backup -Action $edit.Action)
}

function Find-RaSteamEntry {
    <#
    .SYNOPSIS
        Returns the state.json records for one localconfig.vdf.
    .DESCRIPTION
        Always an array; normally zero or one element.
    .PARAMETER State
        State object.
    .PARAMETER ConfigPath
        localconfig.vdf path.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaSteamEntry $state $acc.ConfigPath
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][string]$ConfigPath
    )
    return @($State.SteamEntries | Where-Object { $_.ConfigPath -eq $ConfigPath })
}

function Remove-RaSteamEntry {
    <#
    .SYNOPSIS
        Drops the state.json record of one localconfig.vdf (in memory).
    .DESCRIPTION
        The caller saves the state.
    .PARAMETER State
        State object (modified).
    .PARAMETER ConfigPath
        localconfig.vdf path.
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaSteamEntry $state $acc.ConfigPath
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][string]$ConfigPath
    )
    $State.SteamEntries = @($State.SteamEntries | Where-Object { $_.ConfigPath -ne $ConfigPath })
}

function Set-RaSteamLaunchOption {
    <#
    .SYNOPSIS
        Adds our wrapper to the HOI4 launch options of the selected Steam accounts.
    .DESCRIPTION
        Requires Steam to be closed (asks, or fails with -Yes). Records per account the
        original value (the first one we ever saw) and what was written, so that undo can
        restore the exact original. Warns when another wrapper precedes ours.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder.
    .PARAMETER AccountSelector
        Value of -SteamAccount.
    .PARAMETER LauncherPath
        Stable launcher path (current junction).
    .OUTPUTS
        None.
    .EXAMPLE
        Set-RaSteamLaunchOption $state $steam 'all' (Get-RaLauncherPath)
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$SteamRootPath,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$AccountSelector,
        [Parameter(Mandatory, Position = 3)][ValidateNotNullOrEmpty()][string]$LauncherPath
    )
    $accounts = @(Select-RaSteamAccount @(Get-RaSteamAccount $SteamRootPath) $AccountSelector)
    if (-not (Wait-RaSteamClosed)) { throw 'Steam is running; Steam launch option was not changed.' }
    # Compute runs inside Update-RaLocalConfig and sees this scope dynamically; the name is unique on purpose.
    $raComputeLauncher = $LauncherPath
    $compute = { param($old) Add-RaWrapper $old $raComputeLauncher }
    foreach ($acc in $accounts) {
        $r = Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute $compute
        $existing = @(Find-RaSteamEntry $State $acc.ConfigPath)
        $original = if ($existing.Count -gt 0) { $existing[0].OriginalValue } else { $r.OldValue }
        if ($existing.Count -eq 0 -and (Test-RaHasWrapper $r.OldValue)) { $original = Remove-RaWrapper $r.OldValue }
        Remove-RaSteamEntry $State $acc.ConfigPath
        $record = [pscustomobject]@{
            AccountId     = $acc.Id
            ConfigPath    = $acc.ConfigPath
            OriginalValue = $original
            WrittenValue  = $r.NewValue
            LauncherPath  = $LauncherPath
        }
        $State.SteamEntries = @($State.SteamEntries) + @($record)
        $other = ((Remove-RaWrapperFragment $r.NewValue) -replace '(?i)\s*%command%.*$', '').Trim()
        if ($other) { Write-RaLog "Note: another wrapper precedes ours in account $($acc.Id): '$other'. Compatibility is unknown." -Level WARN }
    }
}

function Get-RaUndoValue {
    <#
    .SYNOPSIS
        Computes the launch options after removing our wrapper.
    .DESCRIPTION
        Restores the exact original only when the value is still what we wrote;
        otherwise (the user edited it since) removes just our fragment.
    .PARAMETER Current
        Current LaunchOptions value.
    .PARAMETER Entry
        state.json record for this file, or $null.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaUndoValue $old $entry
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Position = 0)][AllowNull()][AllowEmptyString()][string]$Current,
        [Parameter(Position = 1)][AllowNull()][psobject]$Entry
    )
    if ($null -ne $Entry -and $Current -ceq $Entry.WrittenValue) {
        if ($null -eq $Entry.OriginalValue) { return '' }
        return [string]$Entry.OriginalValue
    }
    return (Remove-RaWrapper $Current)
}

function Remove-RaSteamLaunchOption {
    <#
    .SYNOPSIS
        Removes our wrapper from every Steam account that has it.
    .DESCRIPTION
        Accounts without our fragment are not touched (no backup either). Unreadable
        accounts are logged and skipped. Returns $false when Steam is running and the
        user did not close it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER SteamRootPath
        Steam folder.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        if (-not (Remove-RaSteamLaunchOption $state $steam)) { 'Steam still running' }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$SteamRootPath
    )
    $accounts = @(Get-RaSteamAccount $SteamRootPath)
    $targets = @()
    foreach ($acc in $accounts) {
        try {
            $info = Get-RaAppLaunchInfo (Read-RaTextFile $acc.ConfigPath).Text
            if (Test-RaHasWrapper $info.Value) { $targets += $acc }
        } catch { Write-RaLog "Account $($acc.Id): cannot read localconfig.vdf ($($_.Exception.Message)); skipped." -Level WARN }
    }
    if ($targets.Count -eq 0) {
        Write-RaLog 'No Steam launch option with Rename Anything found.'
        $State.SteamEntries = @()
        return $true
    }
    if (-not (Wait-RaSteamClosed)) { return $false }
    foreach ($acc in $targets) {
        # Compute runs inside Update-RaLocalConfig and sees this scope dynamically; the name is unique on purpose.
        $raComputeEntry = @(Find-RaSteamEntry $State $acc.ConfigPath) | Select-Object -First 1
        $compute = { param($old) Get-RaUndoValue $old $raComputeEntry }
        [void](Update-RaLocalConfig -ConfigPath $acc.ConfigPath -Compute $compute)
        Remove-RaSteamEntry $State $acc.ConfigPath
    }
    return $true
}
