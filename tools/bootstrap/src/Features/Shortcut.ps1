# ---------------------------------------------------------------------------
# Slice: game shortcut (.lnk) in a folder the user chooses.
# ---------------------------------------------------------------------------

function Get-RaShortcutInfo {
    <#
    .SYNOPSIS
        Reads a .lnk file through the shortcut adapter.
    .DESCRIPTION
        Returns target, arguments, description and icon location.
    .PARAMETER Path
        Shortcut file.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (TargetPath, Arguments, Description, IconLocation)
    .EXAMPLE
        (Get-RaShortcutInfo $lnk).TargetPath
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    return (& $script:RaReadShortcut $Path)
}

function Test-RaOwnShortcut {
    <#
    .SYNOPSIS
        Tests whether a shortcut was created by us.
    .DESCRIPTION
        True only for an existing file whose target is RenameAnythingLauncher.exe inside
        the root folder. Unreadable files are not ours.
    .PARAMETER Path
        Shortcut file.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaOwnShortcut $lnk
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try { $t = (Get-RaShortcutInfo $Path).TargetPath } catch { return $false }
    if (-not $t) { return $false }
    $rootFull = [IO.Path]::GetFullPath($script:RaRoot).TrimEnd('\') + '\'
    return ([IO.Path]::GetFileName($t) -ieq $script:RaLauncherExeName -and
        [IO.Path]::GetFullPath($t).StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase))
}

function Show-RaFolderDialog {
    <#
    .SYNOPSIS
        Shows the Windows folder picker and returns the chosen folder or $null.
    .DESCRIPTION
        Default folder-picker adapter. WinForms dialogs need an STA thread; under an MTA
        host (PowerShell 7 default) the dialog runs in a temporary STA runspace.
    .PARAMETER Default
        Folder selected initially.
    .OUTPUTS
        System.String
    .EXAMPLE
        Show-RaFolderDialog ([Environment]::GetFolderPath('Desktop'))
    #>
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseCompatibleTypes', '',
        Justification = 'System.Windows.Forms is loaded by Add-Type right before use; the profiles list only default assemblies.')]
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][string]$Default)
    $code = {
        param($start, $title)
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = [System.Windows.Forms.FolderBrowserDialog]::new()
        $dlg.Description = $title
        $dlg.SelectedPath = $start
        $dlg.ShowNewFolderButton = $true
        $owner = [System.Windows.Forms.Form]::new()
        $owner.TopMost = $true
        try {
            if ($dlg.ShowDialog($owner) -eq 'OK') { return $dlg.SelectedPath }
            return $null
        } finally {
            $owner.Dispose()
            $dlg.Dispose()
        }
    }
    $title = $script:RaFolderPickerTitle
    if ([Threading.Thread]::CurrentThread.ApartmentState -eq 'STA') { return (& $code $Default $title) }
    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = 'STA'
    $rs.Open()
    try {
        $ps = [powershell]::Create()
        $ps.Runspace = $rs
        [void]$ps.AddScript($code.ToString()).AddArgument($Default).AddArgument($title)
        $r = @($ps.Invoke())
        if ($r.Count -gt 0) { return $r[-1] }
        return $null
    } finally { $rs.Dispose() }
}

function Select-RaFolder {
    <#
    .SYNOPSIS
        Lets the user pick a folder through the folder-picker adapter.
    .DESCRIPTION
        $script:RaFolderPicker = $null means the real dialog (Show-RaFolderDialog).
    .PARAMETER Default
        Folder selected initially.
    .OUTPUTS
        System.String
    .EXAMPLE
        Select-RaFolder $desktop
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][string]$Default)
    if ($script:RaFolderPicker) { return (& $script:RaFolderPicker $Default) }
    return (Show-RaFolderDialog $Default)
}

function Resolve-RaShortcutFolder {
    <#
    .SYNOPSIS
        Decides where the shortcut goes.
    .DESCRIPTION
        -ShortcutPath wins; with -Yes the Desktop is used; otherwise the user chooses
        Desktop (1 or Enter), the folder dialog (2) or types an absolute path.
    .PARAMETER Requested
        -ShortcutPath value or empty.
    .OUTPUTS
        System.String
    .EXAMPLE
        Resolve-RaShortcutFolder $ShortcutPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Requested)
    $desktop = & $script:RaDesktopPath
    if ($Requested) { return $Requested }
    if ($script:RaAssumeYes) { return $desktop }
    while ($true) {
        $a = Read-RaAnswer (Get-RaText 'AskShortcutFolder' @($desktop))
        if ($a -eq '' -or $a -eq $script:RaShortcutFolderDesktopChoice) { return $desktop }
        if ($a -eq $script:RaShortcutFolderPickerChoice) {
            $f = Select-RaFolder $desktop
            if ($f) { return $f }
            continue
        }
        $a = $a.Trim('"')
        if ([IO.Path]::IsPathRooted($a)) { return $a }
    }
}

function New-RaShortcut {
    <#
    .SYNOPSIS
        Creates the "Hearts of Iron IV + Rename Anything" shortcut.
    .DESCRIPTION
        Target: the stable launcher path; argument: the game's Paradox Launcher
        (dowser.exe, what Steam's %command% runs); icon from hoi4.exe (read-only);
        description: the multiplayer warning. Never overwrites a file it did not create.
        The path is recorded in state.json so uninstall can remove it.
    .PARAMETER State
        State object (modified; the caller saves it).
    .PARAMETER Folder
        Existing folder for the shortcut.
    .PARAMETER LauncherPath
        Stable launcher path (current junction).
    .PARAMETER GameDir
        Game folder from Get-RaGameDir.
    .OUTPUTS
        System.String (the shortcut path)
    .EXAMPLE
        New-RaShortcut $state $folder (Get-RaLauncherPath) (Get-RaGameDir $steam)
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$State,
        [Parameter(Mandatory, Position = 1)][ValidateNotNullOrEmpty()][string]$Folder,
        [Parameter(Mandatory, Position = 2)][ValidateNotNullOrEmpty()][string]$LauncherPath,
        [Parameter(Position = 3)][AllowEmptyString()][AllowNull()][string]$GameDir
    )
    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) { throw "Shortcut folder does not exist: $Folder" }
    $lnk = Join-Path ((Resolve-Path -LiteralPath $Folder).ProviderPath) $script:RaShortcutFileName
    if ((Test-Path -LiteralPath $lnk) -and -not (Test-RaOwnShortcut $lnk)) { throw "A different file already exists and was left untouched: $lnk" }
    if (-not $GameDir) { throw 'HOI4 installation not found through Steam libraries; cannot build the shortcut.' }
    $dowser = Join-Path $GameDir $script:RaParadoxLauncherExeName
    if (-not (Test-Path -LiteralPath $dowser -PathType Leaf)) { throw "Paradox Launcher not found: $dowser" }
    $icon = Join-Path $GameDir $script:RaGameExeName
    $iconLocation = if (Test-Path -LiteralPath $icon) { $icon + $script:RaIconIndexSuffix } else { $LauncherPath + $script:RaIconIndexSuffix }
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would create $lnk -> `"$LauncherPath`" `"$dowser`"" -Level PLAN
        return $lnk
    }
    $spec = [pscustomobject]@{
        TargetPath       = $LauncherPath
        Arguments        = '"' + $dowser + '"'
        WorkingDirectory = Split-Path -Parent $LauncherPath
        IconLocation     = $iconLocation
        Description      = Get-RaText 'ShortcutComment'
    }
    & $script:RaWriteShortcut $lnk $spec
    $State.Shortcuts = @(@($State.Shortcuts | Where-Object { $_ -ne $lnk }) + $lnk)
    Write-RaLog "Shortcut created: $lnk"
    return $lnk
}

function Remove-RaOwnShortcut {
    <#
    .SYNOPSIS
        Deletes our shortcuts (recorded ones and the Desktop default).
    .DESCRIPTION
        A shortcut that no longer points to our launcher is left untouched and logged.
        Clears the recorded list.
    .PARAMETER State
        State object (modified; the caller saves it).
    .OUTPUTS
        None.
    .EXAMPLE
        Remove-RaOwnShortcut $state
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    $candidates = @($State.Shortcuts) + @(Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName)
    foreach ($p in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $p)) { continue }
        if (Test-RaOwnShortcut $p) {
            if ($script:RaDry) {
                Write-RaLog "[dry-run] would delete shortcut $p" -Level PLAN
                continue
            }
            Remove-Item -LiteralPath $p -Force
            Write-RaLog "Shortcut deleted: $p"
        } else {
            Write-RaLog "Shortcut no longer points to our launcher, left untouched: $p" -Level WARN
        }
    }
    $State.Shortcuts = @()
}
