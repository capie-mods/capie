<#
.SYNOPSIS
    Installs the throwaway spike mod and injects the CAPIE spike build (S1/S2) into a running HOI4.

.DESCRIPTION
    Throwaway helper of the API 9 spikes (docs/contributing/test-session-api9-spikes.md).
    -InstallMod copies plugin/capie/spikes/capie_spike_mod into the user's HOI4 mod folder and writes
    its .mod descriptor (enable "CAPIE spike S1-S2 (throwaway)" in the launcher playset afterwards).
    -Inject loads capie_core.dll, then capie_editor.dll, from plugin/build/capie-spike (or its debug
    sub-folder with -DebugFlavor) into the one running hoi4.exe with capie_injector.exe. The game must
    have been started WITHOUT CAPIE (no CAPIE launch option), so nothing is loaded twice.

.PARAMETER InstallMod
    Copy the mod and write its descriptor.

.PARAMETER Inject
    Inject the spike DLLs into the running game.

.PARAMETER DebugFlavor
    Use plugin/build/capie-spike/debug (debug log lines) instead of the release flavor.

.EXAMPLE
    .\plugin\capie\spikes\run_spike.ps1 -InstallMod
    .\plugin\capie\spikes\run_spike.ps1 -Inject
#>
[CmdletBinding()]
param([switch]$InstallMod, [switch]$Inject, [switch]$DebugFlavor)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if ($InstallMod) {
    $docs = [Environment]::GetFolderPath('MyDocuments')
    $modRoot = Join-Path $docs 'Paradox Interactive\Hearts of Iron IV\mod'
    $target = Join-Path $modRoot 'capie_spike_mod'
    New-Item -ItemType Directory -Force -Path $modRoot | Out-Null
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'capie_spike_mod') -Destination $target -Recurse
    $descriptor = (Get-Content -LiteralPath (Join-Path $target 'descriptor.mod') -Raw).TrimEnd() + "`r`npath=`"" + ($target -replace '\\', '/') + "`"`r`n"
    [IO.File]::WriteAllText((Join-Path $modRoot 'capie_spike_mod.mod'), $descriptor, [Text.UTF8Encoding]::new($false))
    Write-Host "Spike mod installed: $target (enable it in the launcher playset)"
}
if ($Inject) {
    $build = Join-Path $repo 'plugin\build\capie-spike'
    $dlls = if ($DebugFlavor) { Join-Path $build 'debug' } else { $build }
    $injector = Join-Path $build 'capie_injector.exe'
    foreach ($file in $injector, (Join-Path $dlls 'capie_core.dll'), (Join-Path $dlls 'capie_editor.dll')) {
        if (-not (Test-Path -LiteralPath $file)) { throw "Missing $file; run build.ps1 -Spike first." }
    }
    $games = @(Get-Process -Name hoi4 -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw "Expected exactly one running hoi4.exe, found $($games.Count)." }
    foreach ($name in 'capie_core.dll', 'capie_editor.dll') {
        & $injector $games[0].Id (Join-Path $dlls $name)
        if ($LASTEXITCODE -ne 0) { throw "capie_injector.exe $name failed with exit $LASTEXITCODE (11 = already loaded: start the game without CAPIE)." }
        Write-Host "Injected $name into PID $($games[0].Id)"
    }
    Write-Host "Logs: $(Join-Path $dlls 'capie_editor.log') and capie_core.log (look for CAPIE_SPIKE)"
}
if (-not $InstallMod -and -not $Inject) { Write-Host 'Nothing to do: pass -InstallMod and/or -Inject.' }
