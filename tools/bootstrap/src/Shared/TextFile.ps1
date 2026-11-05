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
        Single place for file hashing (read-only, through the OpenRead adapter). Same format as
        Get-FileHash, but a failure keeps the .NET exception (an antivirus block stays
        recognizable by Test-RaAntivirusError instead of becoming Get-FileHash's "cannot be read").
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
    $stream = & $script:RaOpenRead $Path
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Test-RaAntivirusError {
    <#
    .SYNOPSIS
        Tests whether an error means that an antivirus blocked or removed the file.
    .DESCRIPTION
        Walks the exception chain (an ErrorRecord, PowerShell's MethodInvocationException and
        inner exceptions) for the Win32 errors ERROR_VIRUS_INFECTED (225) and ERROR_VIRUS_DELETED
        (226), as an HRESULT or a native error code. The check never depends on the (localized)
        message text.
    .PARAMETER ErrorObject
        ErrorRecord or Exception.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        try { Get-RaFileSha256 $p } catch { if (Test-RaAntivirusError $_) { 'blocked' } }
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Position = 0)][AllowNull()][object]$ErrorObject)
    $ex = if ($ErrorObject -is [System.Management.Automation.ErrorRecord]) { $ErrorObject.Exception } else { $ErrorObject }
    while ($ex -is [Exception]) {
        $codes = @($ex.HResult)
        if ($ex -is [ComponentModel.Win32Exception]) { $codes += $ex.NativeErrorCode }
        foreach ($code in $codes) {
            $low = $code -band $script:RaWin32HResultMask
            # A plain Win32 code, or an HRESULT of FACILITY_WIN32 (0x8007xxxx).
            $win32 = ($code -eq $low) -or (($code -band (-bnot $script:RaWin32HResultMask)) -eq $script:RaWin32HResultFacility)
            if ($win32 -and $script:RaAntivirusWin32Errors -contains $low) { return $true }
        }
        $ex = $ex.InnerException
    }
    return $false
}

function Get-RaAntivirusText {
    <#
    .SYNOPSIS
        The player-facing explanation of an antivirus block of one release file.
    .DESCRIPTION
        Names the file and its path, says that it is a false positive of a signed release file,
        gives its SHA-256 and the release page that lists it, and the way to restore or allow it in
        Windows Security > Protection history. It never suggests turning the antivirus off. For
        the proxy version.dll it adds that the default Launcher method does not need the file.
    .PARAMETER Path
        Blocked file.
    .PARAMETER Sha256
        SHA-256 the signed manifest gives for the file, or empty.
    .PARAMETER Version
        CAPIE version the file belongs to, or empty (then the latest release page is named).
    .OUTPUTS
        System.String
    .EXAMPLE
        throw (Get-RaAntivirusText -Path $dest -Sha256 $hash -Version '1.6.1')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path,
        [Parameter(Position = 1)][AllowEmptyString()][AllowNull()][string]$Sha256,
        [Parameter(Position = 2)][AllowEmptyString()][AllowNull()][string]$Version
    )
    $name = [IO.Path]::GetFileName($Path)
    $page = if ($Version) { $script:RaGitHubReleasePageFormat -f $script:RaGitHubRepo, $script:RaReleaseTagPrefix, $Version }
    else { $script:RaGitHubLatestPageFormat -f $script:RaGitHubRepo }
    $hash = if ($Sha256) { $Sha256.ToUpperInvariant() } else { '(see SHA256SUMS.txt)' }
    $release = if ($Version) { "CAPIE $Version" } else { 'CAPIE' }
    $text = Get-RaText 'AntivirusBlocked' @($name, $Path, $release, $hash, $page)
    if ($name -ieq $script:RaProxyFileName -or $Path -match '(?i)-proxy-') { $text += ' ' + (Get-RaText 'AntivirusProxyHint') }
    return $text
}

function Get-RaCheckedFileSha256 {
    <#
    .SYNOPSIS
        SHA-256 of a release file; an antivirus block becomes the player-facing explanation.
    .DESCRIPTION
        Like Get-RaFileSha256, but when the antivirus blocked the file (Test-RaAntivirusError), or
        removed a file this run has just written (-JustWritten: it is gone right after the write),
        it throws Get-RaAntivirusText instead of the raw .NET error. Every other error is rethrown
        unchanged.
    .PARAMETER Path
        File to hash.
    .PARAMETER Sha256
        Expected SHA-256 (for the message only), or empty.
    .PARAMETER Version
        CAPIE version of the file (for the message only), or empty.
    .PARAMETER JustWritten
        The file was written by this run a moment ago, so a missing file means it was removed.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaCheckedFileSha256 $dest -Sha256 $expected -Version $m.Version -JustWritten
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path,
        [AllowEmptyString()][AllowNull()][string]$Sha256,
        [AllowEmptyString()][AllowNull()][string]$Version,
        [switch]$JustWritten
    )
    try { return (Get-RaFileSha256 $Path) } catch {
        $removed = $JustWritten -and -not (Test-Path -LiteralPath $Path)
        if ((Test-RaAntivirusError $_) -or $removed) {
            Write-RaLog "Antivirus block of ${Path}: $($_.Exception.Message)" -Level WARN -NoConsole
            throw (Get-RaAntivirusText -Path $Path -Sha256 $Sha256 -Version $Version)
        }
        throw
    }
}
