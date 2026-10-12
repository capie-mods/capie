---
description: What works in CAPIE today, what is in progress and what is only planned. Honest status, updated with each test run.
---

# Status

Last updated 2026-10-12. Everything marked <span class="st st-today">Works today</span> was
checked in the game on HOI4 1.19.3.0 (Windows, Steam), single player, with development builds.
The repository's `docs/test-report.md` and run notes hold the evidence and the limits of each
test.

## Works today

| Item | Notes |
|---|---|
| Start-up checks: game hash, unique patterns at expected addresses, all or nothing | Unknown builds and mismatches leave CAPIE off |
| Country name and adjective from the `RN1` cosmetic tag | Map, diplomacy, tooltips and events pick up the name; decision titles keep their own text |
| Native text field to game command | Text typed into the game's own field reaches the script inside the player's command |
| Polish letters through the transport | Checked on one keyboard layout |
| 32-character limit | Longer input is rejected by the transport, not cut |
| Enter applies | Through the game's own `shortcut = "RETURN"`; no native code needed |
| Names survive save, load and a full restart | Country, leader, parties, states, cities, characters |
| Without CAPIE loaded, the country shows its original name | Checked with the mod enabled; other names unaffected |
| Status localisation key overridden at draw time | `RN_PLUGIN_STATUS` reads "ACTIVE" with CAPIE, "INACTIVE" without |
| No network imports | Current builds import only `KERNEL32` and `bcrypt` |
| Local Windows Defender scan of current candidate builds | "Found no threats"; one engine, not VirusTotal |

## Built for CAPIE 1.1, needs an in-game test

CAPIE 1.1 (API level 2) and the script SDK are built and pass their offline tests (unit,
integration on fake game memory, mutation checks, static checks of the generated script). None
of it has run in the game.

| Item | Page |
|---|---|
| Text editors for any mod through the name grammar `capie_v1_ed__<mod>__<editor>` (up to 4 fields, 32 editors per session) | [Script contract](modders/contract.md#editor-names) |
| Prefill of grammar editors from carriers, once per opening, never over a focused field | [Building an editor](modders/editors.md) |
| `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD` localisation keys | [Detecting CAPIE](modders/detection.md) |
| Wider alphabet: 8 punctuation marks, Latin-1 and Latin Extended-A letters (264 codes) | [Script contract](modders/contract.md#alphabet) |
| SDK text tools in plain script: trim, validation, four text slots, setters | [Text tools](modders/text-tools.md) |
| Multiplayer version check and CAPIE check window | [Multiplayer](multiplayer.md#version-check) |
| Permission triggers and the lobby rule `capie_v1_edit_rights` | [Permissions](permissions.md) |
| Demo mod with two editors | [Building an editor](modders/editors.md) |

## In progress

| Item | Where it stands |
|---|---|
| Leader name refreshes in the open panel right after Apply | The refresh bridge loads, but a safety check on the panel's type fails at run time, so it never calls the refresh (fail closed). Today the name changes after you close and reopen the panel |
| Esc while editing | Closing the Politics panel hides the editor, but the edit mode stays on; after reopening the panel, the editor's actions can be dropped without a message until you cancel and open it again |
| CAPIE launcher with Steam launch option or shortcut | Launcher built and reviewed; wrapper mode for Steam and shortcuts not finished; not tested end to end |
| One-line PowerShell installer, updates, uninstall | Script and offline tests exist; release address, package hash and signing key are placeholders |
| Renaming a subject's names as overlord | Slot exists in Rename Anything, hidden |
| Binary rename to `capie_*.dll` | Decided as a separate step; current files keep legacy `rename_anything_*` names |

## Planned

| Item | Page |
|---|---|
| Publishing the script SDK as its own Workshop item and repository | [For modders](modders/index.md) |
| Main-menu line "CAPIE loaded" or "CAPIE missing" | [Indicators](modders/indicators.md#main-menu) |
| Rename Anything on the public editor names; Rename Anything pins a CAPIE release | [Versions and fallback](modders/versioning.md) |
| Focus on open, select-all, typing cap and Escape for grammar editors | [Building an editor](modders/editors.md#what-is-not-there-yet) |
| Signature table, offline scanner, `versions.json` | [Game updates](game-updates.md) |
| Opt-in runtime verification for unknown game builds (all or nothing) | [Safety](safety.md#unknown-builds) |
| Cyrillic, which depends on the game language's fonts | [Script contract](modders/contract.md#alphabet) |
| Signed releases and VirusTotal links | [Safety](safety.md#signed-releases) |
| Native script keywords (variant A), maybe | [For modders](modders/index.md) |
| Splitting the repository into CAPIE and Rename Anything | |

## Not supported

| Item | Why |
|---|---|
| Multiplayer | Never tested with two players |
| Any game version other than 1.19.3.0 | CAPIE stays off; scripts keep working |
| Achievements and ironman | Mods that change `common/` change the checksum; ironman not tested |
