param(
    [string]$VisualStudioDir = '',
    [int]$TimeoutSeconds = 60
)
# Reproducible mutation check for caption_refresh_core.hpp. Each mutant is one exact,
# unique textual replacement (applied to a copy under plugin/build/caption-refresh-candidate/
# mutants). The fake-memory suite is run WITHOUT the EXE argument (no file/process access).
# A mutant is "killed" when the suite reports failures, crashes, or exceeds the timeout.
$ErrorActionPreference = 'Stop'
$mutants = [ordered]@{
    'launcher-baselines'      = @('if (!s.seeded) { ++counters_.observations_ignored_unseeded; return Observe::ignored_unseeded; }',
                                  'if (!s.seeded) { s.seeded = true; s.acked = rev; s.observed = rev; return Observe::ignored_unseeded; }')
    'tick-seeds-editor'       = @('result = tracker_.observe(spec.field, rev);',
                                  'if (spec.role == Role::editor && !tracker_.state(spec.field).seeded) { tracker_.seed(spec.field, rev); result = Tracker::Observe::unchanged; } else result = tracker_.observe(spec.field, rev);')
    'seed-overwrites'         = @('observe(f, rev);  // an existing seed', 's.acked = rev; s.observed = rev;  // an existing seed')
    'dispatch-ignores-seed'   = @('const bool ok = s.seeded && s.lower_streak == 0 &&', 'const bool ok = s.lower_streak == 0 &&')
    'dispatch-no-headroom'    = @('rev_now == s.observed && rev_now < max_revision;', 'rev_now == s.observed;')
    'prefill-any-role'        = @('model_specs[own.model].role != Role::editor) {', 'model_specs[own.model].role != Role::editor && false) {')
    'immediate-rollback'      = @('rollback_confirm_reads = 3;', 'rollback_confirm_reads = 1;')
    'plan-during-lower'       = @('if (s.lower_streak) continue;', '')
    'per-frame-retry'         = @('if (s.deferred && s.deferred_edge == edge_[i]) continue;', '')
    'deferred-sticky'         = @('s.deferred = false;                                              // new success', '//                                                               new success')
    'no-suspend'              = @('if (s.suspended && s.suspended_rev >= s.observed) continue;', '')
    'no-inflight-guard'       = @('if (inflight_.valid) return p;  // also the re-entrancy guard', '')
    'no-called-counter'       = @('{ std::lock_guard<std::mutex> lock(mutex_); tracker_.called(plan.serial); }', '')
    'lock-across-call'        = @('const bool returned = env_.call_refresh(before.gui, politics_view_index);',
                                  'bool returned = false; { std::lock_guard<std::mutex> lock(mutex_); returned = env_.call_refresh(before.gui, politics_view_index); }')
    'no-authority-gate'       = @('if (!env_.authority_proven()) {', 'if (false) {')
    'no-dirty-readback'       = @('else if (after.dirty != 1) outcome = Outcome::readback_dirty;', '')
    'no-identity-postcheck'   = @('else if (after.gui != before.gui || after.view != before.view) outcome = Outcome::postcall_identity;', '')
    'already-dirty-unlabelled'= @('before.dirty == 1 ? "ok-already-dirty"', 'before.dirty == 2 ? "ok-already-dirty"')
    'no-parent-root'          = @('if (parent != root) return Reject::parent_root;', '')
    'no-model-vt-resolve'     = @('if (model_vt != image + rva::vt_sg_model) return Reject::model_vt;', '')
    'no-manager-guard'        = @('manager_idler != idler) return Reject::manager_idler;', 'false) return Reject::manager_idler;')
    'visibility-ignores-flags'= @('return desired && (flags & 8) && !(flags & (4 | 16)) ?', 'return desired ?')
    'hidden-drops-pending'    = @('tracker_.visibility(own.model, kind == WindowKind::visible);  // hidden keeps',
                                  'tracker_.visibility(own.model, kind == WindowKind::visible); if (kind != WindowKind::visible) tracker_.invalid(spec.field);  // hidden keeps')
    'invalid-per-frame'       = @('if (transition) tracker_.invalid(spec.field);  // edge-triggered', 'tracker_.invalid(spec.field);  // edge-triggered')
    'bind-keeps-state'        = @('void bind(Field f) noexcept { ++bind_gen_[idx(f)]; reset(f); ++counters_.binds; }', 'void bind(Field f) noexcept { (void)f; ++counters_.binds; }')
    'observe-before-original' = @("original(args...);`n    if (active) observe(args...);", "if (active) observe(args...);`n    original(args...);")
}
if (-not $VisualStudioDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioDir 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (& $env:ComSpec /d /c ('call "' + $vcvars + '" >nul 2>&1 && set'))) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$root = Join-Path $PSScriptRoot '..\..\build\caption-refresh-candidate\mutants'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$root = (Resolve-Path -LiteralPath $root).Path
$original = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'caption_refresh_core.hpp')).Replace("`r`n", "`n")
$survivors = @()
foreach ($name in $mutants.Keys) {
    $find = $mutants[$name][0].Replace("`r`n", "`n"); $replace = $mutants[$name][1]
    $count = ([regex]::Matches($original, [regex]::Escape($find))).Count
    if ($count -ne 1) { throw "mutant ${name}: find text matched $count times (must be exactly 1)" }
    $dir = Join-Path $root $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'caption_refresh_core.hpp'), $original.Replace($find, $replace))
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'caption_refresh_test.cpp') -Destination $dir -Force
    Push-Location $dir
    try {
        & cl.exe /nologo /O2 /MT /std:c++20 /EHsc /DNOMINMAX caption_refresh_test.cpp *> build.log
        if ($LASTEXITCODE -ne 0) { "{0,-26} COMPILE-ERROR (killed)" -f $name; continue }
        $process = Start-Process -FilePath (Join-Path $dir 'caption_refresh_test.exe') -NoNewWindow -PassThru `
            -RedirectStandardOutput (Join-Path $dir 'run.log') -RedirectStandardError (Join-Path $dir 'run.err')
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); "{0,-26} TIMEOUT {1}s (killed)" -f $name, $TimeoutSeconds; continue
        }
        $summary = (Get-Content (Join-Path $dir 'run.log') | Select-String 'caption refresh tests:' | Select-Object -Last 1)
        if ($summary -and $summary.Line -match '(\d+) checks, (\d+) failures') {
            if ([int]$matches[2] -gt 0) { "{0,-26} killed: {1} checks, {2} failures (exit {3})" -f $name, $matches[1], $matches[2], $process.ExitCode }
            else { "{0,-26} SURVIVED: {1} checks, 0 failures" -f $name, $matches[1]; $survivors += $name }
        } else { "{0,-26} killed: crash/no summary (exit {1})" -f $name, $process.ExitCode }
    } finally { Pop-Location }
}
"mutants: $($mutants.Count); survivors: $($survivors.Count) $($survivors -join ',')"
if ($survivors.Count) { exit 1 }
