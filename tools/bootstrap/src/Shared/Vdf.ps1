# ---------------------------------------------------------------------------
# Text VDF (Valve KeyValues) parser with source offsets. Offsets let the Steam
# slice replace exactly the bytes of one value and nothing else.
# ---------------------------------------------------------------------------

function ConvertFrom-RaVdfString {
    <#
    .SYNOPSIS
        Unescapes the body of a quoted VDF string.
    .DESCRIPTION
        Handles \\, \", \n and \t; any other backslash sequence is kept verbatim,
        as Steam does.
    .PARAMETER Raw
        String body without the surrounding quotes.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertFrom-RaVdfString 'C:\\Games\\x.exe'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Raw)
    $sb = New-Object System.Text.StringBuilder
    $i = 0
    while ($i -lt $Raw.Length) {
        $c = $Raw[$i]
        if ($c -eq '\' -and $i + 1 -lt $Raw.Length) {
            $n = $Raw[$i + 1]
            switch -CaseSensitive ($n) {
                '\' { [void]$sb.Append('\') }
                '"' { [void]$sb.Append('"') }
                'n' { [void]$sb.Append("`n") }
                't' { [void]$sb.Append("`t") }
                default { [void]$sb.Append('\').Append($n) }
            }
            $i += 2
        } else {
            [void]$sb.Append($c)
            $i++
        }
    }
    return $sb.ToString()
}

function ConvertTo-RaVdfString {
    <#
    .SYNOPSIS
        Escapes a value for a quoted VDF string.
    .DESCRIPTION
        Inverse of ConvertFrom-RaVdfString for \, ", newline and tab.
    .PARAMETER Value
        Plain value.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaVdfString '"C:\a b\x.exe" %command%'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`t", '\t')
}

function New-RaVdfNode {
    <#
    .SYNOPSIS
        Creates a VDF tree node.
    .DESCRIPTION
        Leaf nodes carry Value/ValueStart/ValueEnd; blocks carry OpenIndex/CloseIndex
        and Children. All indexes are character offsets in the parsed text (-1 = none).
    .PARAMETER Key
        Unescaped key.
    .PARAMETER KeyStart
        Offset of the key token.
    .PARAMETER IsBlock
        $true for a { } block, $false for a key/value pair.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        New-RaVdfNode -Key '<root>' -KeyStart -1 -IsBlock $true
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Key,
        [Parameter(Mandatory)][int]$KeyStart,
        [Parameter(Mandatory)][bool]$IsBlock
    )
    return [pscustomobject]@{
        Key = $Key; KeyStart = $KeyStart; IsBlock = $IsBlock
        Value = $null; ValueStart = -1; ValueEnd = -1
        OpenIndex = -1; CloseIndex = -1
        Children = (New-Object System.Collections.ArrayList)
    }
}

function ConvertFrom-RaVdf {
    <#
    .SYNOPSIS
        Parses text VDF into a node tree with source offsets.
    .DESCRIPTION
        Returns a synthetic root block. Comments (//) and [conditional] tokens are
        skipped. Throws on any structural problem: stray characters, a block without a
        key, a key without a value, unbalanced braces, an unterminated string.
    .PARAMETER Text
        Whole file text.
    .OUTPUTS
        System.Management.Automation.PSCustomObject
    .EXAMPLE
        $root = ConvertFrom-RaVdf (Read-RaTextFile $path).Text
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text)
    $root = New-RaVdfNode -Key '<root>' -KeyStart -1 -IsBlock $true
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    $pendingKey = $null
    $pendingKeyStart = -1
    $pos = 0
    foreach ($m in [regex]::Matches($Text, $script:RaVdfTokenPattern)) {
        $gap = $Text.Substring($pos, $m.Index - $pos)
        if ($gap -notmatch '^\s*$') { throw "VDF parse error near offset ${pos}: unexpected characters" }
        $pos = $m.Index + $m.Length
        $tok = $m.Value
        if ($tok.StartsWith('//')) { continue }
        if ($tok -eq '{') {
            if ($null -eq $pendingKey) { throw "VDF parse error at offset $($m.Index): block without key" }
            $node = New-RaVdfNode -Key $pendingKey -KeyStart $pendingKeyStart -IsBlock $true
            $node.OpenIndex = $m.Index
            [void]$stack.Peek().Children.Add($node)
            $stack.Push($node)
            $pendingKey = $null
            continue
        }
        if ($tok -eq '}') {
            if ($null -ne $pendingKey) { throw "VDF parse error at offset $($m.Index): key without value" }
            if ($stack.Count -le 1) { throw "VDF parse error at offset $($m.Index): unbalanced closing brace" }
            $node = $stack.Pop()
            $node.CloseIndex = $m.Index
            continue
        }
        $isQuoted = $tok.StartsWith('"')
        if (-not $isQuoted -and $null -eq $pendingKey -and $tok -match $script:RaVdfConditionalPattern) { continue }
        $value = if ($isQuoted) { ConvertFrom-RaVdfString $tok.Substring(1, $tok.Length - 2) } else { $tok }
        if ($null -eq $pendingKey) {
            $pendingKey = $value
            $pendingKeyStart = $m.Index
        } else {
            $node = New-RaVdfNode -Key $pendingKey -KeyStart $pendingKeyStart -IsBlock $false
            $node.Value = $value
            $node.ValueStart = $m.Index
            $node.ValueEnd = $m.Index + $m.Length
            [void]$stack.Peek().Children.Add($node)
            $pendingKey = $null
        }
    }
    $tail = $Text.Substring($pos)
    if ($tail -notmatch '^\s*$') { throw 'VDF parse error: unterminated string or garbage at end of file' }
    if ($null -ne $pendingKey) { throw 'VDF parse error: dangling key at end of file' }
    if ($stack.Count -ne 1) { throw 'VDF parse error: unclosed block at end of file' }
    return $root
}

function Find-RaVdfChild {
    <#
    .SYNOPSIS
        Returns the direct children of a node with the given key (case-insensitive).
    .DESCRIPTION
        Always returns an array (possibly empty) so callers can check .Count for
        missing or duplicate keys.
    .PARAMETER Node
        Parent node.
    .PARAMETER Key
        Key to match.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaVdfChild $app 'LaunchOptions'
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$Node,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$Key
    )
    return @($Node.Children | Where-Object { $_.Key -ieq $Key })
}

function Find-RaVdfPath {
    <#
    .SYNOPSIS
        Returns all nodes reached by a case-insensitive key path.
    .DESCRIPTION
        Follows every matching block at each level, so duplicates are visible to the caller.
    .PARAMETER Root
        Node to start from.
    .PARAMETER Path
        Keys to follow, outermost first.
    .OUTPUTS
        System.Object[]
    .EXAMPLE
        Find-RaVdfPath $root @('AppState', 'installdir')
    #>
    [CmdletBinding()]
    [OutputType([object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][psobject]$Root,
        [Parameter(Mandatory, Position = 1)][string[]]$Path
    )
    $current = @($Root)
    foreach ($k in $Path) {
        $next = @()
        foreach ($n in $current) { if ($n.IsBlock) { $next += Find-RaVdfChild $n $k } }
        $current = $next
    }
    return $current
}
