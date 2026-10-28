<#
.SYNOPSIS
    Builds the x64 version.dll CAPIE proxy and runs offline Windows tests.
.DESCRIPTION
    Only the reviewed core/editor binaries listed in plugin/capie/deploy/bundle-inputs.json are
    accepted as bundle inputs; build_launcher.ps1 bundles the launcher from the same file, so
    the proxy and the launcher always pin the same CAPIE build. The hashes compiled into
    proxy_manifest.generated.hpp are checked against the JSON after generation, the table in
    DEPLOYMENT.md must mirror the JSON, and a launcher build output that pins other hashes is
    reported as stale. The mutable installation manifest cannot authorize different
    executable code. Does not start the game or modify its installation. Outputs
    plugin/build/capie-proxy.
.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".
.PARAMETER TrustedBundleDirectory
    Folder that holds capie_core.dll and capie_editor.dll; empty means the JSON's source paths.
    Either way the files must have the JSON's hashes.
#>
[CmdletBinding()]
param(
    [string]$VisualStudioDir = '',
    [string]$TrustedBundleDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path (Split-Path -Path $PSScriptRoot -Parent) 'NativeBuild.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'deploy\BundleInputs.psm1') -Force
$repoDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputDir = Join-Path $repoDir 'plugin\build\capie-proxy'
$testsDir = Join-Path $outputDir 'tests'
$runtimeDir = Join-Path $outputDir 'runtime'
$sourceDir = Join-Path $PSScriptRoot 'src\proxy'
$testSourceDir = Join-Path $PSScriptRoot 'tests\proxy'
# The two DLL entries of the shared bundle list, in dll_hashes order (core, editor).
$bundleInputs = @(Read-CapieBundleInput)
Assert-CapieDeploymentTable -Inputs $bundleInputs
$pins = @($bundleInputs | Where-Object { $_.dll } | ForEach-Object {
        $source = if ($TrustedBundleDirectory) { Join-Path $TrustedBundleDirectory $_.name } else { Join-Path $repoDir $_.source }
        @{ name = $_.name; sha = $_.sha256; path = $source }
    })
if (($pins.name -join ',') -cne 'capie_core.dll,capie_editor.dll') { throw 'bundle-inputs.json must list core, then editor.' }
foreach ($entry in $pins) {
    if (-not (Test-Path -LiteralPath $entry.path)) { throw "Reviewed CAPIE input missing (bundle-inputs.json): $($entry.path)" }
    if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha) {
        throw "CAPIE input differs from bundle-inputs.json: $($entry.path)"
    }
}
foreach ($dir in $outputDir, $testsDir, $runtimeDir) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
$proxy = Join-Path $outputDir 'version.dll'
Assert-NbModuleNotLoaded -ModulePath $proxy -Message 'The proxy is loaded in hoi4.exe; close the game first.'
foreach ($entry in $pins) {
    Copy-Item -LiteralPath $entry.path -Destination (Join-Path $runtimeDir $entry.name) -Force
    if ((Get-FileHash -LiteralPath (Join-Path $runtimeDir $entry.name)).Hash.ToLowerInvariant() -ne $entry.sha) {
        throw "CAPIE copy mismatch: $($entry.name)"
    }
}
$header = @('#pragma once', '#include <array>', 'namespace capie::proxy::manifest {',
    'inline constexpr std::array<const char*, 2> dll_hashes = {',
    ('    "' + $pins[0].sha + '",'), ('    "' + $pins[1].sha + '"'), '};', '}') -join "`n"
$proxyHeader = Join-Path $outputDir 'proxy_manifest.generated.hpp'
[IO.File]::WriteAllText($proxyHeader, $header + "`n", [Text.UTF8Encoding]::new($false))
# Build-time check: the compiled pins are the launcher bundle's DLL hashes.
Assert-CapieProxyMatchesBundle -ProxyHashes (Get-CapieHeaderHash -Path $proxyHeader) -Inputs $bundleInputs
$staleLauncher = Test-CapieOtherBuild -HeaderPath (Join-Path $repoDir 'plugin\build\capie-launcher\launcher_manifest.generated.hpp') `
    -Inputs $bundleInputs
if ($staleLauncher) { Write-Warning $staleLauncher }
[void](Import-NbVisualStudioEnvironment -VisualStudioDir $VisualStudioDir)
$flags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
    '/DUNICODE', '/D_UNICODE', '/std:c++20', '/EHsc', '/permissive-')
$libraries = @('bcrypt.lib', 'shell32.lib', 'ole32.lib', 'advapi32.lib')
$buildLog = Join-Path $outputDir 'build.log'
Push-Location $outputDir
try {
    Invoke-NbTool -FilePath 'ml64.exe' -ArgumentList @('/nologo', '/c', '/Fo', 'forwarders.obj', (Join-Path $sourceDir 'forwarders.asm')) `
        -FailureMessage 'Proxy assembly compilation failed.' -LogPath $buildLog
    $arguments = $flags + @('/LD', ('/I' + $outputDir), (Join-Path $sourceDir 'proxy_main.cpp'), 'forwarders.obj',
        ('/Fe:' + $proxy), '/link', ('/DEF:' + (Join-Path $sourceDir 'version.def'))) + $libraries
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Proxy compilation failed.' -LogPath $buildLog -AppendLog
    foreach ($fixture in @(@{ name = 'capie_core.dll'; label = "'C'" }, @{ name = 'capie_editor.dll'; label = "'E'" })) {
        $arguments = $flags + @('/LD', ('/DFIXTURE_LABEL=' + $fixture.label), (Join-Path $testSourceDir 'fake_payload.cpp'),
            ('/Fe:' + (Join-Path $testsDir $fixture.name)))
        Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Fixture compilation failed.' -LogPath $buildLog -AppendLog
    }
    foreach ($scenario in @('core_failure', 'editor_failure', 'missing_export')) {
        $scenarioDir = Join-Path $testsDir $scenario
        New-Item -ItemType Directory -Force -Path $scenarioDir | Out-Null
        foreach ($name in 'capie_core.dll', 'capie_editor.dll') {
            Copy-Item -LiteralPath (Join-Path $testsDir $name) -Destination (Join-Path $scenarioDir $name) -Force
        }
        $name = if ($scenario -eq 'core_failure') { 'capie_core.dll' } else { 'capie_editor.dll' }
        $defines = switch ($scenario) {
            'core_failure' { @("/DFIXTURE_LABEL='C'", '/DFIXTURE_RESULT=31') }
            'editor_failure' { @("/DFIXTURE_LABEL='E'", '/DFIXTURE_RESULT=32') }
            'missing_export' { @('/DFIXTURE_NO_EXPORT') }
        }
        $arguments = $flags + @('/LD') + $defines + @((Join-Path $testSourceDir 'fake_payload.cpp'),
            ('/Fe:' + (Join-Path $scenarioDir $name)))
        Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Failure fixture compilation failed.' -LogPath $buildLog -AppendLog
    }
    $testExe = Join-Path $testsDir 'proxy_test.exe'
    $arguments = $flags + @((Join-Path $testSourceDir 'proxy_test.cpp'), ('/Fe:' + $testExe), '/link') + $libraries
    Invoke-NbTool -FilePath 'cl.exe' -ArgumentList $arguments -FailureMessage 'Proxy test compilation failed.' -LogPath $buildLog -AppendLog
} finally { Pop-Location }
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('capie-proxy-test-' + [guid]::NewGuid().ToString('N'))
try {
    $testSummaries = @()
    foreach ($scenario in @('success', 'core_failure', 'editor_failure', 'missing_export')) {
        $caseScratch = Join-Path $scratch $scenario
        $arguments = if ($scenario -eq 'success') {
            @($caseScratch, $proxy, $testsDir)
        } else { @($caseScratch, $proxy, (Join-Path $testsDir $scenario), $scenario) }
        $lines = & $testExe @arguments 2>&1
        $code = $LASTEXITCODE
        $lines | Add-Content -LiteralPath $buildLog
        $lines | Write-Output
        $testSummaries += "$scenario`: $([string]($lines | Select-Object -Last 1))"
        if ($code -ne 0) { throw "Proxy test $scenario failed: $code" }
    }
} finally {
    # Only a fresh test-owned directory is removed. Never touch a player's data.
    $resolved = [IO.Path]::GetFullPath($scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved).StartsWith('capie-proxy-test-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$exports = & dumpbin.exe /exports $proxy
if ($LASTEXITCODE -ne 0 -or ($exports -join "`n") -notmatch '17 number of functions') {
    throw 'The proxy must export exactly 17 Windows version.dll names and ordinals.'
}
$exports | Set-Content -LiteralPath (Join-Path $outputDir 'exports.txt')
$review = [ordered]@{
    proxy = 'version.dll'; architecture = 'x64'; activation = '-capie or strict proxy.enabled marker'
    game_hash_source = 'src/core/game_build.hpp'; bundle_source = 'deploy/bundle-inputs.json'
    bundle_pins = @($pins | ForEach-Object { [ordered]@{ name = $_.name; sha = $_.sha } })
    offline_test = $testSummaries; runtime_test = 'Separate opt-in tools/test-proxy-loader.ps1'
}
$review | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDir 'build-review.json')
$sums = foreach ($file in @($proxy) + @($pins | ForEach-Object { Join-Path $runtimeDir $_.name })) {
    (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetRelativePath($outputDir, $file)
}
$sums | Set-Content -LiteralPath (Join-Path $outputDir 'SHA256SUMS.txt')
Write-Output "Built $proxy; offline verification passed."
