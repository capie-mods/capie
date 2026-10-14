# ---------------------------------------------------------------------------
# Terminal interface: capability detection and rendering (shared building blocks).
#
# Visual direction (docs/design/DESIGN-SYSTEM.md, "The Staff Annotation"): a recessed
# field-ink panel, a small staff map sheet with a grid, a flag planted in square C3, and
# the pencil writing the mod's name into the margin next to it. The only motion is the
# pencil writing, one character per frame; the resting frame is complete. Status lines use
# the CLI pattern (OK, ASK, SKIP, NOTE, FAIL) and read the same without colour.
#
# Everything here is pure (returns strings or line models) except the few Write/Read
# helpers at the end, which go through the adapters in Adapters.ps1.
# A line model is an array of segments; a segment is @{ S = '<token name>'; T = '<text>' }.
# ---------------------------------------------------------------------------

# Map sheet: grid letters, frame, six rows. The flag (#==) stands on the pole in C3.
$script:RaTuiMapHeader = '     A     B     C     D     E     F'
$script:RaTuiMapFrame = '  +-----+-----+-----+-----+-----+-----+'
$script:RaTuiMapRows = @(
    ' ~ ~ ~ ~ ~ ~ ~ ~ ______ ~ ~ ~ ~ ~ ~',
    ' ~ ~ ~ ~ ~ ~ ___/      \___ ~ ~ ~ ~',
    ' ~ ~ ~ ~ __/  |##==        \_ ~ ~ ~',
    ' ~ ~ ~ _/     |              \ ~ ~ ',
    ' ~ ~ ~ \__    |          ____/ ~ ~ ',
    ' ~ ~ ~ ~ ~\___|_________/ ~ ~ ~ ~ ~'
)
# Character classes of the map and their tokens.
$script:RaTuiMapStyles = [ordered]@{ '~' = 'ash'; '#' = 'header-amber'; '=' = 'header-amber'; '|' = 'steel-bevel' }
$script:RaTuiCoastStyle = 'khaki'
$script:RaTuiFrameStyle = 'steel-bevel'

function New-RaTuiSegment {
    <#
    .SYNOPSIS
        Creates one styled text segment of a line model.
    .DESCRIPTION
        S is a colour token name from $script:RaTuiPalette, T the text.
    .PARAMETER Style
        Token name.
    .PARAMETER Text
        Text.
    .OUTPUTS
        System.Collections.Hashtable
    .EXAMPLE
        New-RaTuiSegment 'khaki' 'Install'
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Style,
        [Parameter(Position = 1)][AllowEmptyString()][string]$Text = ''
    )
    return @{ S = $Style; T = $Text }
}

function Get-RaTuiMode {
    <#
    .SYNOPSIS
        Decides how the run talks to the user: terminal interface or plain prompts.
    .DESCRIPTION
        The interface opens only for an interactive console when no action switch, -Yes or
        -Plain was given. Colour needs a virtual-terminal host and is off with NO_COLOR or
        TERM=dumb. Animation also needs colour and is off with -NoAnimation,
        RA_NO_ANIMATION (any value except 0) or CI. Width is the console width minus one,
        clamped to 44..78.
    .PARAMETER Options
        Normalized option table.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Tui, Color, Vt, Animate, Width)
    .EXAMPLE
        $mode = Get-RaTuiMode $options
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Options)
    $terminal = & $script:RaGetTerminal
    $action = $false
    foreach ($name in $script:RaActionOptionNames) { if ($Options.ContainsKey($name) -and $Options[$name]) { $action = $true } }
    $noColor = [bool](& $script:RaGetEnv $script:RaTuiEnvNoColor) -or ((& $script:RaGetEnv $script:RaTuiEnvTerm) -eq $script:RaTuiDumbTerm)
    $noAnimationEnv = & $script:RaGetEnv $script:RaTuiEnvNoAnimation
    $noAnimation = ([bool]$noAnimationEnv -and $noAnimationEnv -ne '0') -or [bool](& $script:RaGetEnv $script:RaTuiEnvCi) -or [bool]$Options['NoAnimation']
    $tui = [bool]$terminal.Interactive -and -not $action -and -not $Options['Yes'] -and -not $Options['Plain']
    $color = $tui -and [bool]$terminal.Vt -and -not $noColor
    $width = [int]$terminal.Width - 1
    if ($width -lt 1) { $width = $script:RaTuiFallbackWidth }
    $width = [Math]::Max($script:RaTuiMinWidth, [Math]::Min($script:RaTuiMaxWidth, $width))
    return [pscustomobject]@{ Tui = $tui; Color = $color; Vt = ($tui -and [bool]$terminal.Vt); Animate = ($color -and -not $noAnimation); Width = $width }
}

function ConvertTo-RaTuiSgr {
    <#
    .SYNOPSIS
        ANSI 24-bit colour sequence for a palette token.
    .DESCRIPTION
        Foreground by default, background with -Background.
    .PARAMETER Style
        Token name from $script:RaTuiPalette.
    .PARAMETER Background
        Background instead of foreground.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaTuiSgr 'parchment'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][string]$Style, [switch]$Background)
    $hex = $script:RaTuiPalette[$Style]
    if (-not $hex) { throw "Unknown palette token: $Style" }
    $r = [Convert]::ToInt32($hex.Substring(1, 2), 16)
    $g = [Convert]::ToInt32($hex.Substring(3, 2), 16)
    $b = [Convert]::ToInt32($hex.Substring(5, 2), 16)
    $kind = if ($Background) { 48 } else { 38 }
    return ('{0}[{1};2;{2};{3};{4}m' -f [char]27, $kind, $r, $g, $b)
}

function Format-RaTuiLine {
    <#
    .SYNOPSIS
        Renders one line model as a string for the console.
    .DESCRIPTION
        With colour: panel background, indent, each segment in its colour, padded to the
        panel width, then a reset. Without colour: indent and plain text, trailing blanks
        removed. Text beyond the panel width is cut (callers wrap first).
    .PARAMETER Segments
        Line model.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .PARAMETER Background
        Panel token for this line (default: the panel background).
    .OUTPUTS
        System.String
    .EXAMPLE
        Format-RaTuiLine @((New-RaTuiSegment 'khaki' 'x')) $mode
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Segments,
        [Parameter(Mandatory, Position = 1)][psobject]$Mode,
        [string]$Background = $script:RaTuiBackground
    )
    $room = $Mode.Width - $script:RaTuiIndent
    $plain = ''
    $sb = New-Object System.Text.StringBuilder
    foreach ($segment in $Segments) {
        $text = [string]$segment.T
        if ($plain.Length + $text.Length -gt $room) { $text = $text.Substring(0, [Math]::Max(0, $room - $plain.Length)) }
        $plain += $text
        if ($Mode.Color) { [void]$sb.Append((ConvertTo-RaTuiSgr $segment.S)).Append($text) }
    }
    $indent = ' ' * $script:RaTuiIndent
    if (-not $Mode.Color) { return ($indent + $plain).TrimEnd() }
    $pad = ' ' * ($room - $plain.Length)
    return (ConvertTo-RaTuiSgr $Background -Background) + $indent + $sb.ToString() + $pad + ('{0}[0m' -f [char]27)
}

function Split-RaTuiText {
    <#
    .SYNOPSIS
        Word-wraps text to a width.
    .DESCRIPTION
        Breaks at spaces; a word longer than the width (a path, a hash) is cut into pieces.
        Explicit line breaks are kept. Always returns at least one (possibly empty) line.
    .PARAMETER Text
        Text.
    .PARAMETER Width
        Maximum characters per line (at least 1).
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Split-RaTuiText 'a long sentence' 6
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$Text,
        [Parameter(Mandatory, Position = 1)][ValidateRange(1, 1000)][int]$Width
    )
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($paragraph in ([string]$Text).Replace("`r`n", "`n").Split("`n")) {
        $current = ''
        foreach ($word in $paragraph.Split(' ')) {
            if ($word -eq '') { continue }
            while ($word.Length -gt $Width) {
                if ($current) { $lines.Add($current); $current = '' }
                $lines.Add($word.Substring(0, $Width))
                $word = $word.Substring($Width)
            }
            if (-not $current) { $current = $word }
            elseif ($current.Length + 1 + $word.Length -le $Width) { $current += ' ' + $word }
            else { $lines.Add($current); $current = $word }
        }
        $lines.Add($current)
    }
    return $lines.ToArray()
}

function Get-RaTuiStatusModel {
    <#
    .SYNOPSIS
        Line models of one status line in the CLI pattern ("OK    Package verified").
    .DESCRIPTION
        The status word is padded to a fixed column and coloured by $script:RaTuiStatusStyles;
        the text wraps under itself with a hanging indent.
    .PARAMETER Word
        OK, ASK, SKIP, NOTE or FAIL.
    .PARAMETER Text
        Message.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiStatusModel -Word 'OK' -Text 'CAPIE launcher installed' -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateSet('OK', 'ASK', 'SKIP', 'NOTE', 'FAIL')][string]$Word,
        [Parameter(Position = 1)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    $room = $Mode.Width - $script:RaTuiIndent - $script:RaTuiStatusWidth
    $textStyle = if ($Word -in @('SKIP', 'NOTE')) { 'khaki' } else { 'parchment' }
    $result = @()
    $first = $true
    foreach ($line in (Split-RaTuiText $Text $room)) {
        $head = if ($first) { $Word.PadRight($script:RaTuiStatusWidth) } else { ' ' * $script:RaTuiStatusWidth }
        $result += , @((New-RaTuiSegment $script:RaTuiStatusStyles[$Word] $head), (New-RaTuiSegment $textStyle $line))
        $first = $false
    }
    return , $result
}

function Get-RaTuiTextModel {
    <#
    .SYNOPSIS
        Line models of a wrapped paragraph in one colour.
    .DESCRIPTION
        Used for headings, hints and notes.
    .PARAMETER Text
        Text.
    .PARAMETER Style
        Token name.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiTextModel -Text 'What should this setup do?' -Style 'parchment' -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][string]$Style,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    $result = @()
    foreach ($line in (Split-RaTuiText $Text ($Mode.Width - $script:RaTuiIndent))) { $result += , @((New-RaTuiSegment $Style $line)) }
    return , $result
}

function Get-RaTuiMapModel {
    <#
    .SYNOPSIS
        Line models of the staff map sheet.
    .DESCRIPTION
        Grid letters and row numbers in ash, frame in steel, sea in ash, coast in khaki, the
        pole in steel and the flag in header amber (the one highlighted target of the view).
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiMapModel
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param()
    $result = @(, @((New-RaTuiSegment 'ash' $script:RaTuiMapHeader)))
    $result += , @((New-RaTuiSegment $script:RaTuiFrameStyle $script:RaTuiMapFrame))
    $row = 1
    foreach ($inner in $script:RaTuiMapRows) {
        $segments = @((New-RaTuiSegment 'ash' "$row "), (New-RaTuiSegment $script:RaTuiFrameStyle '|'))
        $runStyle = $null
        $run = ''
        foreach ($ch in $inner.ToCharArray()) {
            $key = [string]$ch
            $style = $script:RaTuiCoastStyle
            if ($ch -eq ' ') { $style = $runStyle } elseif ($script:RaTuiMapStyles.Contains($key)) { $style = $script:RaTuiMapStyles[$key] }
            if (-not $style) { $style = 'ash' }
            if ($style -ne $runStyle -and $run) { $segments += New-RaTuiSegment $runStyle $run; $run = '' }
            $runStyle = $style
            $run += $key
        }
        if ($run) { $segments += New-RaTuiSegment $runStyle $run }
        $segments += New-RaTuiSegment $script:RaTuiFrameStyle '|'
        $result += , $segments
        $row++
    }
    $result += , @((New-RaTuiSegment $script:RaTuiFrameStyle $script:RaTuiMapFrame))
    return , $result
}

function Get-RaTuiAnnotationLine {
    <#
    .SYNOPSIS
        Line model of the margin note: grid square, the name written so far, the pencil.
    .DESCRIPTION
        Frame k of the splash shows the first k characters; the last frame shows the full
        name with the pencil resting after it.
    .PARAMETER Count
        Characters written (clamped to the name length).
    .OUTPUTS
        System.Object[] (line model)
    .EXAMPLE
        Get-RaTuiAnnotationLine 7
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Mandatory, Position = 0)][int]$Count)
    $name = $script:RaTuiAnnotationName
    $written = $name.Substring(0, [Math]::Max(0, [Math]::Min($Count, $name.Length)))
    return , @(
        (New-RaTuiSegment 'ash' "$($script:RaTuiAnnotationRef)   "),
        (New-RaTuiSegment 'parchment' $written),
        (New-RaTuiSegment 'pencil-green-lit' $script:RaTuiPencil)
    )
}

function Get-RaTuiMenuModel {
    <#
    .SYNOPSIS
        Line models of the action menu.
    .DESCRIPTION
        One row per action: number, name, hint (a long hint wraps under itself). The selected
        row is marked with ">" (so it reads without colour) and drawn on the olive button
        background.
    .PARAMETER Selected
        Zero-based index of the selected action.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (pscustomobject rows: Segments, Background)
    .EXAMPLE
        Get-RaTuiMenuModel 0 $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Mandatory, Position = 0)][int]$Selected, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $result = @()
    $nameWidth = 0
    foreach ($action in $script:RaTuiMenuActions) { $nameWidth = [Math]::Max($nameWidth, (Get-RaText "Tui$action").Length) }
    for ($i = 0; $i -lt $script:RaTuiMenuActions.Count; $i++) {
        $action = $script:RaTuiMenuActions[$i]
        $on = ($i -eq $Selected)
        $marker = if ($on) { '> ' } else { '  ' }
        $head = '{0}{1}  {2}  ' -f $marker, ($i + 1), (Get-RaText "Tui$action").PadRight($nameWidth)
        $hintRoom = [Math]::Max(1, $Mode.Width - $script:RaTuiIndent - $head.Length)
        $nameStyle = if ($on) { 'parchment' } else { 'khaki' }
        $background = if ($on) { $script:RaTuiSelectedBackground } else { $script:RaTuiBackground }
        $first = $true
        foreach ($hint in (Split-RaTuiText (Get-RaText "Tui$($action)Hint") $hintRoom)) {
            $lead = if ($first) { $head } else { ' ' * $head.Length }
            $result += [pscustomobject]@{
                Segments   = @((New-RaTuiSegment $nameStyle $lead), (New-RaTuiSegment 'ash' $hint))
                Background = $background
            }
            $first = $false
        }
    }
    return , $result
}

function Get-RaTuiSummaryModel {
    <#
    .SYNOPSIS
        Line models of the summary: a key column and a value column under a hairline.
    .DESCRIPTION
        Keys in ash, values in parchment, values wrap under themselves.
    .PARAMETER Rows
        Array of two-element arrays (key, value).
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Object[] (line models)
    .EXAMPLE
        Get-RaTuiSummaryModel @(, @('Log', 'C:\x\bootstrap.log')) $mode
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param([Parameter(Mandatory, Position = 0)][object[]]$Rows, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $room = $Mode.Width - $script:RaTuiIndent - $script:RaTuiKeyWidth
    $result = @(, @((New-RaTuiSegment 'parchment' (Get-RaText 'TuiSummary'))))
    $result += , @((New-RaTuiSegment 'steel-bevel' ('-' * ($Mode.Width - $script:RaTuiIndent))))
    foreach ($row in $Rows) {
        $first = $true
        foreach ($line in (Split-RaTuiText ([string]$row[1]) $room)) {
            $key = if ($first) { ([string]$row[0]).PadRight($script:RaTuiKeyWidth) } else { ' ' * $script:RaTuiKeyWidth }
            $result += , @((New-RaTuiSegment 'ash' $key), (New-RaTuiSegment 'parchment' $line))
            $first = $false
        }
    }
    return , $result
}

function Write-RaTuiModel {
    <#
    .SYNOPSIS
        Writes line models to the console through the raw-output adapter.
    .DESCRIPTION
        Accepts plain line models or menu rows (objects with Segments and Background).
    .PARAMETER Lines
        Line models.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaTuiModel (Get-RaTuiMapModel) $mode
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][object[]]$Lines, [Parameter(Mandatory, Position = 1)][psobject]$Mode)
    $sb = New-Object System.Text.StringBuilder
    foreach ($line in $Lines) {
        if ($line -is [psobject] -and $line.PSObject.Properties['Segments']) {
            [void]$sb.Append((Format-RaTuiLine @($line.Segments) $Mode -Background $line.Background)).Append("`n")
        } else {
            [void]$sb.Append((Format-RaTuiLine @($line) $Mode)).Append("`n")
        }
    }
    & $script:RaWriteRaw $sb.ToString()
}

function Write-RaTuiBlank {
    <#
    .SYNOPSIS
        Writes one empty panel line.
    .DESCRIPTION
        Keeps the panel background continuous between groups.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaTuiBlank $mode
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    Write-RaTuiModel @(, @()) $Mode
}

function Show-RaTuiSplash {
    <#
    .SYNOPSIS
        Draws the map sheet and lets the pencil write the name into the margin.
    .DESCRIPTION
        With animation the margin line is redrawn in place (carriage return) one character
        per frame, then held still. Without animation only the final frame is drawn. The
        endorsement line follows in both cases.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Int32 (number of frames drawn for the margin line)
    .EXAMPLE
        [void](Show-RaTuiSplash $mode)
    #>
    [CmdletBinding()]
    [OutputType([int])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    Write-RaTuiBlank $Mode
    Write-RaTuiModel (Get-RaTuiMapModel) $Mode
    Write-RaTuiBlank $Mode
    $frames = 0
    $total = $script:RaTuiAnnotationName.Length
    if ($Mode.Animate) {
        for ($k = 0; $k -lt $total; $k++) {
            & $script:RaWriteRaw ("`r" + (Format-RaTuiLine (Get-RaTuiAnnotationLine $k) $Mode))
            & $script:RaSleep $script:RaTuiFrameDelayMs
            $frames++
        }
        & $script:RaWriteRaw ("`r" + (Format-RaTuiLine (Get-RaTuiAnnotationLine $total) $Mode) + "`n")
        $frames++
        & $script:RaSleep $script:RaTuiHoldDelayMs
    } else {
        Write-RaTuiModel @(, (Get-RaTuiAnnotationLine $total)) $Mode
        $frames++
    }
    $tag = Get-RaText 'TuiTagline'
    $at = $tag.IndexOf('CAPIE')
    $refPad = ' ' * ($script:RaTuiAnnotationRef.Length + 3)
    Write-RaTuiModel @(, @(
            (New-RaTuiSegment 'ash' ($refPad + $tag.Substring(0, $at))),
            (New-RaTuiSegment 'brass' 'CAPIE'),
            (New-RaTuiSegment 'ash' $tag.Substring($at + 5))
        )) $Mode
    Write-RaTuiBlank $Mode
    return $frames
}

function Read-RaTuiKey {
    <#
    .SYNOPSIS
        Reads one key and names it.
    .DESCRIPTION
        Returns Up, Down, Enter, Escape, or the typed character in lower case.
    .OUTPUTS
        System.String
    .EXAMPLE
        $key = Read-RaTuiKey
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $info = & $script:RaReadKey
    switch ([string]$info.Key) {
        'UpArrow' { return 'Up' }
        'DownArrow' { return 'Down' }
        'Enter' { return 'Enter' }
        'Escape' { return 'Escape' }
    }
    return ([string]$info.KeyChar).ToLowerInvariant()
}

function Select-RaTuiAction {
    <#
    .SYNOPSIS
        Shows the menu and returns the chosen action.
    .DESCRIPTION
        Up/Down move the selection, digits pick directly, Enter confirms, Esc or Q quits.
        On a virtual-terminal host the menu is redrawn in place (cursor up); otherwise the
        menu is printed again below, and a digit confirms at once.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.String (Install, Update, Disable, Uninstall or Quit)
    .EXAMPLE
        $action = Select-RaTuiAction $mode
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][psobject]$Mode)
    $selected = 0
    $count = $script:RaTuiMenuActions.Count
    $model = Get-RaTuiMenuModel $selected $Mode
    Write-RaTuiModel $model $Mode
    while ($true) {
        $key = Read-RaTuiKey
        $before = $selected
        if ($key -eq 'Up') { $selected = ($selected + $count - 1) % $count }
        elseif ($key -eq 'Down') { $selected = ($selected + 1) % $count }
        elseif ($key -eq 'Enter') { return $script:RaTuiMenuActions[$selected] }
        elseif ($key -eq 'Escape' -or $key -eq $script:RaQuitAnswer) { return 'Quit' }
        elseif ($key -match '^[1-9]$' -and [int]$key -le $count) {
            $selected = [int]$key - 1
            if (-not $Mode.Vt) { return $script:RaTuiMenuActions[$selected] }
        }
        if ($selected -ne $before) {
            if ($Mode.Vt) { & $script:RaWriteRaw ('{0}[{1}A' -f [char]27, @($model).Count) } else { Write-RaTuiBlank $Mode }
            $model = Get-RaTuiMenuModel $selected $Mode
            Write-RaTuiModel $model $Mode
        }
    }
}

function Confirm-RaTui {
    <#
    .SYNOPSIS
        Asks a yes/no question as an ASK line and reads one key.
    .DESCRIPTION
        Y/T answer yes, N answers no, Enter takes the default, Esc answers no. The answer is
        echoed after the question so the transcript stays readable.
    .PARAMETER Prompt
        Question (ends with the default in brackets).
    .PARAMETER Default
        Answer for Enter.
    .PARAMETER Mode
        Result of Get-RaTuiMode.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Confirm-RaTui -Prompt (Get-RaText 'TuiAskSteam') -Default $true -Mode $mode
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Prompt,
        [Parameter(Mandatory, Position = 1)][bool]$Default,
        [Parameter(Mandatory, Position = 2)][psobject]$Mode
    )
    Write-RaTuiModel (Get-RaTuiStatusModel -Word 'ASK' -Text $Prompt -Mode $Mode) $Mode
    while ($true) {
        $key = Read-RaTuiKey
        $answer = $null
        if ($key -eq 'Enter') { $answer = $Default }
        elseif ($key -eq 'Escape') { $answer = $false }
        elseif ($key -in @('y', 't')) { $answer = $true }
        elseif ($key -eq 'n') { $answer = $false }
        if ($null -ne $answer) {
            $echo = if ($answer) { Get-RaText 'TuiYes' } else { Get-RaText 'TuiNo' }
            Write-RaTuiModel @(, @((New-RaTuiSegment 'ash' ((' ' * $script:RaTuiStatusWidth) + $echo)))) $Mode
            return [bool]$answer
        }
    }
}
