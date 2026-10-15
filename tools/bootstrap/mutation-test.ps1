<#
.SYNOPSIS
    Mutation test for the bootstrap: every safety check must be caught by the Pester suite.

.DESCRIPTION
    For each mutation the script copies install.ps1, install.Tests.ps1 and
    sign-release-manifest.ps1 into its own folder under -WorkRoot, disables one safety
    check by an exact text replacement in install.ps1, and runs the suite in a child
    process (Pester 3.4, the "Build" tests excluded because they compare install.ps1 with
    src/ and would kill every mutant trivially). A mutant is KILLED when at least one test
    fails. A replacement whose text is not found is reported as PATTERN NOT FOUND and
    makes the run fail, so the list cannot silently rot when the code changes.

    The unmutated copy runs first and must pass completely (sanity check).
    Nothing outside -WorkRoot is written. Exit code 0 only when every mutant is killed.

.PARAMETER WorkRoot
    Empty or disposable folder for copies and test work folders.

.PARAMETER PowerShellExe
    Interpreter for the child runs (pwsh or powershell).

.PARAMETER PesterManifest
    Path of Pester.psd1 (default: Pester 3.4.0 bundled with Windows).

.OUTPUTS
    System.String. One line per mutant: "<name> : KILLED|SURVIVED|PATTERN NOT FOUND (...)".

.EXAMPLE
    .\tools\bootstrap\mutation-test.ps1 -WorkRoot $env:TEMP\ra-mutation -PowerShellExe pwsh
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$WorkRoot,
    [ValidateSet('pwsh', 'powershell')][string]$PowerShellExe = 'pwsh',
    [ValidateNotNullOrEmpty()][string]$PesterManifest = 'C:\Program Files\WindowsPowerShell\Modules\Pester\3.4.0\Pester.psd1'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:MutationSourceFiles = @('install.ps1', 'install.Tests.ps1', 'sign-release-manifest.ps1')
$script:MutationExcludedTag = 'Build'
$script:MutationBaselineName = '(baseline, no mutation)'

# name -> list of @(exact text in install.ps1, replacement). Each mutant disables one safety property.
$script:Mutations = [ordered]@{
    'zip-hash-skip'              = @(, @('if ($hash -ne $Manifest.ZipSha256.ToUpperInvariant()) {', 'if ($false) {'))
    'file-hash-skip-both'        = @(
        @('if ($h -ne $Manifest.Files[$name].ToUpperInvariant())', 'if ($false)'),
        @("if (-not (Test-RaInstalledVersion `$Manifest `$staging)) { throw 'Staged files failed verification.' }", '')
    )
    'no-restore'                 = @(, @('& $script:RaCopyFile $backup $ConfigPath', '$null = $backup'))
    'shortcut-ownership-skip'    = @(, @('        if (Test-RaOwnShortcut $p) {', '        if ($true) {'))
    'steam-check-skip'           = @(, @('    if (-not (Test-RaSteamRunning)) { return $true }', '    return $true'))
    'no-preserve-flags'          = @(
        , @('    return "$fragment $($script:RaSteamCommandToken) $rest"', '    return "$fragment $($script:RaSteamCommandToken)"')
    )
    'signature-skip'             = @(, @('return [bool]$rsa.VerifyData($Data, $script:RaSignatureHashAlgorithm, $sig)', 'return $true'))
    'tag-check-skip'             = @(, @('if (([string]$rel.tag_name).TrimStart($script:RaReleaseTagPrefix) -ne $m.Version) {', 'if ($false) {'))
    'asset-host-skip'            = @(, @('if (-not (Test-RaReleaseAssetUrl $assets[$n])) {', 'if ($false) {'))
    'rollback-verify-skip'       = @(, @('if (-not $m -or -not (Test-RaInstalledVersion $m (Get-RaVersionDir $prev))) {', 'if (-not $m) {'))
    'no-previous-version'        = @(, @('$State.PreviousVersion = $State.InstalledVersion }', '}'))
    'zip-entry-allowlist-skip'   = @(, @("if (-not `$Manifest.Files.Contains(`$name)) { throw `"Package contains an unexpected file: `$name`" }", ''))
    'https-only-skip'            = @(, @('if ($Url -notmatch $script:RaHttpsPrefixPattern) {', 'if ($false) {'))
    'backup-overwrite'           = @(, @('    while (Test-Path -LiteralPath $backup) {', '    while ($false) {'))
    'undo-exact-restore-skip'    = @(, @('if ($null -ne $Entry -and $Current -ceq $Entry.WrittenValue) {', 'if ($false) {'))
    'purge-any-backup'           = @(, @('Where-Object { $_.Name -match $script:RaBackupNamePattern }', 'Where-Object { $_.Name -like ''localconfig.vdf.*'' }'))
    'purge-foreign-root'         = @(, @('    if ($rest.Count -gt 0) {', '    if ($false) {'))
    'disable-ignores-steam'      = @(, @("throw 'Steam is running; launch option not changed.' }`n    } else {", "}`n    } else {"))
    'latest-uses-pinned'         = @(, @('        if ($Options.Latest) { [void](Install-RaLatest', '        if ($false) { [void](Install-RaLatest'))
    'tui-keeps-yes'              = @(, @('        $script:RaAssumeYes = $savedYes', '        $null = $savedYes'))
    'mod-no-backup'              = @(, @('            if ($current -and -not ($record -and $current -ieq $record.Sha256)) {', '            if ($false) {'))
    'mod-remove-changed'         = @(, @('            if ((Get-RaFileSha256 $path) -ine $record.Sha256) {', '            if ($false) {'))
    'default-proxy'              = @(, @("`$Options.LoadMethod } else { 'Launcher' }", "`$Options.LoadMethod } else { 'Proxy' }"))
    'workshop-twin-installed'    = @(, @('    if (Test-Path -LiteralPath $workshop -PathType Leaf) {', '    if ($false) {'))
    'no-tty-waits'               = @(
        , @('    if (-not $mode.Interactive -and -not (Test-RaActionOption $options)', '    if ($false -and -not (Test-RaActionOption $options)')
    )
}

function Invoke-MutationRun {
    <#
    .SYNOPSIS
        Copies the suite, applies the replacements and runs Pester in a child process.
    .DESCRIPTION
        Returns a result object with the number of failed and passed tests, or a
        PatternNotFound flag when a replacement text is missing.
    .PARAMETER Name
        Mutant name (folder name).
    .PARAMETER Replacements
        Pairs of @(old, new); empty for the baseline.
    .PARAMETER Interpreter
        pwsh or powershell.
    .PARAMETER Pester
        Path of Pester.psd1.
    .OUTPUTS
        System.Management.Automation.PSCustomObject (Name, PatternNotFound, Failed, Passed)
    .EXAMPLE
        Invoke-MutationRun -Name 'signature-skip' -Replacements $script:Mutations['signature-skip'] -Interpreter pwsh -Pester $p
    #>
    [CmdletBinding()]
    [OutputType([psobject])]
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Replacements,
        [Parameter(Mandatory)][string]$Interpreter,
        [Parameter(Mandatory)][string]$Pester
    )
    $safeName = $Name -replace '[^0-9A-Za-z-]', '_'
    $dir = Join-Path $WorkRoot "mut\$safeName"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    foreach ($f in $script:MutationSourceFiles) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $f) -Destination $dir }
    $target = Join-Path $dir 'install.ps1'
    $code = [IO.File]::ReadAllText($target)
    foreach ($pair in $Replacements) {
        if (-not $code.Contains($pair[0])) { return [pscustomobject]@{ Name = $Name; PatternNotFound = $true; Failed = 0; Passed = 0 } }
        $code = $code.Replace($pair[0], $pair[1])
    }
    [IO.File]::WriteAllText($target, $code)
    $work = Join-Path $WorkRoot "work\$safeName"
    $tests = Join-Path $dir 'install.Tests.ps1'
    $command = @(
        "`$env:RA_BOOTSTRAP_TEST_ROOT = '$work'; `$env:RA_BOOTSTRAP_REAL_LOCALCONFIG = ''; Import-Module '$Pester'; ",
        "`$r = Invoke-Pester -Script '$tests' -ExcludeTag '$($script:MutationExcludedTag)' -PassThru -Quiet; ",
        "'RESULT failed=' + `$r.FailedCount + ' passed=' + `$r.PassedCount"
    ) -join ''
    $output = & $Interpreter -NoProfile -ExecutionPolicy Bypass -Command $command 2>&1 | Out-String
    $m = [regex]::Match($output, 'RESULT failed=(\d+) passed=(\d+)')
    if (-not $m.Success) { throw "Could not read the Pester result for $Name" }
    return [pscustomobject]@{ Name = $Name; PatternNotFound = $false; Failed = [int]$m.Groups[1].Value; Passed = [int]$m.Groups[2].Value }
}

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$baseline = Invoke-MutationRun -Name $script:MutationBaselineName -Replacements @() -Interpreter $PowerShellExe -Pester $PesterManifest
Write-Output ('{0} : failed={1} passed={2}' -f $baseline.Name, $baseline.Failed, $baseline.Passed)
if ($baseline.Failed -ne 0) { throw 'The unmutated suite fails; fix it before running mutations.' }
$allKilled = $true
foreach ($name in $script:Mutations.Keys) {
    $r = Invoke-MutationRun -Name $name -Replacements $script:Mutations[$name] -Interpreter $PowerShellExe -Pester $PesterManifest
    $verdict = if ($r.PatternNotFound) { 'PATTERN NOT FOUND' } elseif ($r.Failed -gt 0) { 'KILLED' } else { 'SURVIVED' }
    if ($verdict -ne 'KILLED') { $allKilled = $false }
    Write-Output ('{0} : {1} (failed={2} passed={3})' -f $name, $verdict, $r.Failed, $r.Passed)
}
if (-not $allKilled) { exit 1 }
exit 0
