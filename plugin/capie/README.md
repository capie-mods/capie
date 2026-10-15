# CAPIE: Cyprian's API Extender (native code)

Technical notes for people working on the native code. For what CAPIE is, what it gives mods
and players, its status, its safety rules and screenshots, read the
[CAPIE repository README](https://github.com/capie-mods/capie#readme). The contract for mods is
[CONTRACT.md](CONTRACT.md).

CAPIE started as the native part of the HOI4 mod "Rename Anything". Stage 2 rebuilt it from the
accepted binaries `rename_anything_country.dll` (country capability 18841A6D…) and the generated
caption-refresh bridge rev7 (3864532C…), with the same behaviour; see
[monorepo archive: docs/evidence/session-20261011/claude-stage2/report.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/claude-stage2/report.txt). It supports one game build:
`hoi4.exe` 1.19.3.0, SHA-256 `7DC947BE…A031D5`.

For development, CAPIE loads by default through the opt-in `version.dll` proxy. Prepare the
reviewed stage-2 DLLs, build with `build_proxy.ps1`, then read
[proxy loading and acceptance limits](../../docs/contributing/proxy-loader.md). The launcher is
still available. The proxy uses `current/runtime/` and needs the exact `-capie` argument or the
explicit bootstrap config marker; it starts no injection helper process.

## Country leader portrait layer, API level 7 (CAPIE 1.5, developer build)

`CONTRACT.md` section 18, design and review in [monorepo archive: docs/evidence/session-20261014/portrait-layer/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261014/portrait-layer).
Built into `plugin/build/capie-v15-dev`.

- `capie_core.dll` adds one hook: `ResolvePortrait` (139DF20), the function every GUI portrait
  icon uses (only caller B44520). For the civilian portrait of a ruling country leader with a
  layer (same object, same id), it builds the chosen sprite's entry, checks it with the game's
  validity function (139D1E0, the sprite exists) and only then fills the caller's buffer; a
  missing sprite (for example a DLC the client lacks) shows the game's portrait. Army and navy
  portraits are never changed. Game data are never written.
- The cache holds each country's ruling leader (game chain: politics +0xF90, ruling party
  +0xD0, first leader role, role +0x8) and is filled on the logic thread from UpdateNames and,
  rebuilt whole, after `set_variable` of `capie_pt_rev`. Logic in
  `features/country/portrait_layer.hpp`, `portrait_layer_hooks.hpp`, pins in
  `portrait_layer_pins.hpp` (2 signatures, 18 instruction pins, 7 call pins; character layout
  as CCharacter::Write/Read use it).
- `RNCountryCapabilities` returns a v4 value (`0x524E4402`, `0x524E4403` with the flag layer);
  grammar Apply carries `capie_portrait` (1 or 0), 181 records at most.
- Console (`-Console`): `change_leader_portrait <TAG> <sprite>`, `reset_leader_portrait <TAG>`
  (sprites checked in the game's sprite database).
- Version 1.5.0, API level 7, build 10500.
- Tests: `portrait_layer_test.cpp` (both flavors), `portrait_layer_pins_exe_test.cpp`; 12 more
  mutants, 4 more console mutants.

## Host probe, debug flavor only (log, no record)

`features/host/host_probe.hpp` (read-only chain: application, session, backend, human list) and
`features/host/host_pins.hpp` (5 signatures, 11 instruction pins, 1 RIP pin, 2 vtable slot pins),
design in [monorepo archive: docs/evidence/session-20261014/host-detect/report.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261014/host-detect/report.txt). Only the debug flavor of
`capie_editor.dll` checks the pins at start (`CAPIE_HOST probe on` or `off; pin <id>`) and logs one
line per grammar Apply: `CAPIE_HOST probe would_be_host=yes|no|unreadable mode server backend
machine humans own_entry own_is_host my_len host_len names=equal|different|unreadable record=none`.
No player names in the log, no `capie_host` record; the release flavor has no probe. Tests:
`host_probe_test.cpp`, `host_pins_exe_test.cpp`, 11 mutants.

## Country flag layer, API level 6 (CAPIE 1.4, developer build)

`CONTRACT.md` section 17, design and review in [monorepo archive: docs/evidence/session-20261014/flag-layer/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261014/flag-layer).
Built into `plugin/build/capie-v14-dev`.

- `capie_core.dll` adds 3 hooks on the flag atlas path: `UpdateFlagAtlas` (123FE20) forces a
  reload of changed slots (slot key = an impossible cosmetic id), `LoadCountryFlag` (123F7E0)
  loads a layer's file with the game's own loader and falls back to the game's flag when that
  file does not load, `BuildFlagCandidates` (147D990) reduces the candidates to `<stem>.tga`
  only inside that load for the same country. Logic in `features/country/flag_layer.hpp`
  (packing, snapshot gate), `flag_layer_hooks.hpp` (cache, thread-local frames, shared lock only
  on the TBB rebuild path) and `flag_layer_pins.hpp` (3 signatures, 7 instruction pins, 3 call
  pins). The flag layer needs the name-layer pins as well.
- `RNCountryCapabilities` returns v3 (`0x524E4303`) with the flag layer; grammar Apply carries
  `capie_flag` (1 or 0).
- Console (`-Console`): `change_nation_flag <TAG> <file>`, `reset_nation_flag <TAG>`.
- Version 1.4.0, API level 6, build 10400.
- Tests: `flag_layer_test.cpp` (both flavors), `flag_layer_pins_exe_test.cpp`; 12 more mutants,
  4 more console mutants.

## Faction name layer and opening text, API level 5 (CAPIE 1.3, developer build)

`CONTRACT.md` sections 5.1, 15 and 16. Built into `plugin/build/capie-v13-dev`.

- `capie_core.dll` adds a 7th hook, `CFaction::GetName` (D8BB80): the faction leader's
  `capie_fn_n` / `capie_fn_ver` (commit `capie_fn_rev`) replace the faction name on screen; the
  stored name stays. The text is cached on the logic thread (UpdateNames, `capie_fn_rev`
  commit); the hook reads only the cache (one atomic load when no faction layer exists). Pins:
  +2 signatures (GetName, the game's string copy 11E010), +2 instruction pins (members, count),
  +1 rel32 pin.
- `capie_editor.dll`: grammar Apply carries `capie_pf1_*` (35 records), the text of prefill
  carrier 1 at the opening; the largest Apply is 179 records.
- No `capie_host` record: the host flag is not established statically (`CONTRACT.md` 16).
- Version 1.3.0, API level 5, build 10300.
- Tests: `name_layer_test.cpp` (faction reader and hook), `name_layer_pins_exe_test.cpp`
  (+5 corrupted-byte cases), `public_editor_test.cpp` (`capie_pf1_*`); 8 more mutants.

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

Evidence and design: [monorepo archive: docs/evidence/session-20261013/custom-name-layer/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261013/custom-name-layer).

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
.\plugin\capie\mutation_check.ps1                           # 155 mutants (bridge and public-editor ones in debug and release)
.\plugin\capie\mutation_check.ps1 -MutantsFile tests\console\mutants_console.json   # 45 console mutants
# mutation_check.ps1 takes -MinHookDir or the CAPIE_MINHOOK_DIR environment variable (folder with include\MinHook.h)
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
older creation times), checks the playset the game was started with
(`Documents\Paradox Interactive\Hearts of Iron IV\dlc_load.json`, `playset_gate.hpp`): CAPIE
loads only when the "CAPIE" switch mod (`toggle-mod/`, identity `mod/capie.mod`, later its
Workshop ID in `config.hpp`) is enabled; disabled means vanilla without a notice, an unreadable
or invalid file means vanilla with one non-blocking notice. Then it waits for the main menu (responsive window + two
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
