<#
.SYNOPSIS
    Builds CAPIE 1.5 (capie_core.dll, capie_editor.dll, capie_injector.exe, capie_debug_control.exe) and runs every offline test.

.DESCRIPTION
    Output: -OutputDirectory (default plugin/build/capie/, release flavor) and its debug/
    subfolder (debug flavor: key log, Execute observer, ring and RNDebugRequest; plus the
    pinned debug controller). Tests and benchmarks are built into its tests/ subfolder.

    Compiles MinHook (third-party C, /W4 without /WX), then every CAPIE source with
    /MT /W4 /WX /std:c++20. Runs the ported accepted suites (inherited debug-bridge suite,
    caption core, focus release, integration in both flavors, trace ring, country protocol,
    editor input, country status), the game-call fault latch, the new tests (signature table vs EXE file, country
    differential vs the accepted candidate source, hot-path benchmarks), the public-contract
    tests (grammar and alphabet, CAPIE_* keys, grammar editors in both flavors) and the Python
    checks (signature table, status localisation, native alphabet vs the SDK table, addresses in
    comments).

    Files of other repositories (the demo mod's .gui, the SDK and Rename Anything character
    tables, Rename Anything's localisation) are read from their vendored copies in
    tests/fixtures/external, whose SHA-256 values tests/check_external_fixtures.py checks first.
    No sibling checkout or junction is needed.

    It never starts, inspects, injects into or reads memory of a game process. The EXE is
    only read as a file for the static fingerprint checks. Refuses to overwrite a locked
    (possibly loaded) DLL, and refuses an output folder that is (or contains) the reviewed
    build named by deploy/bundle-inputs.json. Accepted builds under plugin/build/* are not touched.

.PARAMETER MinHookDir
    MinHook source folder (needs include\MinHook.h and LICENSE.txt).

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER GameExe
    hoi4.exe used for the static file checks (read only). Missing file = checks skipped (reported).

.PARAMETER Python
    Python interpreter for check_signature_table.py.

.PARAMETER SkipBenchmarks
    Do not run the hot-path benchmarks. Without it only the CAPIE benchmarks run; the old
    comparisons with the rev7 bridge and country-status sources are skipped with a notice
    (those sources target HOI4 1.19.3.0).

.PARAMETER OutputDirectory
    Output folder relative to the repository root (default plugin\build\capie). The reviewed
    folder that deploy/bundle-inputs.json names (the binaries other scripts pin by hash) is
    refused: build into a scratch folder and compare the hashes instead.

.PARAMETER Console
    Developer build with the console-command candidate (CAPIE_CONSOLE_CANDIDATE=1, see
    src/features/console/). Without -OutputDirectory the output goes to
    plugin\build\capie-console-dev. Never use it for a release or a launcher package.

.OUTPUTS
    None. Prints a summary line per step.

.EXAMPLE
    .\plugin\capie\build.ps1 -MinHookDir 'C:\path\to\minhook'
#>
[CmdletBinding()]
[OutputType([void])]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$MinHookDir,
    [string]$VisualStudioDir = '',
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe',
    [string]$Python = 'python',
    [switch]$SkipBenchmarks,
    [ValidateNotNullOrEmpty()][string]$OutputDirectory = 'plugin\build\capie',
    [switch]$Console
)
if ($Console -and -not $PSBoundParameters.ContainsKey('OutputDirectory')) {
    $OutputDirectory = 'plugin\build\capie-console-dev'  # developer-only output, never the default folder
}
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:CapieDir = $PSScriptRoot
$script:SourceDir = Join-Path $PSScriptRoot 'src'
$script:TestDir = Join-Path $PSScriptRoot 'tests'
$script:RepoDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:OutDir = [IO.Path]::GetFullPath((Join-Path $script:RepoDir $OutputDirectory))
# The reviewed binaries are pinned by hash and MSVC output is not reproducible: never build over them.
Import-Module (Join-Path $PSScriptRoot 'deploy\BundleInputs.psm1') -Force
$script:ReviewedDir = [IO.Path]::GetFullPath((Join-Path $script:RepoDir (Get-CapieReviewedDirectory -Inputs @(Read-CapieBundleInput))))
$outKey = $script:OutDir.TrimEnd('\') + '\'
if ($outKey.StartsWith($script:ReviewedDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "$OutputDirectory is the reviewed build named by deploy/bundle-inputs.json; build into a scratch folder and compare the hashes."
}
$script:ExternalDir = Join-Path $script:TestDir 'fixtures\external'
$script:DebugDir = Join-Path $script:OutDir 'debug'
$script:TestOutDir = Join-Path $script:OutDir 'tests'
$script:ObjDir = Join-Path $script:OutDir 'obj'
$script:CommonFlags = @('/nologo', '/O2', '/MT', '/W4', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
$script:CppFlags = $script:CommonFlags + @('/WX', '/std:c++20', '/EHsc', '/permissive-')
if ($Console) { $script:CppFlags += '/DCAPIE_CONSOLE_CANDIDATE=1' }  # src/features/console/ wiring

function Import-VisualStudioEnvironment {
    <#
    .SYNOPSIS
        Imports the vcvars64 environment into this process (without printing it).
    .PARAMETER Directory
        Visual Studio installation; empty = vswhere.
    .OUTPUTS
        None.
    .EXAMPLE
        Import-VisualStudioEnvironment -Directory ''
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$Directory)
    if (-not $Directory) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $Directory = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $Directory) { throw 'Visual Studio C++ x64 tools are required.' }
    $vcvars = Join-Path $Directory 'VC\Auxiliary\Build\vcvars64.bat'
    foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}

function Invoke-Checked {
    <#
    .SYNOPSIS
        Runs a native tool and throws with a message when it fails.
    .PARAMETER FilePath
        Executable.
    .PARAMETER ArgumentList
        Arguments.
    .PARAMETER Failure
        Message on a non-zero exit code.
    .OUTPUTS
        None (tool output is passed through).
    .EXAMPLE
        Invoke-Checked cl.exe @('/c', 'a.cpp') 'compile failed'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$FilePath, [string[]]$ArgumentList, [string]$Failure)
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) { throw "$Failure (exit $LASTEXITCODE)" }
}

function Assert-NotLocked {
    <#
    .SYNOPSIS
        Refuses to overwrite a DLL that is locked (possibly loaded by a process).
    .PARAMETER Path
        DLL path.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-NotLocked 'C:\x\capie_core.dll'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        try { [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None').Dispose() }
        catch { throw "$Path is locked (possibly loaded). Do not rebuild a loaded module." }
    }
}

function Build-Flavor {
    <#
    .SYNOPSIS
        Builds capie_core.dll, capie_editor.dll and capie_injector.exe of one flavor.
    .PARAMETER Directory
        Output folder.
    .PARAMETER DebugFlavor
        1 for the debug flavor, 0 for release.
    .OUTPUTS
        None.
    .EXAMPLE
        Build-Flavor -Directory $script:OutDir -DebugFlavor 0
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([string]$Directory, [int]$DebugFlavor)
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    foreach ($name in 'capie_core.dll', 'capie_editor.dll') { Assert-NotLocked (Join-Path $Directory $name) }
    Push-Location $Directory
    try {
        $flavor = "/DCAPIE_DEBUG_DIAGNOSTICS=$DebugFlavor"
        Invoke-Checked cl.exe ($script:CppFlags + @($flavor, '/LD', (Join-Path $script:SourceDir 'dll\capie_core.cpp'),
                $script:MinHookInclude) + $script:MinHookObjects + @('/link', 'bcrypt.lib', '/OUT:capie_core.dll')) 'capie_core.dll compilation failed'
        Invoke-Checked cl.exe ($script:CppFlags + @($flavor, '/LD', (Join-Path $script:SourceDir 'dll\capie_editor.cpp'),
                $script:MinHookInclude) + $script:MinHookObjects + @('/link', 'bcrypt.lib', '/OUT:capie_editor.dll')) 'capie_editor.dll compilation failed'
        Invoke-Checked cl.exe ($script:CppFlags + @((Join-Path $script:SourceDir 'tools\capie_injector.cpp'),
                '/link', 'bcrypt.lib', '/OUT:capie_injector.exe')) 'capie_injector.exe compilation failed'
        Copy-Item -LiteralPath $script:LicensePath -Destination (Join-Path $Directory 'LICENSE.MinHook.txt') -Force
    } finally { Pop-Location }
}

function Invoke-Test {
    <#
    .SYNOPSIS
        Compiles one test source and runs it; throws on a compile or test failure.
    .PARAMETER Source
        Test source (relative to tests/).
    .PARAMETER Output
        Executable name.
    .PARAMETER Flags
        Extra compiler flags.
    .PARAMETER Arguments
        Run-time arguments.
    .PARAMETER Link
        Extra objects/linker arguments placed before /link.
    .OUTPUTS
        System.String. The last output line of the test.
    .EXAMPLE
        Invoke-Test 'legacy\protocol_test.cpp' 'protocol_test.exe' @('/UNDEBUG')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([string]$Source, [string]$Output, [string[]]$Flags = @(), [string[]]$Arguments = @(), [string[]]$Link = @())
    Push-Location $script:TestOutDir
    try {
        Invoke-Checked cl.exe ($script:CppFlags + $Flags + @((Join-Path $script:TestDir $Source), $script:MinHookInclude) +
            $Link + @('/link', 'bcrypt.lib', "/OUT:$Output")) "$Output compilation failed"
        $lines = & (Join-Path $script:TestOutDir $Output) @Arguments 2>&1
        $code = $LASTEXITCODE
        $lines | ForEach-Object { Write-Verbose $_ }
        if ($code -ne 0) { $lines | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }; throw "$Output failed (exit $code)" }
        $last = ($lines | Select-Object -Last 1)
        Write-Host ("  {0,-34} {1}" -f $Output, $last)
        return [string]$last
    } finally { Pop-Location }
}

Import-VisualStudioEnvironment -Directory $VisualStudioDir
$MinHookDir = (Resolve-Path -LiteralPath $MinHookDir).Path
$script:MinHookInclude = '/I' + (Join-Path $MinHookDir 'include')
$script:LicensePath = Join-Path $MinHookDir 'LICENSE.txt'
if (-not (Test-Path -LiteralPath (Join-Path $MinHookDir 'include\MinHook.h')) -or -not (Test-Path -LiteralPath $script:LicensePath)) {
    throw 'MinHook headers and LICENSE.txt are required.'
}
foreach ($dir in $script:OutDir, $script:DebugDir, $script:TestOutDir, $script:ObjDir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

# 1. MinHook (third-party C: /W4 without /WX).
Push-Location $script:ObjDir
try {
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    Invoke-Checked cl.exe ($script:CommonFlags + @('/c', '/TC') + $sources + @($script:MinHookInclude)) 'MinHook compilation failed'
} finally { Pop-Location }
$script:MinHookObjects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj') | ForEach-Object { Join-Path $script:ObjDir $_ }

# 2. Binaries, both flavors.
Build-Flavor -Directory $script:OutDir -DebugFlavor 0
Build-Flavor -Directory $script:DebugDir -DebugFlavor 1
$debugEditor = Join-Path $script:DebugDir 'capie_editor.dll'
$debugHash = (Get-FileHash -LiteralPath $debugEditor -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = '#pragma once' + "`n" + '// Generated by plugin/capie/build.ps1: pins the debug-flavor capie_editor.dll.' + "`n" +
    'inline constexpr wchar_t capie_debug_dll_name[] = L"capie_editor.dll";' + "`n" +
    'inline constexpr wchar_t capie_debug_dll_sha256[] = L"' + $debugHash + '";' + "`n"
[IO.File]::WriteAllText((Join-Path $script:DebugDir 'capie_debug_manifest.hpp'), $manifest, [Text.UTF8Encoding]::new($false))
Push-Location $script:DebugDir
try {
    Invoke-Checked cl.exe ($script:CppFlags + @((Join-Path $script:SourceDir 'tools\capie_debug_control.cpp'), ('/I' + $script:DebugDir),
            '/link', 'bcrypt.lib', '/OUT:capie_debug_control.exe')) 'capie_debug_control.exe compilation failed'
    if ((Get-FileHash -LiteralPath $debugEditor -Algorithm SHA256).Hash.ToLowerInvariant() -ne $debugHash) { throw 'Debug DLL changed during the controller build.' }
    Invoke-Checked '.\capie_debug_control.exe' @('--help') 'Debug controller help check failed'
} finally { Pop-Location }
Write-Host "Built release flavor in $script:OutDir and debug flavor in $script:DebugDir"

# compile_commands.json for clang-tidy (tools/run-clang-tidy.ps1); debug flavor covers every code path.
# Sources plus the new offline tests (tests/legacy and tests/compat are copies of accepted code
# and are not analysed). The release flavor of capie_editor.cpp is analysed separately by the
# tidy script with /DCAPIE_DEBUG_DIAGNOSTICS=0.
$tidyFiles = @('dll\capie_core.cpp', 'dll\capie_editor.cpp', 'tools\capie_injector.cpp', 'tools\capie_debug_control.cpp' |
        ForEach-Object { Join-Path $script:SourceDir $_ }) +
    @('memory_test.cpp', 'game_calls_test.cpp', 'signature_exe_test.cpp', 'country_cache_test.cpp', 'editor_hotpath_test.cpp', 'bench\hotpath_bench.cpp' |
        ForEach-Object { Join-Path $script:TestDir $_ })
$tidyFiles += @('public_names_test.cpp', 'public_keys_test.cpp', 'public_editor_test.cpp') | ForEach-Object { Join-Path $script:TestDir $_ }
$tidyFiles += @('name_layer_test.cpp', 'name_layer_pins_exe_test.cpp', 'flag_layer_test.cpp', 'flag_layer_pins_exe_test.cpp',
    'host_probe_test.cpp', 'host_pins_exe_test.cpp', 'window_pins_exe_test.cpp', 'rtti_exe_test.cpp', 'portrait_layer_test.cpp', 'portrait_layer_pins_exe_test.cpp',
    'colour_layer_test.cpp', 'colour_layer_pins_exe_test.cpp') |
    ForEach-Object { Join-Path $script:TestDir $_ }
$database = foreach ($file in $tidyFiles) {
    [ordered]@{
        directory = $script:OutDir
        file      = $file
        command   = ('clang-cl /std:c++20 /EHsc /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DCAPIE_DEBUG_DIAGNOSTICS=1 "' +
            $script:MinHookInclude + '" "/I' + $script:DebugDir + '" -c "' + $file + '"')
    }
}
[IO.File]::WriteAllText((Join-Path $script:OutDir 'compile_commands.json'), (ConvertTo-Json @($database) -Depth 3), [Text.UTF8Encoding]::new($false))

# 3. Offline tests. First the vendored files of other repositories (hashes in fixtures/external/lock.json).
& $Python -I (Join-Path $script:TestDir 'check_external_fixtures.py')
if ($LASTEXITCODE -ne 0) { throw 'A vendored file differs from tests/fixtures/external/lock.json' }
Write-Host 'Offline tests:'
$exeArgs = if (Test-Path -LiteralPath $GameExe) { @($GameExe) } else { Write-Host '  (game EXE not found: static EXE checks skipped)'; @() }
Invoke-Test 'legacy\protocol_test.cpp' 'protocol_test.exe' @('/UNDEBUG') | Out-Null
Invoke-Test 'legacy\editor_input_test.cpp' 'editor_input_test.exe' | Out-Null
Invoke-Test 'legacy\trace_ring_test.cpp' 'trace_ring_test.exe' | Out-Null
Invoke-Test 'legacy\caption_refresh_test.cpp' 'caption_refresh_test.exe' @() $exeArgs | Out-Null
Invoke-Test 'legacy\focus_release_test.cpp' 'focus_release_test.exe' @() $exeArgs | Out-Null
Invoke-Test 'legacy\country_status_test.cpp' 'country_status_test.exe' @() @() $script:MinHookObjects | Out-Null
Invoke-Test 'memory_test.cpp' 'memory_test.exe' | Out-Null
# Game-call fault latch of both DLLs (scan H1, H3): a fault inside a game call stops every later one.
Invoke-Test 'game_calls_test.cpp' 'game_calls_test.exe' | Out-Null
Invoke-Test 'signature_exe_test.cpp' 'signature_exe_test.exe' @() $exeArgs | Out-Null
Invoke-Test 'country_cache_test.cpp' 'country_cache_test.exe' @() @() $script:MinHookObjects | Out-Null
# Country name layer, API level 4 (custom-name-layer report): lookup, decoding and hook bodies, both flavors; pins vs the EXE file.
Invoke-Test -Source 'name_layer_test.cpp' -Output 'name_layer_test_debug.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=1') | Out-Null
Invoke-Test -Source 'name_layer_test.cpp' -Output 'name_layer_test_release.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=0') | Out-Null
Invoke-Test -Source 'name_layer_pins_exe_test.cpp' -Output 'name_layer_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
# Country flag layer, API level 6 (flag-layer design + review K1-K9): packing, cosmetic gate, hooks; pins vs the EXE file.
Invoke-Test -Source 'flag_layer_test.cpp' -Output 'flag_layer_test_debug.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=1') | Out-Null
Invoke-Test -Source 'flag_layer_test.cpp' -Output 'flag_layer_test_release.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=0') | Out-Null
Invoke-Test -Source 'flag_layer_pins_exe_test.cpp' -Output 'flag_layer_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
Invoke-Test -Source 'portrait_layer_test.cpp' -Output 'portrait_layer_test_debug.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=1') | Out-Null
Invoke-Test -Source 'portrait_layer_test.cpp' -Output 'portrait_layer_test_release.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=0') | Out-Null
Invoke-Test -Source 'portrait_layer_pins_exe_test.cpp' -Output 'portrait_layer_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
# Country colour layer, API level 8 (country-colour-layer-spec): decode, maths, block, slots, getter, repaint; pins vs the EXE file.
Invoke-Test -Source 'colour_layer_test.cpp' -Output 'colour_layer_test_debug.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=1') | Out-Null
Invoke-Test -Source 'colour_layer_test.cpp' -Output 'colour_layer_test_release.exe' -Flags @('/DCAPIE_DEBUG_DIAGNOSTICS=0') | Out-Null
Invoke-Test -Source 'colour_layer_pins_exe_test.cpp' -Output 'colour_layer_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
Invoke-Test -Source 'host_probe_test.cpp' -Output 'host_probe_test.exe' | Out-Null
Invoke-Test -Source 'host_pins_exe_test.cpp' -Output 'host_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
# Scripted-window layout pins (N10): the game's own stores of the window offsets, vs the EXE file.
Invoke-Test -Source 'window_pins_exe_test.cpp' -Output 'window_pins_exe_test.exe' -Arguments $exeArgs | Out-Null
# Every vtable constant belongs to the expected class (MSVC RTTI, core/rtti.hpp).
Invoke-Test -Source 'rtti_exe_test.cpp' -Output 'rtti_exe_test.exe' -Arguments $exeArgs | Out-Null
# The inherited suite loads capie_editor.dll into its own (non-game) process for the wrong-host check.
Copy-Item -LiteralPath $debugEditor -Destination (Join-Path $script:TestOutDir 'capie_editor.dll') -Force
Invoke-Test 'legacy\editor_inherited_test.cpp' 'editor_inherited_test.exe' @('/UNDEBUG', '/DCAPIE_DEBUG_DIAGNOSTICS=1') @() $script:MinHookObjects | Out-Null
Invoke-Test 'legacy\editor_integration_test.cpp' 'editor_integration_test_debug.exe' @('/UNDEBUG', '/DCAPIE_DEBUG_DIAGNOSTICS=1') @() $script:MinHookObjects | Out-Null
Invoke-Test 'legacy\editor_integration_test.cpp' 'editor_integration_test_release.exe' @('/UNDEBUG', '/DCAPIE_DEBUG_DIAGNOSTICS=0') @() $script:MinHookObjects | Out-Null
Invoke-Test 'editor_hotpath_test.cpp' 'editor_hotpath_test_debug.exe' @('/DCAPIE_DEBUG_DIAGNOSTICS=1') @($debugEditor) $script:MinHookObjects | Out-Null
Invoke-Test 'editor_hotpath_test.cpp' 'editor_hotpath_test_release.exe' @('/DCAPIE_DEBUG_DIAGNOSTICS=0') @((Join-Path $script:OutDir 'capie_editor.dll')) $script:MinHookObjects | Out-Null
# Public contract, API level 2 (CONTRACT.md): grammar and alphabet, CAPIE_* keys, grammar editors in both flavors.
Invoke-Test -Source 'public_names_test.cpp' -Output 'public_names_test.exe' | Out-Null
Invoke-Test -Source 'public_keys_test.cpp' -Output 'public_keys_test.exe' -Link $script:MinHookObjects | Out-Null
# The demo mod's real .gui is checked against the native layout rule (in-game failure PID45148).
$demoGui = Join-Path $script:ExternalDir 'hoi4-rename-anything\sdk\capie-script-sdk\examples\capie_demo\interface\mydemo.gui'
foreach ($flavor in 1, 0) {
    $name = if ($flavor -eq 1) { 'public_editor_test_debug.exe' } else { 'public_editor_test_release.exe' }
    Invoke-Test -Source 'public_editor_test.cpp' -Output $name -Flags @("/DCAPIE_DEBUG_DIAGNOSTICS=$flavor") -Arguments @($demoGui) `
        -Link $script:MinHookObjects | Out-Null
}

# 4. Signature table equivalence with the accepted sources.
& $Python -I (Join-Path $script:TestDir 'check_signature_table.py')
if ($LASTEXITCODE -ne 0) { throw 'Signature table differs from the accepted sources' }
& $Python -I (Join-Path $script:TestDir 'check_status_localisation.py')
if ($LASTEXITCODE -ne 0) { throw 'CAPIE status localisation contract failed' }
& $Python -I (Join-Path $script:TestDir 'check_alphabet.py')
if ($LASTEXITCODE -ne 0) { throw 'Native alphabet differs from the SDK character table' }
& $Python -I (Join-Path $script:RepoDir 'tools\lint\check_comment_addresses.py')
if ($LASTEXITCODE -ne 0) { throw 'A comment names an address the code does not pin (name the function instead)' }

# 5. Benchmarks (CAPIE release/debug). The comparisons with the old rev7 bridge and country-status
# sources (CAPIE_BENCH_LEGACY=1 in hotpath_bench.cpp) are not run: those sources target HOI4 1.19.3.0
# and no longer compile against the 1.19.4.0 fixture, and the country one is a staged copy that only
# country-status-candidate\build.ps1 writes. The numbers from before the port are in the git history.
if (-not $SkipBenchmarks) {
    Write-Host 'Benchmarks (ns per call, median of runs):'
    Write-Host '  (legacy rev7/country-status comparisons skipped: their sources target HOI4 1.19.3.0)'
    Invoke-Test 'bench\hotpath_bench.cpp' 'bench_editor_release.exe' @('/DCAPIE_BENCH_LEGACY=0', '/DCAPIE_DEBUG_DIAGNOSTICS=0') @() $script:MinHookObjects | Out-Null
    Invoke-Test 'bench\hotpath_bench.cpp' 'bench_editor_debug.exe' @('/DCAPIE_BENCH_LEGACY=0', '/DCAPIE_DEBUG_DIAGNOSTICS=1') @() $script:MinHookObjects | Out-Null
    Invoke-Test 'bench\hotpath_bench.cpp' 'bench_country_capie.exe' @('/DCAPIE_BENCH_PART=1', '/DCAPIE_BENCH_LEGACY=0') @() $script:MinHookObjects | Out-Null
}
if ($Console) {
    & (Join-Path $script:TestDir 'console\run_console_tests.ps1') -VisualStudioDir $VisualStudioDir -OutputDirectory (Join-Path $OutputDirectory 'console-tests')
    Write-Host 'CONSOLE DEVELOPER BUILD (CAPIE_CONSOLE_CANDIDATE=1): not for a release or a launcher package.'
}
Write-Host "CAPIE build and offline tests passed: $script:OutDir"
