---
description: What works in CAPIE today, what is in progress and what is only planned. Honest status, updated with each test run.
---

# Status

Last updated 2026-10-11. Everything marked <span class="st st-today">Works today</span> was
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
| Public script SDK (variant B, `capie_v1_*`) as its own Workshop item | [Script contract](modders/contract.md) |
| Detection signals `CAPIE_STATUS`, `capie_cap`, `capie_v1_seen` | [Detecting CAPIE](modders/detection.md) |
| "CAPIE missing" indicator and main-menu line | [Indicators](modders/indicators.md) |
| Versioned contract; Rename Anything pins a CAPIE release | [Versions and fallback](modders/versioning.md) |
| Multiplayer level L0: features off and a banner until every player confirms | [Multiplayer](multiplayer.md) |
| Host permissions through a lobby game rule; overlord over a player subject | [Permissions](permissions.md) |
| Signature table, offline scanner, `versions.json` | [Game updates](game-updates.md) |
| Opt-in runtime verification for unknown game builds (all or nothing) | [Safety](safety.md#unknown-builds) |
| Wider alphabets (Cyrillic, accents, umlauts), if the game supports them | [Script contract](modders/contract.md#text-limits) |
| Signed releases and VirusTotal links | [Safety](safety.md#signed-releases) |
| Native script keywords (variant A), maybe | [For modders](modders/index.md) |
| Splitting the repository into CAPIE and Rename Anything | |

## Not supported

| Item | Why |
|---|---|
| Multiplayer | Never tested with two players |
| Any game version other than 1.19.3.0 | CAPIE stays off; scripts keep working |
| Achievements and ironman | Mods that change `common/` change the checksum; ironman not tested |
