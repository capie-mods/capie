# Stability plan for the 1.5.1 release (HOI4 1.19.4.0)

Consolidated from four read-only reviews on 2026-10-27 (native CAPIE, Rename Anything script,
launcher/installer/proxy, tests and process) and the first in-game sessions on 1.19.4.0. Goal:
the game always starts, CAPIE never corrupts game state, every unexpected state turns a feature
off, and the next game update is a routine job. Status: **done**, **next** (before the release),
**later**.

## Blockers for the release

| # | Item | Where | Status |
|---|---|---|---|
| B1 | Commit the 1.19.4.0 port and `tools/port/` (a clean clone fails `check_signature_table.py` without `accepted-1.19.4.0.json`) | capie | next (needs the maintainer's go) |
| B2 | One source of truth for shipped hashes: proxy, launcher bundle, `DEPLOYMENT.md`, installer pin. The shipped 1.5.0 `version.dll` carries stale DLL hashes, so proxy mode never loads CAPIE | `build_proxy.ps1:24-27`, `build_launcher.ps1:57-71`, `DEPLOYMENT.md` | next |
| B3 | Release check that every shipped PE contains `game_exe_sha256` and `capie_release` of `game_build.hpp` (a manifest could claim 1.19.4.0 for 1.19.3.0 binaries) | `tools/release/releasekit/package.py` | next |
| B4 | Installer default release must be 1.5.1 (it still installs 1.5.0, which cannot run on 1.19.4.0) | `tools/bootstrap/.../Configuration.ps1:17-38` | next |
| B5 | Multiplayer: CAPIE Applies have no lock and MP was never tested (desync if temp records do not reach other clients) | `rn_capie_v1.txt` header, legacy guards | decision needed: lock CAPIE Applies in MP until a two-machine test |

## Native CAPIE

| # | Item | Status |
|---|---|---|
| N1 | Fault latch: the first access violation inside a game call stops every later game call (`engine::game_call_faulted`) | done |
| N2 | `guarded_find` reads the result's vtable through the guarded copy; null checks on every game-call wrapper | done |
| N3 | `capie_core` init: once-guard (no concurrent init), refuses a renamed copy (40), pins its module (14) | done |
| N4 | Prove on 1.19.4.0 that the effect constructor deep-copies the record vector (H2), and the portrait entry layout (H3) | done: both confirmed in Ghidra (port notes, section Ghidra verification) |
| N10 | Refuse a window root equal to its parent (the factory returns the parent on failure); pin the `+0xBF9` and `+0xE8` stores | next |
| N11 | Hook SetVariable through its only vtable slot instead of an inline detour | later |
| N5 | Runtime RTTI check of every vtable constant (class name via the complete object locator) at init; refuse the dependent feature on mismatch; pin the `+0xBF9` read | done: `core/rtti.hpp`, editor refuses (23), core keeps the portrait layer off; `rtti_exe_test` 30/30 on 1.19.4.0 confirms every inferred vtable (model, root, edit definition). `+0xBF9` pin: waiting for the Ghidra review |
| N6 | Hooks are `noexcept`: a C++ exception from the original function aborts the process. Remove `noexcept` from hook entry points, keep try/catch around CAPIE's own work | later: the engine barely uses C++ exceptions (out-of-memory, which ends the game anyway) and the change spans every hook; do it with the hook-table refactor (readability plan step 6) |
| N7 | Flag layer: synchronise `seen_`; gate or drop the second `LoadCountryFlag` call | later |
| N8 | Document and thread-check the `UpdateNames` call from `SetVariable::Execute` | later |
| N9 | Log cap (rotate above ~4 MB), cache caps, clear pointer-keyed caches when the game state changes | later |

## Rename Anything script

| # | Item | Status |
|---|---|---|
| S1 | Portrait texture paths with spaces are cut (`gfx/leaders/South America/...` → `gfx/leaders/South`): 69 broken sprites; widen `check.py` to every mod `.gfx`. Also: base sprites whose file ships only with a DLC now belong to that DLC's pack (55), a game sprite with a wrong path is skipped (1) | done |
| S2 | Subject country Apply writes the name layer with CAPIE API < 4; add the `capie_cap > 3` gate (error 15) | done |
| S3 | Main-window country Reset runs without a custom name and without cooldown (double news event); guard with `rn_cn_has_custom` | done |
| S4 | Mini portraits: image property does not resolve through the typed row variable (in-game result); use a numeric overlay grid (`rn_chars_pp`, `GFX_rnchp_<value>`, value 0 = unknown portrait). DLC textures left out | done, needs in-game look |
| S5 | Rename the portrait picker's temp loop index `rn_pp_i` (collides with the character variable): the character variables are now `rn_cp_*` | done |
| S6 | `rn_is_host` is set or cleared on every Apply (no stale host rights); the `log =` lines stay on purpose (the only trace for player reports) | done |
| S7 | Rebuild the Characters page in the daily refresh while the tab is open (dead/transferred characters) | later |
| S8 | City names stored by slot: also store the province id | later |
| S9 | Close every other picker in each opener (two editors at once) | later |

## Launcher, installer, proxy

| # | Item | Status |
|---|---|---|
| L1 | The launcher always starts the game: any refusal or exception before launch (also error 740, unknown launcher name) starts Steam's command unchanged with CAPIE off and a notice | next |
| L2 | Uninstall never deletes the bundle while a Steam launch option may point at it; switch the `current` junction without a gap | next |
| L3 | Installer refuses while the game or the Paradox Launcher runs; cleanup failures are logged, not thrown; sweep own leftovers (`.tmp`, `.bak`) after a hash check | next |
| L4 | Open `hoi4.exe` with `FILE_SHARE_DELETE` (or release the pin after injection) so Steam can update the game | next |
| L5 | Notices topmost and in front; a status file; actionable "unsupported build" text | next |
| L6 | Crash-loop breaker: a sentinel before injection / `RNInitialize`; skip CAPIE once if it is still there | later |

## Process

| # | Item | Status |
|---|---|---|
| P1 | Break the cross-repo cycle (capie reads mod files, the mod reads capie files): one-way dependency, a generated contract JSON, `vendor/` with a hash lock; no junctions | later |
| P2 | CI on Windows for both repos (lint, pytest including the `check_*` scripts, `build.ps1 -SkipBenchmarks`, mutation); exe-dependent jobs on a self-hosted runner | later |
| P3 | Test for the every-update path (`unsupported_executable` and its status text); generator equality test including char portraits and orphan files | next |
| P4 | Vanilla fingerprint of every game file the mod reads; scheduled triage on a new `hoi4.exe` | later |
| P5 | Archive per game build: old `hoi4.exe`, triage, map, accepted table, reviewed DLLs | later |
| P6 | In-game checklist per build (`docs/contributing/port-checklist.md`) with screenshots | next |
