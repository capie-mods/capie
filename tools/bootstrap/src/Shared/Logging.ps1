# ---------------------------------------------------------------------------
# Logging and prompts (shared by every slice).
# ---------------------------------------------------------------------------

function Get-RaLogPath {
    <#
    .SYNOPSIS
        Path of bootstrap.log under the root folder.
    .DESCRIPTION
        The log is the only file kept by -Uninstall.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-RaLogPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $script:RaRoot $script:RaLogFileName)
}

function Write-RaLog {
    <#
    .SYNOPSIS
        Writes a message to the console (coloured by level) and appends it to bootstrap.log.
    .DESCRIPTION
        The console line is skipped with -NoConsole or when $script:RaQuiet is set.
        Nothing is written to disk in dry-run mode. A failing log write never fails the run.
    .PARAMETER Message
        Text to log.
    .PARAMETER Level
        INFO, WARN, ERROR or PLAN (dry-run plan lines).
    .PARAMETER NoConsole
        Log to the file only.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-RaLog 'Steam step declined.' -Level WARN
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$Message,
        [ValidateSet('INFO', 'WARN', 'ERROR', 'PLAN')][string]$Level = 'INFO',
        [switch]$NoConsole
    )
    $line = '{0} [{1}] {2}' -f (Get-RaNow).ToString($script:RaLogTimestampFormat), $Level, $Message
    if (-not $NoConsole -and -not $script:RaQuiet) { Write-RaHost $Message -Color $script:RaLogColors[$Level] }
    if ($script:RaDry) { return }
    try {
        Initialize-RaRoot
        $enc = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::AppendAllText((Get-RaLogPath), $line + [Environment]::NewLine, $enc)
    } catch { Write-Verbose "Log write failed: $($_.Exception.Message)" }
}

function Read-RaAnswer {
    <#
    .SYNOPSIS
        Asks the user a question and returns the trimmed answer.
    .DESCRIPTION
        Goes through the $script:RaReadHost adapter; $null becomes an empty string.
    .PARAMETER Prompt
        Question text (\uXXXX escapes are decoded).
    .OUTPUTS
        System.String
    .EXAMPLE
        $mode = Read-RaAnswer (Get-RaText 'AskMode')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][AllowEmptyString()][string]$Prompt)
    $answer = & $script:RaReadHost (ConvertFrom-RaEscapedText $Prompt)
    if ($null -eq $answer) { return '' }
    return ([string]$answer).Trim()
}

function Confirm-Ra {
    <#
    .SYNOPSIS
        Asks a yes/no question until the answer is understood.
    .DESCRIPTION
        Accepts English and Polish answers; an empty answer returns -Default.
        With -Yes ($script:RaAssumeYes) the answer is always yes and nothing is asked.
    .PARAMETER Prompt
        Question text.
    .PARAMETER Default
        Result for an empty answer.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Confirm-Ra (Get-RaText 'AskConfirmSteam') $false
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Prompt,
        [Parameter(Position = 1)][bool]$Default = $false
    )
    if ($script:RaAssumeYes) { return $true }
    while ($true) {
        $a = (Read-RaAnswer $Prompt).ToLowerInvariant()
        if ($a -eq '') { return $Default }
        if ($a -in $script:RaYesAnswers) { return $true }
        if ($a -in $script:RaNoAnswers) { return $false }
    }
}
