# CAPIE 1.0: Cyprian's API Extender (native code)

Native part of the HOI4 mod "Rename Anything", reorganised in stage 2 from the accepted
binaries `rename_anything_country.dll` (country capability 18841A6D…) and the generated
caption-refresh bridge rev7 (3864532C…). Behaviour is equivalent; see
`docs/evidence/session-20261011/claude-stage2/report.txt`. Supported game: `hoi4.exe`
1.19.3.0, SHA-256 `7DC947BE…A031D5`, nothing else.

The default development loading path is the opt-in `version.dll` proxy. Build with
`build_proxy.ps1` after preparing the reviewed stage-2 DLLs, then see
[proxy loading and acceptance limits](../../docs/proxy-loader.md). The existing launcher
remains available. The proxy uses `current/runtime/` and requires the exact `-capie`
argument or the explicit bootstrap config marker; it creates no injection helper process.

## Country name layer, API level 4 (CAPIE 1.2, developer build)

`CONTRACT.md` section 14. A name a player gives a country lives in the country's own variables
(`capie_cn_n`, `capie_cn_a`, `capie_cn_ver`, commit `capie_cn_rev`), not in a cosmetic tag, so
flag and colour stay the game's. On screen: layer > current cosmetic tag > base name.

- `capie_core.dll` adds 3 hooks: `CCountry_UpdateNames` (716580), `CCountry_BuildNameKey`
  (6ECFF0), `CSetVariableEffect::Execute` (364C60). Logic in
  `features/country/name_layer.hpp` (variable lookup and decoding, fail-closed),
  `name_layer_hooks.hpp` (hook bodies, refresh after `capie_cn_rev`) and `name_layer_pins.hpp`
  (5 signatures, 10 instruction pins, 5 RIP pins). A failed pin turns the layer off and keeps
  the localisation hooks. CAPIE writes no game state. New log `capie_core.log` (counts only,
  never a name).
- `RNCountryCapabilities` returns v2 (`0x524E4302`) with the layer, v1 without; the editor
  bridge requires v2 for a country Apply.
- Console (`-Console` builds): `change_nation_name` / `reset_nation_name` write or clear the
  layer through `eval_effect` and drop only a legacy `RN1|` cosmetic tag.
- Version 1.2.0, API level 4, build number 10200 (`capie_cap = 4` in every command).
- Tests: `name_layer_test.cpp` (both flavors), `name_layer_pins_exe_test.cpp` (pins vs the EXE
  file, with corrupted copies); 13 more mutants in `tests/mutants.json`, 9 new console mutants.

```powershell
.\plugin\capie\build.ps1 -MinHookDir 'C:\path\to\minhook' -OutputDirectory 'plugin\build\capie-v12-dev' -Console
```

Evidence and design: `docs/evidence/session-20261013/custom-name-layer/`.

## Public contract, API level 2 (CAPIE 1.1)

`CONTRACT.md` is the versioned contract for other mods; the script side is
`sdk/capie-script-sdk/`. What 1.1 adds to the binaries, with no new hook and no new pin:

- `capie_editor.dll`: text editors of any mod that names its scripted GUI
  `capie_v1_ed__<mod>__<editor>` (`features/editor/public_names.hpp` parser,
  `public_schema.hpp` records and limits, `public_editor.hpp` registry, opening, prefill,
  Apply transport and sentinels). Up to 4 fields, 32 editors per session, 8 per mod. Records
  `capie_cap`, `capie_ver`, `capie_epoch`, `capie_nf`, `capie_f<n>_*`; every incoming
  `capie_*` record is removed first. Extended 264-code alphabet for these editors only.
- `capie_core.dll`: `CAPIE_STATUS` ("CAPIE 1.1"), `CAPIE_API` ("2"), `CAPIE_BUILD` ("1.1.0");
  `RN_PLUGIN_STATUS*` now reads "CAPIE 1.1 loaded".
- Rename Anything's editors keep their frozen legacy names and records (`CONTRACT.md` 11).

Build 1.1 into its own folder while `plugin/build/capie/` holds the accepted 1.0 binaries that
the launcher, the proxy and `deploy/DEPLOYMENT.md` pin by hash:

```powershell
.\plugin\capie\build.ps1 -MinHookDir 'C:\path\to\minhook' -OutputDirectory 'plugin\build\capie-v11'
uv run pwsh -NoProfile -File plugin/capie/tools/run-clang-tidy.ps1 -BuildDirectory plugin\build\capie-v11
```

New offline tests: `public_names_test.cpp` (grammar, alphabet), `public_keys_test.cpp`
(`CAPIE_*` keys), `public_editor_test.cpp` (grammar editors on the real hook bodies, both
flavors), `check_alphabet.py` (native table equals the SDK table); 20 more mutants.

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
             transport (Apply), input_codes, diagnostics, keylog + debug_observer (debug only),
             public_names / public_schema / public_editor (grammar editors, API level 2)
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
.\plugin\capie\mutation_check.ps1                           # 112 mutants (bridge and public-editor ones in debug and release)
.\plugin\capie\mutation_check.ps1 -MutantsFile tests\console\mutants_console.json   # 38 console mutants
uv run pwsh -NoProfile -File plugin/capie/tools/run-clang-tidy.ps1   # clang-tidy + clang-format (uv dev group)
```

`/MT /W4 /WX /std:c++20 /permissive-` for all CAPIE C++; MinHook (third-party C) with
`/W4` only. Nothing here starts, injects into or reads a game process; the EXE is read
as a file for the static signature checks.

## Launcher (`src/launcher/`, output `plugin/build/capie-launcher/`)

`RenameAnythingLauncher.exe` in wrapper mode: Steam launch option
`"...\RenameAnythingLauncher.exe" %command%`, the bootstrap's shortcut
(`... "<game>\dowser.exe"`), or a double-click (finds the game, starts its Paradox
Launcher). It starts the given `dowser.exe`/`hoi4.exe` unchanged inside a job object,
binds only a `hoi4.exe` from that tree (job membership, or a running parent chain with
older creation times), waits for the main menu (responsive window + two
`Executing History from` lines in this session's game.log + 15 s) and runs
`capie_injector.exe PID ABS_PATH` for `capie_core.dll`, then `capie_editor.dll`.
Unknown game hash or damaged bundle: the game runs without CAPIE and the player gets a
non-blocking notice. It stays alive until the started tree ends and logs to
`%LOCALAPPDATA%\RenameAnything\launcher.log`; no network code. The accepted
`plugin/launcher/` candidate is unchanged and stays the reference.

```powershell
pwsh -File .\plugin\capie\build_launcher.ps1             # pinned CAPIE inputs, build, unit + flow tests (fakes), CLI cases
pwsh -File .\plugin\capie\launcher_mutation_check.ps1    # launcher mutants (tests/launcher/mutants.json)
```
`tools/run-clang-tidy.ps1` also analyses the launcher once `build_launcher.ps1` has written
its `compile_commands.json`.

## Deployment

Nothing in the accepted launcher or bootstrap manifests is changed by this stage. The
replacements needed at deployment are listed in `deploy/DEPLOYMENT.md`.
