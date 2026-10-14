---
description: What works in CAPIE today, what is in progress and what is only planned. Honest status, updated with each test run.
---

# Status

Last updated 2026-10-14. Everything marked <span class="st st-today">Works today</span> was
checked in the game on HOI4 1.19.3.0 (Windows, Steam), single player, with development builds.
Nothing is released. The repository's `docs/contributing/in-game-test-record.md` lists the
evidence and the limits of each check.

<ul class="st-legend">
  <li><span class="st st-today">Works today</span> checked in the game</li>
  <li><span class="st st-progress">In progress</span> built, not yet checked or not finished</li>
  <li><span class="st st-planned">Planned</span> decided, not built</li>
  <li><span class="st st-not">Not supported</span> do not rely on it</li>
</ul>

## Works today

| Item | Notes |
|---|---|
| Start-up checks: game hash, unique patterns at expected addresses | Unknown builds and mismatches leave CAPIE off |
| Two DLLs, `capie_core.dll` and `capie_editor.dll` | Release variant without diagnostic hooks; debug variant with diagnostics. Both checked in the game |
| Country name layer (API level 4) | CAPIE 1.2 developer build. Name and adjective change, flag and colour stay; survives save, restart and load. See [Name layers](modders/name-layers.md) |
| Country names from an `RN1` cosmetic tag | CAPIE 1.0 and 1.1; still read for older saves |
| Native text field to game command | Text typed into the game's own field reaches the script inside the player's command |
| Text editors for any mod (API level 2) | The SDK's demo mod ran on CAPIE 1.1 after a fix |
| Leader caption refresh after Apply | The open Politics panel shows the new name at once |
| Polish letters through the transport | Checked on one keyboard layout |
| Enter applies | Through the game's own `shortcut = "RETURN"`; no native code needed |
| Names survive save, load and a full restart | Country, leader, parties, states, cities, characters |
| Console commands `change_nation_name`, `reset_nation_name`, `capie_cmd` (API level 3) | Developer builds only |
| Status line "CAPIE 1.1 loaded" in Rename Anything | Through the localisation override |
| No network imports | Current builds import only `KERNEL32` and `bcrypt` |
| Local Windows Defender scan of candidate builds | "Found no threats"; one engine, not VirusTotal |

## Built, needs an in-game test

| Item | Page |
|---|---|
| Faction name layer and the `capie_pf1_*` record of the displayed name (API level 5, CAPIE 1.3 developer build) | [Name layers](modders/name-layers.md#faction) |
| Country name layer: story cosmetic tags, a cosmetic tag change after a rename, loading without CAPIE, migration of `RN1` saves | [Name layers](modders/name-layers.md) |
| Prefill of grammar editors from carriers | [Building an editor](modders/editors.md) |
| `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD` localisation keys | [Detecting CAPIE](modders/detection.md) |
| Wider alphabet: 8 punctuation marks, Latin-1 and Latin Extended-A letters (264 codes) | [Script contract](modders/contract.md#alphabet) |
| SDK on CAPIE 1.2 (its country setter now writes the name layer) | [Text tools](modders/text-tools.md) |
| Multiplayer version check and CAPIE check window | [Multiplayer](multiplayer.md#version-check) |
| Permission triggers and the lobby rule `capie_v1_edit_rights` | [Permissions](permissions.md) |
| Proxy loader beyond start-up (editors, saves); passing `-capie` through Steam and the Paradox Launcher | [Install](install.md) |

## In progress

| Item | Where it stands |
|---|---|
| CAPIE launcher | Through Steam's launch option it found the game and loaded CAPIE. Shortcut mode, which starts the Paradox Launcher directly, did not work yet |
| "CAPIE" switch mod in the playset | The launcher loads CAPIE only when this small mod is enabled in the active playset. Being built |
| Installer: one PowerShell line with a terminal UI, and an installer on GitHub Releases | Script and offline tests exist; release address, package hash and signing key are placeholders |
| Esc while editing | Closing the Politics panel hides the editor, but the edit mode stays on until you cancel and open it again |

## Planned

| Item | Page |
|---|---|
| Publishing the script SDK as its own Workshop item and repository | [For modders](modders/index.md) |
| Main-menu line "CAPIE loaded" or "CAPIE missing" | [Indicators](modders/indicators.md#main-menu) |
| Rename Anything on the public editor names | [Versions and fallback](modders/versioning.md) |
| A record that marks the host of a multiplayer session | [Permissions](permissions.md#the-host) |
| Focus on open, select-all, typing cap and Escape for grammar editors | [Building an editor](modders/editors.md#what-is-not-there-yet) |
| Offline scanner and `versions.json` | [Game updates](game-updates.md) |
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
