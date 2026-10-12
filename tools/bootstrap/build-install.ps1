<#
.SYNOPSIS
    Builds the single-file tools/bootstrap/install.ps1 from the fragments in tools/bootstrap/src.

.DESCRIPTION
    install.ps1 must stay ONE pure-ASCII file (no BOM) because users run it with
    "irm <url> | iex". The source is kept in small vertical slices under src/:

        src/Header.ps1                       help + param block of the distributed script
        src/Shared/*.ps1                     configuration, constants, adapters, messages,
                                             logging, VDF, byte-exact files, state,
                                             manifest/hash/signature, Steam discovery,
                                             version store
        src/Features/*.ps1                   one file per user-visible slice: install,
                                             Steam launch option, shortcut, update, uninstall
        src/Main.ps1                         option handling, dispatch, entry point (last)

    The fragments are concatenated in the fixed order below, each wrapped in a
    "#region <path>" block, with LF line endings. The result is checked for pure ASCII
    and for parse errors before it is written.

    -Check builds in memory and compares with the existing install.ps1 byte for byte (after
    CRLF->LF, because core.autocrlf=true checks the file out with CRLF):
    exit 0 when it is up to date, 1 when it is stale (the Pester suite runs this check).

.PARAMETER Check
    Do not write; fail (exit 1) when install.ps1 differs from a fresh build.

.PARAMETER OutputPath
    Target file. Default: install.ps1 next to this script.

.OUTPUTS
    System.String. A one-line status message.

.EXAMPLE
    .\tools\bootstrap\build-install.ps1

.EXAMPLE
    .\tools\bootstrap\build-install.ps1 -Check
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [switch]$Check,
    [ValidateNotNullOrEmpty()][string]$OutputPath = (Join-Path $PSScriptRoot 'install.ps1')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Order matters only for the header (first) and Main.ps1 (last: it holds the entry point).
$script:BuildFragments = @(
    'Header.ps1',
    'Shared/Configuration.ps1',
    'Shared/Constants.ps1',
    'Shared/Adapters.ps1',
    'Shared/Messages.ps1',
    'Shared/Logging.ps1',
    'Shared/Vdf.ps1',
    'Shared/TextFile.ps1',
    'Shared/State.ps1',
    'Shared/Manifest.ps1',
    'Shared/Steam.ps1',
    'Shared/VersionStore.ps1',
    'Features/Proxy.ps1',
    'Features/SteamLaunchOption.ps1',
    'Features/Install.ps1',
    'Features/Update.ps1',
    'Features/Shortcut.ps1',
    'Features/Uninstall.ps1',
    'Main.ps1'
)
$script:BuildSourceDir = Join-Path $PSScriptRoot 'src'
$script:BuildNewline = "`n"
$script:BuildMaxAscii = 127
$script:BuildExitStale = 1

function Get-BuildFragmentText {
    <#
    .SYNOPSIS
        Reads one fragment as text with LF line endings and no trailing blank lines.
    .DESCRIPTION
        Rejects any non-ASCII byte (this also rejects a BOM), so the output stays pure ASCII.
    .PARAMETER RelativePath
        Path under src/ with forward slashes.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-BuildFragmentText 'Shared/Vdf.ps1'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][ValidateNotNullOrEmpty()][string]$RelativePath)
    $full = Join-Path $script:BuildSourceDir ($RelativePath -replace '/', '\')
    $bytes = [IO.File]::ReadAllBytes($full)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -gt $script:BuildMaxAscii) { throw "Non-ASCII byte 0x$('{0:X2}' -f $bytes[$i]) at offset $i in $RelativePath (use \uXXXX escapes)." }
    }
    $text = [Text.Encoding]::ASCII.GetString($bytes).Replace("`r`n", $script:BuildNewline)
    return $text.TrimEnd("`n", ' ', "`t")
}

function New-InstallScriptText {
    <#
    .SYNOPSIS
        Concatenates all fragments into the text of install.ps1.
    .DESCRIPTION
        The header comes first unwrapped (its help block and param block must open the
        script); every other fragment is wrapped in #region/#endregion with its path.
        Throws on non-ASCII content or parse errors.
    .OUTPUTS
        System.String
    .EXAMPLE
        $text = New-InstallScriptText
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    $sb = New-Object System.Text.StringBuilder
    $first = $true
    foreach ($fragment in $script:BuildFragments) {
        $text = Get-BuildFragmentText $fragment
        if ($first) {
            [void]$sb.Append($text).Append($script:BuildNewline).Append($script:BuildNewline)
            [void]$sb.Append('# GENERATED FILE: edit tools/bootstrap/src/* and run tools/bootstrap/build-install.ps1.').Append($script:BuildNewline)
            $first = $false
            continue
        }
        [void]$sb.Append($script:BuildNewline).Append("#region $fragment").Append($script:BuildNewline)
        [void]$sb.Append($text).Append($script:BuildNewline)
        [void]$sb.Append("#endregion $fragment").Append($script:BuildNewline)
    }
    $result = $sb.ToString()
    foreach ($ch in $result.ToCharArray()) {
        if ([int]$ch -gt $script:BuildMaxAscii) { throw "Non-ASCII character U+$('{0:X4}' -f [int]$ch) in the build output." }
    }
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseInput($result, [ref]$tokens, [ref]$errors)
    if (@($errors).Count -gt 0) { throw "Build output does not parse: $(@($errors)[0].Message)" }
    return $result
}

$built = [Text.Encoding]::ASCII.GetBytes((New-InstallScriptText))
if ($Check) {
    # core.autocrlf=true checks the file out with CRLF; the committed (and served) bytes are LF.
    $currentText = if (Test-Path -LiteralPath $OutputPath) { [IO.File]::ReadAllText($OutputPath).Replace("`r`n", $script:BuildNewline) } else { '' }
    $current = [Text.Encoding]::ASCII.GetBytes($currentText)
    $same = ($current.Length -eq $built.Length)
    if ($same) { for ($i = 0; $i -lt $built.Length; $i++) { if ($current[$i] -ne $built[$i]) { $same = $false; break } } }
    if (-not $same) {
        Write-Output "STALE: $OutputPath differs from a fresh build of src/. Run tools/bootstrap/build-install.ps1."
        exit $script:BuildExitStale
    }
    Write-Output "UP-TO-DATE: $OutputPath"
    exit 0
}
[IO.File]::WriteAllBytes($OutputPath, $built)
Write-Output "Wrote $OutputPath ($($built.Length) bytes, $($script:BuildFragments.Count) fragments)"
