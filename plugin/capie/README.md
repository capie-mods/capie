# CAPIE 1.0: Cyprian's API Extender (native code)

Native part of the HOI4 mod "Rename Anything", reorganised in stage 2 from the accepted
binaries `rename_anything_country.dll` (country capability 18841A6D…) and the generated
caption-refresh bridge rev7 (3864532C…). Behaviour is equivalent; see
`docs/evidence/session-20261011/claude-stage2/report.txt`. Supported game: `hoi4.exe`
1.19.3.0, SHA-256 `7DC947BE…A031D5`, nothing else.

## Binaries (output `plugin/build/capie/`, debug flavor in `debug/`)

| File | Role | Replaces |
| --- | --- | --- |
| `capie_core.dll` | RN1 country names, `RN_PLUGIN_STATUS*` text ("CAPIE 1.0 loaded"), `RNCountryCapabilities` | `rename_anything_country.dll` |
| `capie_editor.dll` | inline editors: prefill, Apply transport, caption refresh, focus release | `rename_anything_caption_refresh_bridge.dll` |
| `capie_injector.exe` | `PID ABSOLUTE_DLL_PATH`, accepts exactly the two DLL names above | `rename_anything_injector.exe`, `inject_caption_refresh_bridge.exe` |
| `debug/capie_debug_control.exe` | development-only `snapshot/dump/pause/resume`, pinned to `debug/capie_editor.dll` | `inline_debug_control.exe` |

Two DLLs stay two DLLs in 1.0. A single `capie.dll` would make initialization all or
nothing (country names would also stop when an editor signature fails) and would remove
the cross-module capability query; both are behaviour changes, so the merge is proposed
for a later stage (report, OPEN). Load order stays core, then editor.

Flavors: **release** writes only error and lifecycle lines, installs 4 hooks (Tick,
Bind, Dispatch, effect constructor) and exports no `RNDebugRequest`. **debug**
(`CAPIE_DEBUG_DIAGNOSTICS=1`) is the rev7 configuration: 7 hooks (plus key handler,
key wrapper, Execute), key log, ring, remote requests, typed text in `RN_INLINE` lines.

## Layout (phase A design)

```
src/core/      what the pinned build is and the generic machinery
  game_build.hpp       SHA, version, DLL/log names
  game_layout.hpp      every RVA, offset, flag bit and token, named
  signatures.hpp       Signature/SlotPin/TokenPin/RipPin types, masked unique-at-RVA matcher
  signature_table.hpp  all 47 signatures + 9 pins as data
  scanner.hpp          inactive skeleton of the decision-10(a) runtime scanner (never called)
  memory.hpp           bounded, SEH-guarded reads (VirtualQuery per read)
  pe_image.hpp         bounded .text locator
  sha256.hpp           CNG file hashing
  hook_set.hpp         MinHook create/enable (all-at-once and ordered variants)
  log.hpp, trace_ring.hpp  log channels, file writer, debug ring and request mailbox
  init_codes.hpp       exit codes 10..99 (unchanged numbers)
src/engine/    adapters: how CAPIE talks to the game
  msvc_string.hpp      MSVC std::string layout, strict and byte comparisons
  game_calls.hpp       the six called game functions, SEH-guarded
  widgets.hpp          edit boxes, carriers, child lists, UTF-8 decoding
  records.hpp          temp-variable record layout
src/features/  behaviour
  country/   protocol (RN1 decoding), country_names (hooks, status, key cache), capability
  editor/    schema (all names), window_view (+ per-frame pre-filter), lifecycle, prefill,
             transport (Apply), input_codes, diagnostics, keylog + debug_observer (debug only)
  caption/   caption_refresh (core, templated over memory), caption_adapter (wiring)
  focus/     focus_release (core), focus_adapter (wiring)
src/dll/       composition roots: capie_core.cpp, capie_editor.cpp (hook bodies, init, exports)
src/tools/     capie_injector.cpp, capie_debug_control.cpp
tests/         legacy/ (accepted suites, include lines changed only), compat/ (old-name shims),
               new tests, bench/, mutants.json, check_signature_table.py
```

Dependency rule: `features -> engine -> core`, `dll -> everything`; features do not
include each other except the shared `editor/schema.hpp` (one place for every mod name)
and the caption/focus adapters that the editor composition root wires in.

Why these abstractions, and no more:
- **Signature table as data** (`Signature` + group tables): one matcher replaces three
  copies (country wildcard matcher, bridge `unique_at`, caption hex matcher); the same
  rows are what a future scanner needs, and a test can compare them with the accepted
  sources.
- **Memory policy template** (`rd<T>(mem, ...)`, `ProcessMemory`): already used by the
  accepted caption/focus cores so fake-memory tests run the production guard chains;
  kept, and reused for the EXE-file pin checks.
- **Env adapters** for caption/focus: the cores stay free of Windows and engine globals.
- **HookSet**: both DLLs had the same create/rollback sequence; the two activation
  variants are kept because they differ observably.
- **Log channels**: one switch decides what the release build writes.
Not introduced: an object for the editor state (hooks are free functions and the state
is a process singleton; the accepted tests address it directly), virtual interfaces
(nothing has two implementations at run time), a scanner implementation.

## Build and test

```powershell
.\plugin\capie\build.ps1 -MinHookDir 'C:\path\to\minhook'   # both flavors, all offline tests, benchmarks
.\plugin\capie\mutation_check.ps1                           # 79 mutants (bridge ones in debug and release)
uv run pwsh -NoProfile -File plugin/capie/tools/run-clang-tidy.ps1   # clang-tidy + clang-format (uv dev group)
```

`/MT /W4 /WX /std:c++20 /permissive-` for all CAPIE C++; MinHook (third-party C) with
`/W4` only. Nothing here starts, injects into or reads a game process; the EXE is read
as a file for the static signature checks.

## Deployment

Nothing in the accepted launcher or bootstrap manifests is changed by this stage. The
replacements needed at deployment are listed in `deploy/DEPLOYMENT.md`.
