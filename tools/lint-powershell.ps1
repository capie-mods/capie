<#
.SYNOPSIS
    Runs PSScriptAnalyzer with the repository settings and project rules on PowerShell files.

.DESCRIPTION
    Loads PSScriptAnalyzer either from -ModulePath (a folder produced by
    "Save-Module PSScriptAnalyzer -Path <dir>", so nothing is installed into the user
    profile) or from the installed modules, then analyses every *.ps1, *.psm1 and
    *.psd1 file under -Path with PSScriptAnalyzerSettings.psd1 and the custom rules in
    tools/lint/RaScriptRules.psm1. Vendored and third-party trees are skipped (see
    -ExcludePath). Exit code: 0 = no findings, 1 = findings or analyzer failure.

.PARAMETER ModulePath
    Optional folder that contains PSScriptAnalyzer (either the module folder itself or a
    parent of it, as created by Save-Module). Without it the installed module is used.

.PARAMETER Path
    Files or folders to analyse. Default: the repository root.

.PARAMETER ExcludePath
    Repository-relative path prefixes that are skipped. The default skips vendored
    code (.git, .venv, .gstack, node_modules, site output) and
    plugin/probes/caption_refresh_candidate, which is owned by a separate work stream
    and is cleaned up there.

.PARAMETER PassThru
    Return the diagnostic records instead of only printing them.

.OUTPUTS
    Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord (with -PassThru)

.EXAMPLE
    Save-Module PSScriptAnalyzer -Path $env:TEMP\psmodules
    .\tools\lint-powershell.ps1 -ModulePath $env:TEMP\psmodules

.EXAMPLE
    .\tools\lint-powershell.ps1 -Path .\tools\bootstrap
#>
[CmdletBinding()]
[OutputType([object])]
param(
    [string]$ModulePath = '',
    [string[]]$Path = @(),
    [string[]]$ExcludePath = @(
        '.git', '.venv', '.gstack', 'node_modules', 'site', 'docs/site',
        'plugin/probes/caption_refresh_candidate'
    ),
    [switch]$PassThru
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:LintModuleName = 'PSScriptAnalyzer'
$script:LintExtensions = @('.ps1', '.psm1', '.psd1')
$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:SettingsPath = Join-Path $script:RepoRoot 'PSScriptAnalyzerSettings.psd1'
$script:CustomRulePath = Join-Path $PSScriptRoot 'lint\RaScriptRules.psm1'
$script:LintMaxAttempts = 3
$script:LintRacingRule = 'PSUseCorrectCasing'

function Import-LintAnalyzer {
    <#
    .SYNOPSIS
        Imports PSScriptAnalyzer from a saved-module folder or from the installed modules.
    .DESCRIPTION
        Looks for PSScriptAnalyzer.psd1 directly in -Folder, in -Folder\PSScriptAnalyzer\<version>
        or in -Folder\<version>, newest version first. Throws when nothing is found.
    .PARAMETER Folder
        Optional saved-module folder; empty means "use the installed module".
    .OUTPUTS
        System.Management.Automation.PSModuleInfo
    .EXAMPLE
        Import-LintAnalyzer -Folder C:\tmp\modules
    #>
    [CmdletBinding()]
    [OutputType([System.Management.Automation.PSModuleInfo])]
    param([AllowEmptyString()][string]$Folder)
    if (-not $Folder) {
        if (-not (Get-Module -ListAvailable -Name $script:LintModuleName)) {
            throw "$($script:LintModuleName) is not installed. Pass -ModulePath <folder from Save-Module>."
        }
        return (Import-Module -Name $script:LintModuleName -PassThru)
    }
    $manifests = @(Get-ChildItem -LiteralPath $Folder -Recurse -Filter "$($script:LintModuleName).psd1" -File |
            Sort-Object -Property { (Split-Path -Leaf $_.DirectoryName) -as [version] } -Descending)
    if ($manifests.Count -eq 0) { throw "No $($script:LintModuleName).psd1 under $Folder" }
    return (Import-Module -Name $manifests[0].FullName -PassThru)
}

function Test-LintExcluded {
    <#
    .SYNOPSIS
        Tests whether a file lies under one of the excluded repository-relative prefixes.
    .DESCRIPTION
        Comparison is case-insensitive and uses forward slashes.
    .PARAMETER FullName
        Absolute path of the file.
    .PARAMETER Prefixes
        Repository-relative prefixes to skip.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-LintExcluded -FullName C:\repo\.venv\x.ps1 -Prefixes '.venv'
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)][string]$FullName,
        [AllowEmptyCollection()][string[]]$Prefixes
    )
    $relative = $FullName.Substring($script:RepoRoot.Length).TrimStart('\', '/').Replace('\', '/')
    foreach ($prefix in $Prefixes) {
        $p = $prefix.Replace('\', '/').TrimEnd('/')
        if ($relative -ieq $p -or $relative.StartsWith($p + '/', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Get-LintFile {
    <#
    .SYNOPSIS
        Lists the PowerShell files to analyse.
    .DESCRIPTION
        Expands folders recursively, keeps only PowerShell extensions and drops excluded paths.
    .PARAMETER Root
        Files or folders to expand.
    .PARAMETER Prefixes
        Repository-relative prefixes to skip.
    .OUTPUTS
        System.IO.FileInfo
    .EXAMPLE
        Get-LintFile -Root C:\repo -Prefixes '.git'
    #>
    [CmdletBinding()]
    [OutputType([System.IO.FileInfo], [object[]])]
    param(
        [Parameter(Mandatory)][string[]]$Root,
        [AllowEmptyCollection()][string[]]$Prefixes
    )
    $all = foreach ($r in $Root) {
        $item = Get-Item -LiteralPath $r
        if ($item.PSIsContainer) { Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force } else { $item }
    }
    return @($all | Where-Object { $script:LintExtensions -contains $_.Extension.ToLowerInvariant() } |
            Where-Object { -not (Test-LintExcluded -FullName $_.FullName -Prefixes $Prefixes) } |
            Sort-Object -Property FullName -Unique)
}

function Invoke-LintFile {
    <#
    .SYNOPSIS
        Analyses one file, retrying when PSScriptAnalyzer itself crashes.
    .DESCRIPTION
        PSScriptAnalyzer 1.25 occasionally throws a NullReferenceException from
        PSUseCorrectCasing (CommandInfo.Parameters, a race with other rules in its parallel
        rule runner). The analysis is retried up to $script:LintMaxAttempts times; after
        that the file is analysed in two passes that cannot race: every rule except
        PSUseCorrectCasing, then PSUseCorrectCasing alone. A failure there is rethrown,
        so an analyzer crash can never pass silently.
    .PARAMETER FullName
        File to analyse.
    .OUTPUTS
        Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord
    .EXAMPLE
        Invoke-LintFile -FullName C:\repo\tools\test-session.ps1
    #>
    [CmdletBinding()]
    [OutputType([object], [object[]])]
    param([Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$FullName)
    $analyzerArgs = @{
        Path                = $FullName
        Settings            = $script:SettingsPath
        CustomRulePath      = $script:CustomRulePath
        IncludeDefaultRules = $true
        ErrorAction         = 'Stop'
    }
    for ($attempt = 1; $attempt -le $script:LintMaxAttempts; $attempt++) {
        try {
            return @(Invoke-ScriptAnalyzer @analyzerArgs)
        } catch {
            Write-Verbose "Analyzer crashed on $FullName (attempt $attempt): $($_.Exception.Message)"
        }
    }
    $first = @(Invoke-ScriptAnalyzer @analyzerArgs -ExcludeRule $script:LintRacingRule)
    $second = @(Invoke-ScriptAnalyzer -Path $FullName -Settings $script:SettingsPath -IncludeRule $script:LintRacingRule -ErrorAction Stop)
    return @($first + $second)
}

[void](Import-LintAnalyzer -Folder $ModulePath)
$roots = if ($Path.Count -gt 0) { $Path } else { @($script:RepoRoot) }
$files = @(Get-LintFile -Root $roots -Prefixes $ExcludePath)
$records = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
    foreach ($r in (Invoke-LintFile -FullName $file.FullName)) { $records.Add($r) }
}
foreach ($r in $records) {
    $rel = $r.ScriptPath.Substring($script:RepoRoot.Length).TrimStart('\')
    Write-Information -MessageData ('{0}:{1}: [{2}] {3}: {4}' -f $rel, $r.Line, $r.Severity, $r.RuleName, $r.Message) -InformationAction Continue
}
Write-Information -MessageData ('Analysed {0} file(s); findings: {1}' -f $files.Count, $records.Count) -InformationAction Continue
if ($PassThru) { $records.ToArray() }
if ($records.Count -gt 0) { exit 1 }
exit 0
