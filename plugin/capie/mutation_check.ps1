<#
.SYNOPSIS
    Mutation check of the CAPIE caption core, focus release and editor bridge (78 mutants, ported from rev7).

.DESCRIPTION
    Reads tests/mutants.json: each mutant is one exact, unique textual replacement in one
    source file plus the suite that must kill it (caption core suite, focus suite, or the
    editor integration suite). For every mutant the src/ and tests/ trees are copied to
    plugin/build/capie/mutants/<name>, the replacement is applied, the suite is compiled
    from the copy and run WITHOUT the EXE argument (no file or process access). A mutant is
    killed by a compile error, failing checks, a crash or the timeout. Any survivor fails
    the script. Never touches a game process.

.PARAMETER MinHookDir
    MinHook source folder.

.PARAMETER VisualStudioDir
    Visual Studio folder; empty means "find it with vswhere".

.PARAMETER TimeoutSeconds
    Per-mutant run timeout.

.PARAMETER Only
    Optional mutant name filter (wildcards).

.OUTPUTS
    System.String. One line per mutant and a summary line.

.EXAMPLE
    .\plugin\capie\mutation_check.ps1
#>
[CmdletBinding()]
[OutputType([string])]
param(
    [string]$MinHookDir = 'C:\Users\user\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\Tools\minhook',
    [string]$VisualStudioDir = '',
    [int]$TimeoutSeconds = 60,
    [string]$Only = '*'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$root = Join-Path $repo 'plugin\build\capie\mutants'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$minhookInclude = '/I' + (Join-Path $MinHookDir 'include')
Push-Location $root
try {
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    & cl.exe /nologo /O2 /MT /c /TC @sources $minhookInclude *> minhook-build.log
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
} finally { Pop-Location }
$objects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj') | ForEach-Object { Join-Path $root $_ }
$mutants = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tests\mutants.json') -Raw | ConvertFrom-Json
$survivors = [System.Collections.Generic.List[string]]::new()
$counts = @{}
foreach ($mutant in $mutants) {
    if ($mutant.name -notlike $Only) { continue }
    $dir = Join-Path $root $mutant.name
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $dir -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests') -Destination $dir -Recurse
    # Only C++ is needed; Python copies would be picked up by the repository's Python linters.
    Get-ChildItem -LiteralPath (Join-Path $dir 'tests') -Recurse -Include '*.py', '*.json' | Remove-Item -Force
    $target = Join-Path $dir $mutant.file
    $original = [IO.File]::ReadAllText($target).Replace("`r`n", "`n")
    $find = $mutant.find.Replace("`r`n", "`n")
    $count = ([regex]::Matches($original, [regex]::Escape($find))).Count
    if ($count -ne 1) { throw "mutant $($mutant.name): find text matched $count times (must be exactly 1)" }
    [IO.File]::WriteAllText($target, $original.Replace($find, $mutant.replace))
    $suite = Join-Path $dir $mutant.suite
    $exe = Join-Path $dir 'suite.exe'
    $flags = @('/nologo', '/O2', '/MT', '/utf-8', '/std:c++20', '/EHsc', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/UNDEBUG',
        '/DCAPIE_DEBUG_DIAGNOSTICS=1', $minhookInclude)
    Push-Location $dir
    try {
        & cl.exe @flags $suite @objects /link bcrypt.lib "/OUT:$exe" *> build.log
        $counts[$mutant.suite] = 1 + [int]($counts[$mutant.suite])
        if ($LASTEXITCODE -ne 0) { "{0,-36} COMPILE-ERROR (killed)" -f $mutant.name; continue }
        $process = Start-Process -FilePath $exe -NoNewWindow -PassThru -WorkingDirectory $dir `
            -RedirectStandardOutput (Join-Path $dir 'run.log') -RedirectStandardError (Join-Path $dir 'run.err')
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); "{0,-36} TIMEOUT {1}s (killed)" -f $mutant.name, $TimeoutSeconds; continue
        }
        $line = (Get-Content (Join-Path $dir 'run.log') | Select-String ([regex]::Escape($mutant.summary)) | Select-Object -Last 1)
        if ($line -and $line.Line -match '(\d+) checks, (\d+) failures') {
            if ([int]$matches[2] -gt 0) { "{0,-36} killed: {1} checks, {2} failures (exit {3})" -f $mutant.name, $matches[1], $matches[2], $process.ExitCode }
            else { $survivors.Add($mutant.name); "{0,-36} SURVIVED: {1} checks, 0 failures" -f $mutant.name, $matches[1] }
        } else { "{0,-36} killed: crash/no summary (exit {1})" -f $mutant.name, $process.ExitCode }
    } finally { Pop-Location }
}
$total = ($counts.Values | Measure-Object -Sum).Sum
"mutants: $total ($(($counts.GetEnumerator() | ForEach-Object { "$($_.Value) via $([IO.Path]::GetFileName($_.Key))" }) -join ', ')); survivors: $($survivors.Count) $($survivors -join ',')"
if ($survivors.Count) { exit 1 }
