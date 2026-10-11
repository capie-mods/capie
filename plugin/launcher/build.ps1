param([string]$VisualStudioDir = '')
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'This development build script requires PowerShell 7; the built launcher does not.' }
$sourceDir = $PSScriptRoot
$repoDir = [IO.Path]::GetFullPath((Join-Path $sourceDir '..\..'))
$outputDir = Join-Path $repoDir 'plugin\build\launcher-candidate'
$runtimeDir = Join-Path $outputDir 'runtime'
$inputs = @(
    @{ source='plugin/build/country-capability/rename_anything_country.dll'; name='rename_anything_country.dll'; sha='18841A6D7552224F0D34BBC05983916912969735109A548C16DE4581845E10B5'; dll=$true },
    @{ source='plugin/build/country-capability/rename_anything_injector.exe'; name='rename_anything_injector.exe'; sha='DF8D60E9F1C403193DAC79FE5229793569E30D01D17F5172C5B11F1CB14063C7'; dll=$false },
    @{ source='plugin/build/inline-editor-render-bridge/rename_anything_inline_editor_bridge.dll'; name='rename_anything_inline_editor_bridge.dll'; sha='B20FBE88756F7F22D989230D6D51953D82BCCD2349CD00BDCC545C382DF8BAF6'; dll=$true },
    @{ source='plugin/build/inline-editor-render-bridge/inject_inline_editor_render_bridge.exe'; name='inject_inline_editor_render_bridge.exe'; sha='31B510BECD5DCBA3588231C8312C32B1F2C2CC49B0CAFC8BBB2A51B18F455AE4'; dll=$false }
)
$protectedNames = @('plugin/src/injector.cpp','plugin/src/image_guard.hpp','plugin/src/country.cpp','plugin/src/country_capability.hpp','plugin/build.ps1') + @($inputs | ForEach-Object source)
$baselinePath = Join-Path $repoDir 'probes/rn_inline_editor_product_qa/source-baseline.json'
if (Test-Path -LiteralPath $baselinePath) {
    $protectedNames += @((Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json).PSObject.Properties.Name)
}
$protectedNames = @($protectedNames | Sort-Object -Unique)
function Protected-Hashes {
    $answer = [ordered]@{}
    foreach ($name in $protectedNames) { $answer[$name] = (Get-FileHash -LiteralPath (Join-Path $repoDir $name) -Algorithm SHA256).Hash }
    return $answer
}
$before = Protected-Hashes
foreach ($entry in $inputs) {
    if ($before[$entry.source] -ne $entry.sha) { throw "Fixed accepted input mismatch: $($entry.source)" }
}
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
foreach ($entry in $inputs) {
    Copy-Item -LiteralPath (Join-Path $repoDir $entry.source) -Destination (Join-Path $runtimeDir $entry.name) -Force
    if ((Get-FileHash -LiteralPath (Join-Path $runtimeDir $entry.name) -Algorithm SHA256).Hash -ne $entry.sha) { throw 'Bundle copy mismatch.' }
}
Copy-Item -LiteralPath (Join-Path $repoDir 'plugin/LICENSE.MinHook.txt') -Destination (Join-Path $outputDir 'LICENSE.MinHook.txt') -Force
$manifest = @('#pragma once', '// Build-generated from four fixed, accepted binaries only.', 'namespace rnl::manifest {',
    'inline constexpr char game_sha256[]="7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5";',
    'inline constexpr std::array<BundleEntry,4> files={{')
foreach ($entry in $inputs) {
    $digest = (Get-FileHash -LiteralPath (Join-Path $runtimeDir $entry.name) -Algorithm SHA256).Hash.ToLowerInvariant()
    $isDll = if ($entry.dll) { 'true' } else { 'false' }
    $manifest += ' {L"' + $entry.name + '","' + $digest + '",' + $isDll + '},'
}
$manifest += '}};', '}'
[IO.File]::WriteAllText((Join-Path $outputDir 'manifest.generated.hpp'), ($manifest -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ x64 tools are required to BUILD only.' }
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$buildLog = Join-Path $outputDir 'build.log'
$cliResults = [Collections.Generic.List[object]]::new()
function Run-ReadOnlyCli([string]$executable, [string[]]$arguments, [int]$expected) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $child = [Diagnostics.Process]::Start($start)
    try {
        if (-not $child.WaitForExit(10000)) { throw 'Read-only CLI timed out; not terminated. No GUI/game launch is permitted in these tests.' }
        $stdout = $child.StandardOutput.ReadToEnd()
        $stderr = $child.StandardError.ReadToEnd()
        $result = [ordered]@{ executable=$executable; arguments=$arguments; exit=$child.ExitCode; expected=$expected; stdout=$stdout.Trim(); stderr=$stderr.Trim() }
        $cliResults.Add($result)
        if ($child.ExitCode -ne $expected) { throw "CLI expected $expected, observed $($child.ExitCode): $stderr" }
    } finally { $child.Dispose() }
}
Push-Location $outputDir
try {
    $common = @('/nologo','/O2','/MT','/W4','/WX','/utf-8','/std:c++20','/EHsc','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/DUNICODE','/D_UNICODE')
    & cl.exe @common (Join-Path $sourceDir 'launcher.cpp') ('/I' + $outputDir) /link bcrypt.lib user32.lib shell32.lib ole32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" /OUT:RenameAnythingLauncher.exe 2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed.' }
    & cl.exe @common (Join-Path $sourceDir 'launcher_test.cpp') /link bcrypt.lib shell32.lib /OUT:launcher_test.exe 2>&1 | Tee-Object -FilePath $buildLog -Append
    if ($LASTEXITCODE -ne 0) { throw 'Launcher test compilation failed.' }
    & .\launcher_test.exe (Join-Path $outputDir 'component-fixtures') 2>&1 | Tee-Object -FilePath $buildLog -Append
    if ($LASTEXITCODE -ne 0) { throw 'Launcher component tests failed.' }
    $exe = Join-Path $outputDir 'RenameAnythingLauncher.exe'
    $cliDir = Join-Path $outputDir 'cli-fixtures'
    $emptyDir = Join-Path $cliDir 'missing-bundle'
    New-Item -ItemType Directory -Force -Path $emptyDir | Out-Null
    $wrongHost = Join-Path $cliDir 'hoi4.exe'
    Copy-Item -LiteralPath $exe -Destination $wrongHost -Force
    $emptyExe = Join-Path $emptyDir 'RenameAnythingLauncher.exe'
    Copy-Item -LiteralPath $exe -Destination $emptyExe -Force
    Run-ReadOnlyCli $exe @('--help') 0
    Run-ReadOnlyCli $exe @('--game',$wrongHost,'--verify-only') 10
    Run-ReadOnlyCli $exe @('--game',(Join-Path $cliDir 'does-not-exist\hoi4.exe'),'--verify-only') 10
    Run-ReadOnlyCli $exe @('--game','hoi4.exe','--verify-only') 10
    Run-ReadOnlyCli $exe @('--verify-only') 2
    Run-ReadOnlyCli $exe @('--game',$wrongHost,'--verify-only','--dll','arbitrary.dll') 2
    Run-ReadOnlyCli $emptyExe @('--game',$wrongHost,'--verify-only') 20
    Run-ReadOnlyCli $emptyExe @('--help') 0
    & mt.exe -nologo ('-inputresource:' + $exe + ';#1') ('-out:' + (Join-Path $outputDir 'launcher.embedded.manifest')) 2>&1 | Tee-Object -FilePath $buildLog -Append
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect own application manifest.' }
    if ((Get-Content -LiteralPath (Join-Path $outputDir 'launcher.embedded.manifest') -Raw) -notmatch 'level="asInvoker"') { throw 'Launcher must not request elevation.' }
} finally { Pop-Location }
$after = Protected-Hashes
foreach ($name in $protectedNames) { if ($before[$name] -ne $after[$name]) { throw "Protected file changed: $name" } }
if (Select-String -LiteralPath $buildLog -Pattern '\bwarning\s+[A-Z]+[0-9]+' -Quiet) { throw 'Own build warnings are not accepted.' }
$sourceHashes = [ordered]@{}
Get-ChildItem -LiteralPath $sourceDir -File | Sort-Object Name | ForEach-Object { $sourceHashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
$report = [ordered]@{
    date=[DateTimeOffset]::Now.ToString('o'); status='Built offline; no HOI4/injector/gameplay/UI activation'; runtime_accepted=$false;
    warnings=0; cli_readonly_cases=$cliResults; source_hashes=$sourceHashes;
    launcher_sha256=(Get-FileHash -LiteralPath (Join-Path $outputDir 'RenameAnythingLauncher.exe') -Algorithm SHA256).Hash;
    protected_count=$protectedNames.Count; protected_before=$before; protected_after=$after; fixed_bundle=$inputs;
    limits=@('Fresh runtime startup timing/playset integration PENDING','WaitForInputIdle is not menu readiness proof','Failure guardian can remain for a hung already-created helper','Accepted native product/focus/Enter remain separate')
}
[IO.File]::WriteAllText((Join-Path $outputDir 'build-review.json'), ($report | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Built offline launcher candidate only: $outputDir"
