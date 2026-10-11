param(
    [string]$VisualStudioDir = '',
    [int]$TimeoutSeconds = 60,
    [string]$MinHookDir = 'C:\Users\user\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\Tools\minhook'
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
    'no-called-counter'       = @('tracker_.called(plan.serial); first_success', 'first_success')
    'lock-across-call'        = @('const bool returned = env_.call_refresh(before.gui, politics_view_index);',
                                  'bool returned = false; { std::lock_guard<std::mutex> lock(mutex_); returned = env_.call_refresh(before.gui, politics_view_index); }')
    'no-arm-switch'           = @('if (!env_.refresh_armed()) {', 'if (false) {')
    'no-dirty-readback'       = @('else if (after.dirty != 1) outcome = Outcome::readback_dirty;', '')
    'no-identity-postcheck'   = @('else if (after.gui != before.gui || after.view != before.view) outcome = Outcome::postcall_identity;', '')
    'already-dirty-unlabelled'= @('before.dirty == 1 ? "ok-already-dirty"', 'before.dirty == 2 ? "ok-already-dirty"')
    'no-parent-root'          = @('if (parent != root) return Reject::parent_root;', '')
    'no-model-vt-resolve'     = @('if (model_vt != image + rva::vt_sg_model) return vt_reject(Reject::model_vt, model_vt, image + rva::vt_sg_model);', '')
    'no-gui-word-diag'        = @('if (rd(mem, gui, 0, gui_word0)) diag->gui_word0 = gui_word0;', '')
    'no-gui-b8-diag'          = @('if (rd(mem, gui, gui_structural_offset, gui_b8)) diag->gui_b8_rva = gui_b8 ? gui_b8 - image : 0;', '')
    'gui-word0-gates'         = @('    uptr gui_b8 = 0;', "    if (!rd(mem, gui, 0, gui_word0) || gui_word0 != image + 0x294A688) return Reject::gui_null;`n    uptr gui_b8 = 0;")
    'gui-b8-gates'            = @('    uptr gui_b8 = 0;', "    { uptr b8 = 0; if (!rd(mem, gui, gui_structural_offset, b8) || b8 != image + gui_structural_rva) return Reject::gui_null; }`n    uptr gui_b8 = 0;")
    'N05-dispatch-any-role'   = @('model_specs[own.model].role == Role::editor;', 'true;')
    'N08-max-revision-1000'   = @('max_revision = 999;', 'max_revision = 1000;')
    'verdict-no-cap'          = @('else if (rev >= max_revision) { tracker_.count_dispatch_reject(); verdict = Verdict::cap_reached; }', '')
    'verdict-no-counter-stale' = @('verdict = Verdict::no_counter; }', '}')
    'no-first-success-diag'   = @('if (first_success) diag_line(own.model, "resolve-ok-first", plan.serial, diag);', '')
    'N10-bind-not-clearing-invalid' = @("invalid_[own.model] = kind == WindowKind::invalid;`n            carrier_bad_", "carrier_bad_")
    'no-view-vt-diag'         = @('return vt_reject(Reject::view_vt, view_vt, image + rva::vt_politics_view);', 'return Reject::view_vt;')
    'no-manager-guard'        = @('manager_idler != idler) return Reject::manager_idler;', 'false) return Reject::manager_idler;')
    'visibility-ignores-flags'= @('return desired && (flags & 8) && !(flags & (4 | 16)) ?', 'return desired ?')
    'hidden-drops-pending'    = @('tracker_.visibility(own.model, kind == WindowKind::visible);  // hidden keeps',
                                  'tracker_.visibility(own.model, kind == WindowKind::visible); if (kind != WindowKind::visible) tracker_.invalid(spec.field);  // hidden keeps')
    'invalid-per-frame'       = @('if (transition) tracker_.invalid(spec.field);  // edge-triggered', 'tracker_.invalid(spec.field);  // edge-triggered')
    'bind-keeps-state'        = @('void bind(Field f) noexcept { ++bind_gen_[idx(f)]; reset(f); ++counters_.binds; }', 'void bind(Field f) noexcept { (void)f; ++counters_.binds; }')
    'prefill-no-edge'         = @('{ std::lock_guard<std::mutex> lock(mutex_); tracker_.visibility(own.model, true); }', '')
    'tick-hidden-services'    = @('if (kind != WindowKind::visible) return;', '')
    'observe-before-original' = @("original(args...);`n    if (active) observe(args...);", "if (active) observe(args...);`n    original(args...);")
}

# Bridge-level mutants: applied to a copy of the GENERATED armed bridge and checked with the
# integration suite caption_refresh_bridge_test.cpp (core header taken unmodified via /I).
$bridgeMutants = [ordered]@{
    'bridge-no-dispatch-gate'  = @('const bool caption_ok=caption_code==0;', 'const bool caption_ok=true;')
    'bridge-no-prefill-seed'   = @('    if(prefilled) caption_prefill(window,view.model);', '')
    'bridge-no-caption-tick'   = @("        caption_tick(window);`n", '')
    'bridge-no-caption-bind'   = @("    caption_bind(window);`n", '')
    'bridge-gate-country'      = @('model>1) return 0;  // disarmed/country', 'model>2) return 0;  // disarmed/country')
    'bridge-prefill-country'   = @('|| model>1) return;', '|| model>2) return;')
    'bridge-gate-ignores-arm'  = @('if(!caption_armed.load(std::memory_order_acquire) || model>1) return 0;', 'if(model>1) return 0;')
    'bridge-tick-ignores-arm'  = @('if(caption_armed.load(std::memory_order_acquire)) caption_service.on_tick(', 'if(true) caption_service.on_tick(')
    'bridge-no-focus-tick'     = @("        focus_tick(window);`n", '')
    'bridge-focus-wrong-arg'   = @('focus_switch(manager,nullptr);', 'focus_switch(manager,manager);')
    'bridge-no-reprefill'      = @('if(state.visible) { ++state.generation; state.attempted=false; state.prefilled=false; }', 'if(state.visible) ++state.generation;')
    'R02-hide-clears-attempted-only' = @('if(state.visible) { ++state.generation; state.attempted=false; state.prefilled=false; }', 'if(state.visible) { ++state.generation; state.attempted=false; }')
    'R04-hide-no-generation'   = @('if(state.visible) { ++state.generation; state.attempted=false; state.prefilled=false; }', 'if(state.visible) { state.attempted=false; state.prefilled=false; }')
    'R05-invalid-also-reprefills' = @('state.visible=false; state.attempted=true; state.prefilled=false; ++state.generation;', 'state.visible=false; state.attempted=false; state.prefilled=false; ++state.generation;')
    'B02-prefill-seed-even-if-failed' = @('    if(prefilled) caption_prefill(window,view.model);', '    caption_prefill(window,view.model);')
    'B03-gate-open-on-context-read-fail' = @('if(!member(context,0x10,window)) return 1;                               // fail-closed', 'if(!member(context,0x10,window)) return 0;                               // fail-closed')
    'bridge-no-stale-sentinel' = @("        if(stale_eligible(context,model) && dispatch_stale(current,opaque_selector,caption_ok?1u:caption_code)) return;`n", '')
    'bridge-sentinel-code-ignored' = @('dispatch_stale(current,opaque_selector,caption_ok?1u:caption_code)', 'dispatch_stale(current,opaque_selector,1u)')
    'bridge-stale-epoch-zero'  = @('epoch.raw_value=-static_cast<std::int64_t>(code)*fixed_point;', 'epoch.raw_value=0;')
    'bridge-stale-epoch-fixed' = @('epoch.raw_value=-static_cast<std::int64_t>(code)*fixed_point;', 'epoch.raw_value=-fixed_point;')
    'bridge-stale-keeps-reserved' = @("        if(reserved_key(key)) { ++sanitized; continue; }`n        current.records[kept++]=record;`n    }`n    Record& epoch", "        current.records[kept++]=record;`n    }`n    Record& epoch")
    'bridge-stale-any-window'  = @('inspect_window(window,view)==ViewResult::visible && view.model==model;', '(inspect_window(window,view)!=ViewResult::unrelated || true);')
    'bridge-no-err-reserved'   = @('std::strcmp(key,"rn_native_err")==0 || std::strcmp(key,"rn_nativeb_err")==0) return true;', 'false) return true;')
    'bridge-no-error-slot'     = @('const bool error_slot=i==31 && !encoded.value;', 'const bool error_slot=false;')
}
# Focus-release mutants: applied to focus_release.hpp, checked with focus_release_test.cpp.
$focusMutants = [ordered]@{
    'focus-no-transition'      = @('transition = known_[own.editor] && parent_visible_[own.editor] && !own.parent_visible;', 'transition = !own.parent_visible;')
    'focus-ignore-receiver'    = @('receiver == candidate + off::edit_receiver &&', '')
    'focus-ignore-flag'        = @('&& (flags & 1)) {', ') {')
    'focus-ignore-name'        = @('candidate == focused && edit_named(mem, image, candidate, name) &&', 'candidate == focused &&')
    'focus-ignore-arm'         = @('if (!env_.armed()) return;', '')
    'focus-bit4-visible'       = @('(flags & 8) && !(flags & (4 | 16));', '(flags & 8);')
    'focus-bit16-ignored'      = @('(flags & 8) && !(flags & (4 | 16));', '(flags & 8) && !(flags & 4);')
    'focus-no-hide-log'        = @('root_hide = known_[own.editor] && own_visible_[own.editor] && !own.own_visible;', 'root_hide = false;')
    'focus-hide-source-merge'  = @('if (!own.parent_visible) return "parent-hidden";', '')
    'focus-own-visible-bit16'  = @('out.own_visible = out.desired && (out.root_flags & 8) && !(out.root_flags & (4 | 16));', 'out.own_visible = out.desired && (out.root_flags & 8) && !(out.root_flags & 4);')
    'focus-bind-keeps'         = @("lock(mutex_);`n        known_[own.editor] = false;", "lock(mutex_);`n        (void)own;")
    'focus-invalid-keeps'      = @('if (kind == Kind::invalid) { known_[own.editor] = false; return; }', 'if (kind == Kind::invalid) { return; }')
    'focus-trust-return'       = @('focus_after != edit &&', '(focus_after != edit || returned) &&')
    'focus-no-draft-check'     = @('const bool text_same = text_before && edit_text(mem, edit, after) && same(before, after);', 'const bool text_same = true;')
    'focus-lock-across-call'   = @('const bool returned = env_.call_focus_null(manager);', 'bool returned = false; { std::lock_guard<std::mutex> hold(mutex_); returned = env_.call_focus_null(manager); }')
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
$minhookInclude = Join-Path $MinHookDir 'include'
Push-Location $root
try {
    $sources = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c') | ForEach-Object { Join-Path $MinHookDir ('src\' + $_) }
    & cl.exe /nologo /O2 /MT /c /TC @sources ('/I' + $minhookInclude) *> minhook-build.log
    if ($LASTEXITCODE -ne 0) { throw 'MinHook compilation failed' }
} finally { Pop-Location }
$minhookObjects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj') | ForEach-Object { Join-Path $root $_ }
$survivors = [System.Collections.Generic.List[string]]::new()
function Invoke-Mutant([string]$name, [string]$targetName, [string]$original, [string]$find, [string]$replace,
                       [string]$testName, [string[]]$extraArgs, [string]$summary) {
    $find = $find.Replace("`r`n", "`n")
    $count = ([regex]::Matches($original, [regex]::Escape($find))).Count
    if ($count -ne 1) { throw "mutant ${name}: find text matched $count times (must be exactly 1)" }
    $dir = Join-Path $root $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir $targetName), $original.Replace($find, $replace))
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $testName) -Destination $dir -Force
    Push-Location $dir
    try {
        & cl.exe /nologo /O2 /MT /utf-8 /std:c++20 /EHsc /DNOMINMAX /DWIN32_LEAN_AND_MEAN $testName @extraArgs *> build.log
        if ($LASTEXITCODE -ne 0) { return "{0,-26} COMPILE-ERROR (killed)" -f $name }
        $exe = Join-Path $dir ([IO.Path]::ChangeExtension($testName, '.exe'))
        $process = Start-Process -FilePath $exe -NoNewWindow -PassThru -WorkingDirectory $dir `
            -RedirectStandardOutput (Join-Path $dir 'run.log') -RedirectStandardError (Join-Path $dir 'run.err')
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); return "{0,-26} TIMEOUT {1}s (killed)" -f $name, $TimeoutSeconds
        }
        $line = (Get-Content (Join-Path $dir 'run.log') | Select-String $summary | Select-Object -Last 1)
        if ($line -and $line.Line -match '(\d+) checks, (\d+) failures') {
            if ([int]$matches[2] -gt 0) { return "{0,-26} killed: {1} checks, {2} failures (exit {3})" -f $name, $matches[1], $matches[2], $process.ExitCode }
            $survivors.Add($name)
            return "{0,-26} SURVIVED: {1} checks, 0 failures" -f $name, $matches[1]
        }
        return "{0,-26} killed: crash/no summary (exit {1})" -f $name, $process.ExitCode
    } finally { Pop-Location }
}
$core = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'caption_refresh_core.hpp')).Replace("`r`n", "`n")
foreach ($name in $mutants.Keys) {
    Invoke-Mutant $name 'caption_refresh_core.hpp' $core $mutants[$name][0] $mutants[$name][1] 'caption_refresh_test.cpp' @() 'caption refresh tests:'
}
$focus = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'focus_release.hpp')).Replace("`r`n", "`n")
$focusArgs = @(('/I' + $PSScriptRoot))
foreach ($name in $focusMutants.Keys) {
    Invoke-Mutant $name 'focus_release.hpp' $focus $focusMutants[$name][0] $focusMutants[$name][1] 'focus_release_test.cpp' $focusArgs 'focus release tests:'
}
$bridge = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'caption_refresh_bridge.cpp')).Replace("`r`n", "`n")
$bridgeArgs = @(('/I' + $PSScriptRoot), ('/I' + $minhookInclude)) + $minhookObjects + @('/link', 'bcrypt.lib')
foreach ($name in $bridgeMutants.Keys) {
    Invoke-Mutant $name 'caption_refresh_bridge.cpp' $bridge $bridgeMutants[$name][0] $bridgeMutants[$name][1] 'caption_refresh_bridge_test.cpp' $bridgeArgs 'caption refresh bridge integration:'
}
"core mutants: $($mutants.Count); focus mutants: $($focusMutants.Count); bridge mutants: $($bridgeMutants.Count); survivors: $($survivors.Count) $($survivors -join ',')"
if ($survivors.Count) { exit 1 }
