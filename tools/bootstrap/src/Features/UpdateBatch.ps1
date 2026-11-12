# ---------------------------------------------------------------------------
# Slice: click-to-update (ADR-0038). Every install, update, repair and rollback keeps a copy of
# this installer in <root>\setup\install.ps1 and writes <root>\Update CAPIE.bat, which runs that
# copy with -Update when the player double-clicks it. Nothing runs on its own: the batch file
# makes the same signed-manifest and SHA-256 checked -Update a player can type, one click away.
# ---------------------------------------------------------------------------

function Get-RaUpdateBatchPath {
    <#
    .SYNOPSIS
        Path of the click-to-update batch file in the root folder.
    .DESCRIPTION
        <root>\Update CAPIE.bat. Removed with everything else by -Uninstall.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaUpdateBatchPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $script:RaRoot $script:RaUpdateBatchName)
}

function Get-RaSetupCopyPath {
    <#
    .SYNOPSIS
        Path of the local installer copy the batch file runs.
    .DESCRIPTION
        <root>\setup\install.ps1. It lives outside the version folders: those hold exactly the
        files of their signed manifest, and a rollback to an older version must keep the copy.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaSetupCopyPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path (Join-Path $script:RaRoot $script:RaSetupCopyDirName) $script:RaSetupCopyFileName)
}

function Get-RaSelfScriptText {
    <#
    .SYNOPSIS
        Full text of the installer that is running now, or $null.
    .DESCRIPTION
        Walks from this function's syntax tree up to the script it was defined in, so it works
        the same for -File, for irm | iex and for & ([scriptblock]::Create(...)). Only a
        generated install.ps1 qualifies: pure ASCII, it parses, and it carries the generated-file
        marker and the entry point. Anything else returns $null (the copy is then left as it is).
    .OUTPUTS
        System.String
    .EXAMPLE
        $text = Get-RaSelfScriptText
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $ast = ${function:Get-RaSelfScriptText}.Ast
    while ($ast.Parent) { $ast = $ast.Parent }
    $text = $ast.Extent.Text
    if (-not $text -or -not $text.Contains($script:RaGeneratedScriptMarker) -or -not $text.Contains('function Invoke-RaMain')) { return $null }
    foreach ($ch in $text.ToCharArray()) { if ([int]$ch -gt 127) { return $null } }
    return $text
}

function ConvertTo-RaBatchValue {
    <#
    .SYNOPSIS
        Escapes a value for a  set "NAME=value"  line of a batch file.
    .DESCRIPTION
        Inside the quotes cmd.exe takes & | < > ^ literally; only % is special in a batch file
        (doubled here). A double quote cannot occur in a Windows path and is refused, as are line
        breaks.
    .PARAMETER Value
        Text to embed.
    .OUTPUTS
        System.String
    .EXAMPLE
        ConvertTo-RaBatchValue 'C:\Users\A %B\x'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Value)
    if ($Value.IndexOfAny([char[]]@('"', "`r", "`n")) -ge 0) { throw "Value cannot be written into a batch file: $Value" }
    return $Value.Replace('%', '%%')
}

function New-RaUpdateBatchText {
    <#
    .SYNOPSIS
        Text of Update CAPIE.bat (ASCII, CRLF).
    .DESCRIPTION
        The batch file names the installer copy and its SHA-256 by absolute path (it does not
        depend on its own folder or the working folder) and passes them to Windows PowerShell
        5.1 through environment variables, so no path is ever parsed as code. PowerShell reads
        the copy's bytes once, compares their SHA-256 with the recorded one and runs exactly
        those bytes with -Update (no window of time between check and use). A missing or changed
        copy runs nothing and names the download address of the installer. The window waits
        for a key at the end.
    .PARAMETER ScriptPath
        Absolute path of the installer copy.
    .PARAMETER Sha256
        Its SHA-256 (hex).
    .OUTPUTS
        System.String
    .EXAMPLE
        New-RaUpdateBatchText (Get-RaSetupCopyPath) $hash
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$ScriptPath,
        [Parameter(Mandatory, Position = 1)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$Sha256
    )
    $url = $script:RaGitHubLatestInstallerFormat -f $script:RaGitHubRepo
    $missing = $script:RaBatchCopyMissingExit
    $refuse = "Nothing was run. Download the installer again: ' + `$env:CAPIE_SETUP_URL); exit $missing }"
    # One line, no double quote and no percent sign: it sits inside "..." on the cmd.exe line.
    $command = @(
        "`$ErrorActionPreference = 'Stop'",
        '$p = $env:CAPIE_SETUP_SCRIPT',
        "if (-not (Test-Path -LiteralPath `$p -PathType Leaf)) { Write-Host ('The local CAPIE installer ' + `$p + ' is missing. $refuse",
        '$b = [IO.File]::ReadAllBytes($p)',
        "`$h = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash(`$b)).Replace('-', '')",
        "if (`$h -ne `$env:CAPIE_SETUP_SHA256) { Write-Host ('The local CAPIE installer ' + `$p + ' was changed or is damaged. $refuse",
        '& ([scriptblock]::Create([Text.Encoding]::ASCII.GetString($b))) -Update'
    ) -join '; '
    $lines = @(
        '@echo off',
        'rem Update CAPIE: written by the CAPIE installer; every install, update and rollback rewrites it.',
        'rem Double-click it to update CAPIE: it runs the local installer copy named below with -Update',
        'rem (newest signed release, every file checked against its SHA-256). It runs only when started.',
        'setlocal DisableDelayedExpansion',
        ('set "CAPIE_SETUP_SCRIPT=' + (ConvertTo-RaBatchValue $ScriptPath) + '"'),
        ('set "CAPIE_SETUP_SHA256=' + $Sha256.ToUpperInvariant() + '"'),
        ('set "CAPIE_SETUP_URL=' + (ConvertTo-RaBatchValue $url) + '"'),
        ('"' + $script:RaBatchPowerShell + '" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "' + $command + '"'),
        'set "CAPIE_EXIT=%ERRORLEVEL%"',
        'echo.',
        'pause',
        'exit /b %CAPIE_EXIT%'
    )
    return (($lines -join "`r`n") + "`r`n")
}

function Write-RaFileReplacing {
    <#
    .SYNOPSIS
        Writes bytes to a file through a temporary file next to it.
    .DESCRIPTION
        A reader sees the old or the new content, never a half-written file. Creates the folder.
    .PARAMETER Path
        Target file.
    .PARAMETER Bytes
        Content.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaFileReplacing $path $bytes
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$Path,
        [Parameter(Mandatory, Position = 1)][AllowEmptyCollection()][byte[]]$Bytes
    )
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $tmp = $Path + $script:RaWriteTempSuffix
    [IO.File]::WriteAllBytes($tmp, $Bytes)
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($tmp, $Path, [NullString]::Value) } else { [IO.File]::Move($tmp, $Path) }
}

function Install-RaUpdateBatch {
    <#
    .SYNOPSIS
        Refreshes the installer copy and Update CAPIE.bat after an install, update or rollback.
    .DESCRIPTION
        Writes the running installer to <root>\setup\install.ps1 (when it is a generated
        install.ps1, see Get-RaSelfScriptText) and then the batch file with that copy's SHA-256.
        When the running script cannot be copied, an existing copy and batch file stay as they are.
        Never fails the run: a problem is logged as a warning. No-op without an installed version.
    .PARAMETER State
        State object (read only).
    .OUTPUTS
        System.Boolean ($true when the batch file is in place and matches the copy)
    .EXAMPLE
        [void](Install-RaUpdateBatch $state)
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory, Position = 0)][psobject]$State)
    if (-not $State.InstalledVersion) { return $false }
    $copy = Get-RaSetupCopyPath
    $batch = Get-RaUpdateBatchPath
    if ($script:RaDry) {
        Write-RaLog "[dry-run] would write $copy and $batch" -Level PLAN
        return $true
    }
    try {
        $text = Get-RaSelfScriptText
        if (-not $text) {
            Write-RaLog "This run is not a generated install.ps1; $copy and $batch were left as they are." -Level WARN -NoConsole
            return $false
        }
        $bytes = [Text.Encoding]::ASCII.GetBytes($text)
        $existing = if (Test-Path -LiteralPath $copy -PathType Leaf) { [IO.File]::ReadAllBytes($copy) } else { $null }
        if ($null -eq $existing -or -not (Test-RaBytesEqual $existing $bytes)) { Write-RaFileReplacing $copy $bytes }
        $hash = Get-RaSha256OfData $bytes
        $batchBytes = [Text.Encoding]::ASCII.GetBytes((New-RaUpdateBatchText $copy $hash))
        Write-RaFileReplacing $batch $batchBytes
        Write-RaLog (Get-RaText 'UpdateBatchReady' @($batch))
        return $true
    } catch {
        Write-RaLog "Update CAPIE.bat not written: $($_.Exception.Message)" -Level WARN
        return $false
    }
}
