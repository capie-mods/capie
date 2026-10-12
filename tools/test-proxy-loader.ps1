<#
.SYNOPSIS
    Runs bounded startup checks of the CAPIE proxy against the installed HOI4.
.DESCRIPTION
    Temporarily adds version.dll to the game directory, refuses an existing file, and
    restores the three user settings files after all owned game processes have exited.
    Each case gets an isolated LOCALAPPDATA and a copy of the reviewed CAPIE runtime.
    Only processes started by this script are stopped. No saves are opened or changed.
.PARAMETER ProxyDll
    Built proxy DLL to test.
.PARAMETER RuntimeDirectory
    Folder containing the reviewed capie_core.dll and capie_editor.dll.
.PARAMETER EvidenceDirectory
    New directory for JSON observations and copied diagnostic logs.
.PARAMETER GameDirectory
    Installed Hearts of Iron IV directory.
.PARAMETER ObservationSeconds
    Time to observe each game startup, including module and initialization evidence.
.EXAMPLE
    .\tools\test-proxy-loader.ps1 -ProxyDll .\plugin\build\capie-proxy\version.dll `
        -RuntimeDirectory .\plugin\build\capie -EvidenceDirectory .\plugin\build\proxy-live
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProxyDll,
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV',
    [ValidateRange(5, 120)][int]$ObservationSeconds = 45
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
$gameExe = Join-Path $gameRoot 'hoi4.exe'
$proxySource = (Resolve-Path -LiteralPath $ProxyDll).Path
$runtimeSource = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
$installedProxy = Join-Path $gameRoot 'version.dll'
$pinnedGame = '7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5'
$runtimePins = @{
    'capie_core.dll' = 'D95B510D71F78AC5F5AB83C2EE38A803E6B5D6B651B7EBB4AD281820EB93BE00'
    'capie_editor.dll' = '742D655E2DC92AF036C09FB6F4E808039040D9F696D8908E6D4FAED02C433CCA'
}
if ((Get-FileHash -LiteralPath $gameExe).Hash -ne $pinnedGame) { throw 'Unsupported game executable.' }
if (Test-Path -LiteralPath $installedProxy) { throw 'Existing game version.dll: refusing to replace it.' }
if (Test-Path -LiteralPath $evidenceRoot) { throw 'EvidenceDirectory must be new.' }
foreach ($entry in $runtimePins.GetEnumerator()) {
    if ((Get-FileHash -LiteralPath (Join-Path $runtimeSource $entry.Key)).Hash -ne $entry.Value) {
        throw "Unreviewed runtime: $($entry.Key)"
    }
}
foreach ($existing in @(Get-Process -Name hoi4 -ErrorAction SilentlyContinue)) {
    if ($existing.Path -eq $gameExe) { throw 'The installed game is already running.' }
}
$profileRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Paradox Interactive\Hearts of Iron IV'
$profileSnapshot = @{}
foreach ($name in @('dlc_load.json', 'settings.txt', 'pdx_settings.txt')) {
    $path = Join-Path $profileRoot $name
    $profileSnapshot[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$gameFilesBefore = @(Get-ChildItem -LiteralPath $gameRoot -File | Sort-Object Name | ForEach-Object {
        [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
$proxyHash = (Get-FileHash -LiteralPath $proxySource).Hash
New-Item -ItemType Directory -Path $evidenceRoot | Out-Null
$caseResults = [Collections.Generic.List[object]]::new()
$restored = $false
$gameFilesAfter = @()
$copyCreated = $false
try {
    # Copy without overwrite; cleanup is authorized only for these exact bytes.
    [IO.File]::Copy($proxySource, $installedProxy, $false)
    $copyCreated = $true
    foreach ($caseName in @('no-flag', 'flag', 'config', 'tampered-bundle')) {
        $caseRoot = Join-Path $evidenceRoot $caseName
        $appRoot = Join-Path $caseRoot 'AppData'
        $capieRoot = Join-Path $appRoot 'RenameAnything'
        $runtimeRoot = Join-Path $capieRoot 'current\runtime'
        New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
        foreach ($name in $runtimePins.Keys) {
            Copy-Item -LiteralPath (Join-Path $runtimeSource $name) -Destination (Join-Path $runtimeRoot $name)
        }
        if ($caseName -eq 'config') {
            [IO.File]::WriteAllText((Join-Path $capieRoot 'proxy.enabled'), "CAPIE_PROXY_ENABLED_V1`n", [Text.Encoding]::ASCII)
        }
        if ($caseName -eq 'tampered-bundle') {
            $changedDll = Join-Path $runtimeRoot 'capie_editor.dll'
            $changedBytes = [IO.File]::ReadAllBytes($changedDll)
            $changedBytes[$changedBytes.Length - 1] = $changedBytes[$changedBytes.Length - 1] -bxor 1
            [IO.File]::WriteAllBytes($changedDll, $changedBytes)
        }
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $gameExe
        $startInfo.WorkingDirectory = $gameRoot
        $startInfo.UseShellExecute = $false
        $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $startInfo.Environment['LOCALAPPDATA'] = $appRoot
        $startInfo.ArgumentList.Add('-gdpr-compliant')
        if ($caseName -in @('flag', 'tampered-bundle')) { $startInfo.ArgumentList.Add('-capie') }
        $owned = [Diagnostics.Process]::Start($startInfo)
        $modules = @{}
        $samples = [Collections.Generic.List[object]]::new()
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $observationError = $null
        Write-Output "Starting $caseName, owned PID $($owned.Id)"
        try {
            while ($clock.Elapsed.TotalSeconds -lt $ObservationSeconds -and -not $owned.HasExited) {
                $owned.Refresh()
                foreach ($module in $owned.Modules) {
                    if ($module.ModuleName -in @('version.dll', 'capie_core.dll', 'capie_editor.dll') -and -not $modules.ContainsKey($module.FileName)) {
                        $modules[$module.FileName] = [ordered]@{
                            name = $module.ModuleName; path = $module.FileName; first_seen_ms = $clock.ElapsedMilliseconds
                        }
                    }
                }
                $samples.Add([ordered]@{ elapsed_ms = $clock.ElapsedMilliseconds; alive = -not $owned.HasExited })
                Start-Sleep -Milliseconds 500
            }
        } catch {
            $observationError = $_.Exception.Message
        } finally {
            $exitedBeforeStop = $owned.HasExited
            $exitCodeBeforeStop = if ($exitedBeforeStop) { $owned.ExitCode } else { $null }
            if (-not $owned.HasExited) {
                if ($owned.CloseMainWindow()) { [void]$owned.WaitForExit(5000) }
                if (-not $owned.HasExited) { $owned.Kill($true); [void]$owned.WaitForExit(10000) }
            }
            $owned.Dispose()
        }
        $expectedCapie = $caseName -in @('flag', 'config')
        $proxyLogPath = Join-Path $capieRoot 'proxy.log'
        $proxyLog = if (Test-Path -LiteralPath $proxyLogPath) { [IO.File]::ReadAllText($proxyLogPath) } else { '' }
        $initializationMatches = if ($expectedCapie) {
            $proxyLog.Contains('loaded core=0 editor=0; code=0')
        } elseif ($caseName -eq 'tampered-bundle') {
            $proxyLog.Contains('invalid_bundle; CAPIE not initialized')
        } else { $proxyLog -eq '' }
        $proxyObserved = @($modules.Values | Where-Object { $_.path -eq $installedProxy }).Count -eq 1
        $capieCount = @($modules.Values | Where-Object { $_.name -in @('capie_core.dll', 'capie_editor.dll') }).Count
        $moduleCountMatches = $capieCount -eq $(if ($expectedCapie) { 2 } else { 0 })
        $result = [ordered]@{
            case = $caseName; observed_seconds = $clock.Elapsed.TotalSeconds
            exited_before_stop = $exitedBeforeStop; exit_code_before_stop = $exitCodeBeforeStop
            proxy_observed = $proxyObserved; capie_modules = $capieCount; expected_capie = $expectedCapie
            modules = @($modules.Values); samples = @($samples); observation_error = $observationError
            proxy_log = $proxyLog; initialization_matches = $initializationMatches
            passed = $proxyObserved -and $moduleCountMatches -and $initializationMatches -and -not $exitedBeforeStop -and -not $observationError
        }
        $caseResults.Add($result)
        Write-Output "$caseName passed=$($result.passed) proxy=$proxyObserved CAPIE=$capieCount early-exit=$exitedBeforeStop"
    }
} finally {
    if ($copyCreated) {
        if ((Get-FileHash -LiteralPath $installedProxy).Hash -ne $proxyHash) {
            throw 'Test proxy changed externally; refusing to delete it.'
        }
        Remove-Item -LiteralPath $installedProxy
    }
    foreach ($path in $profileSnapshot.Keys) {
        $bytes = $profileSnapshot[$path]
        if ($null -eq $bytes) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
        } else { [IO.File]::WriteAllBytes($path, $bytes) }
    }
    $gameFilesAfter = @(Get-ChildItem -LiteralPath $gameRoot -File | Sort-Object Name | ForEach-Object {
            [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
        })
    $restored = ($gameFilesBefore | ConvertTo-Json -Depth 4 -Compress) -ceq ($gameFilesAfter | ConvertTo-Json -Depth 4 -Compress)
    $report = [ordered]@{
        date = [DateTimeOffset]::Now.ToString('o'); game = $gameExe; game_sha256 = $pinnedGame
        proxy_sha256 = $proxyHash; runtime_sha256 = $runtimePins; cases = @($caseResults)
        game_directory_restored = $restored; game_files_before = $gameFilesBefore; game_files_after = $gameFilesAfter
        limitations = @('Startup/module test only; no editor interactions, saves or multiplayer.',
            'Does not establish Steam/Paradox argument forwarding or before-main hook installation.',
            'Native Windows observation; REA PTY process capture is unavailable on Windows.')
    }
    [IO.File]::WriteAllText((Join-Path $evidenceRoot 'results.json'), ($report | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}
if (-not $restored) { throw 'Game directory differs after cleanup; inspect results.json.' }
if (@($caseResults | Where-Object { -not $_.passed }).Count -gt 0) { throw 'One or more startup cases failed; inspect results.json and proxy logs.' }
Write-Output "All startup cases passed. Game directory restored. Evidence: $evidenceRoot"
