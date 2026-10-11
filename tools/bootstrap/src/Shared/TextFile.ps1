# ---------------------------------------------------------------------------
# Byte-exact text file I/O and hashing.
# ---------------------------------------------------------------------------

function Read-RaTextFile {
    <#
    .SYNOPSIS
        Reads a UTF-8 file strictly, keeping everything needed to write it back byte for byte.
    .DESCRIPTION
        Refuses invalid UTF-8 and files whose UTF-8 round trip differs. Returns the text,
        whether a BOM was present, the dominant newline and the original bytes.
    .PARAMETER Path
        File to read.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Text, HasBom, Newline, Bytes)
    .EXAMPLE
        $file = Read-RaTextFile $configPath
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    $bytes = [byte[]](& $script:RaReadAllBytes $Path)
    $bom = $script:RaUtf8Bom
    $hasBom = ($bytes.Length -ge $bom.Length -and $bytes[0] -eq $bom[0] -and $bytes[1] -eq $bom[1] -and $bytes[2] -eq $bom[2])
    $offset = if ($hasBom) { $bom.Length } else { 0 }
    $strict = New-Object System.Text.UTF8Encoding($false, $true)
    try { $text = $strict.GetString($bytes, $offset, $bytes.Length - $offset) }
    catch { throw "File is not valid UTF-8, refusing to edit: $Path" }
    $round = $strict.GetBytes($text)
    if ($round.Length -ne ($bytes.Length - $offset)) { throw "UTF-8 round trip mismatch, refusing to edit: $Path" }
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    return [pscustomobject]@{ Text = $text; HasBom = $hasBom; Newline = $newline; Bytes = $bytes }
}

function ConvertTo-RaFileContent {
    <#
    .SYNOPSIS
        Encodes text as UTF-8, optionally with a BOM.
    .DESCRIPTION
        Counterpart of Read-RaTextFile: produces exactly the bytes a file should have.
    .PARAMETER Text
        Text to encode.
    .PARAMETER HasBom
        Prepend the UTF-8 BOM.
    .OUTPUTS
        System.Byte[]
    .EXAMPLE
        ConvertTo-RaFileContent $edit.Text $file.HasBom
    #>
    [CmdletBinding()]
    [OutputType([byte[]], [object[]])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory, Position = 1)][bool]$HasBom
    )
    $body = (New-Object System.Text.UTF8Encoding($false)).GetBytes($Text)
    if (-not $HasBom) { return , $body }
    return , ([byte[]](@($script:RaUtf8Bom) + $body))
}

function Test-RaBytesEqual {
    <#
    .SYNOPSIS
        Compares two byte arrays.
    .DESCRIPTION
        Length first, then byte by byte.
    .PARAMETER A
        First array.
    .PARAMETER B
        Second array.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaBytesEqual $after $expected
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$A,
        [Parameter(Mandatory, Position = 1)][AllowEmptyCollection()][byte[]]$B
    )
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}

function Get-RaSha256OfData {
    <#
    .SYNOPSIS
        SHA-256 of a byte array as upper-case hex.
    .DESCRIPTION
        Same format as Get-FileHash, so values can be compared directly.
    .PARAMETER Bytes
        Data to hash.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSha256OfData $file.Bytes
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyCollection()][byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') } finally { $sha.Dispose() }
}

function Get-RaFileSha256 {
    <#
    .SYNOPSIS
        SHA-256 of a file as upper-case hex.
    .DESCRIPTION
        Single place for file hashing (Get-FileHash, read-only).
    .PARAMETER Path
        File to hash.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaFileSha256 $zipPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
