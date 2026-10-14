# In-game test record

This page records what has been checked in the running game, feature by feature. Contributors use
it to see what still needs a check, and the "What works today" table in the
[README](../../README.md#what-works-today) takes its statuses from here.

## Environment and scope

- Hearts of Iron IV 1.19.3.0, Windows, Steam. `hoi4.exe` SHA-256:
  `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`.
- Single player only. No check has ever run with two players.
- Development builds only: the generated `mod/` (or a QA fixture mod before the editors were
  merged) plus a CAPIE build loaded by hand or by a development loader. Nothing here has been
  published, so no release build has been tested.
- Most runs used the Polish interface at 1720x900. A few early runs used English.

## How to read the table

Each row has one status:

| Status | Meaning |
|---|---|
| Works today | Passed in the game on a development build. The limits column says what that check did not cover. |
| In progress | Code exists, but the in-game check is missing, failed, or covers only part of the feature. The row says what is missing. |
| Planned | Designed or decided, with no working code to test yet. |
| Not supported | Not part of the current scope. Do not rely on it. |

"Last in-game check" gives the date of the most recent run that exercised the feature and the
build used. Build names:

- Pre-CAPIE DLL: the first country name DLL and its QA bridges and fixtures (2026-10-11).
- Caption bridge: pre-CAPIE country DLL plus the caption refresh bridge (2026-10-11).
- CAPIE 1.0 release: first CAPIE release build (2026-10-12).
- CAPIE 1.1: CAPIE 1.1 release build; "console developer build" adds the console commands.
- CAPIE 1.2 developer build: debug build `capie-v12-dev` with the country name layer.

The evidence column links to the run notes. The notes are in Polish and name runs by game
process ID. The old [test report](../evidence/notes/test-report.md) and
[progress log](../evidence/notes/PROGRESS.md) hold the full history of the first day.

## Feature record

| Feature | Status | Last in-game check | Evidence | Limits / not covered |
|---|---|---|---|---|
| Own country name and adjective, cosmetic tag era (RN1) | Works today | 2026-10-12, CAPIE 1.0 release | [test report](../evidence/notes/test-report.md) (PID32656, PID42732), [3rd run](../evidence/qa-20261011-day2/THIRD-RUN-NOTES.md), [12th run](../evidence/qa-20261011-day2/TWELFTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md) | The name was stored as an `RN1` cosmetic tag, which also changed the flag (seen on Canada in the [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md)). Current builds use the CAPIE 1.2 name layer instead; how old `RN1` saves migrate is unchecked (G10). Map, diplomacy, tooltips and the vanilla event `news.350` showed the new name; checked decisions kept their own titles. |
| Own country name and adjective, CAPIE 1.2 name layer | Works today | 2026-10-14, CAPIE 1.2 developer build | [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md) (PID22860, PID34504) | Checked on the United Kingdom (Politics) and Canada: name changed, flag and colour unchanged, kept after save, full restart and load. Not checked yet: a country with a story cosmetic tag, a cosmetic tag or ideology change after Apply, loading the save without CAPIE, migrating an old `RN1` save, decision texts with the layer active, performance. Test list G1 to G14 in the [stage 2 report](../evidence/session-20261013/custom-name-layer/final-report.txt). Debug build only; no release build of 1.2 was run. |
| Own country leader | Works today | 2026-10-13, CAPIE 1.1 | [3rd run](../evidence/qa-20261011-day2/THIRD-RUN-NOTES.md), [12th run](../evidence/qa-20261011-day2/TWELFTH-RUN-NOTES.md), [16th run](../evidence/qa-20261011-day2/SIXTEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md), [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md) | Pencil next to the portrait in Politics. A long name runs under the pencil (open). |
| Party names, ruling party (short and full) | Works today | 2026-10-13, CAPIE 1.1 | [12th run](../evidence/qa-20261011-day2/TWELFTH-RUN-NOTES.md), [16th run](../evidence/qa-20261011-day2/SIXTEENTH-RUN-NOTES.md), [18th run](../evidence/qa-20261011-day2/EIGHTEENTH-RUN-NOTES.md), [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md) | Checked on the neutral (group 4) ruling party of Poland. |
| Party names, all four groups through tabs | In progress | 2026-10-11, caption bridge | [18th run](../evidence/qa-20261011-day2/EIGHTEENTH-RUN-NOTES.md) | Group 4 (ruling) and group 2 (communist tab) applied without error. Groups 1 and 3 have no recorded in-game check. |
| Instant caption refresh (leader caption, party name) | Works today | 2026-10-13, CAPIE 1.1 | [18th run](../evidence/qa-20261011-day2/EIGHTEENTH-RUN-NOTES.md), [19th run](../evidence/qa-20261011-day2/NINETEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md), [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md) | Needs CAPIE. The party name refresh comes from the game itself; the leader caption needs the CAPIE refresh hook. |
| States | Works today | 2026-10-12, CAPIE 1.0 release | [3rd run](../evidence/qa-20261011-day2/THIRD-RUN-NOTES.md), [13th run](../evidence/qa-20261011-day2/THIRTEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md) | Script only, no CAPIE needed. Reset of a state name was not run in game. |
| Cities (victory points) | Works today | 2026-10-11, caption bridge | [1st run](../evidence/qa-20261011-day2/FIRST-RUN-NOTES.md), [2nd run](../evidence/qa-20261011-day2/SECOND-RUN-NOTES.md), [19th run](../evidence/qa-20261011-day2/NINETEENTH-RUN-NOTES.md) | Script only. Map label and province tooltip change; the state and naval base keep their own names. The city list shows "Original (now New name)". Only vanilla 1.19.3 victory points; map mods were not checked. |
| Other characters through the legacy editor | Works today | 2026-10-13, CAPIE 1.1 console developer build | [3rd run](../evidence/qa-20261011-day2/THIRD-RUN-NOTES.md), [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md) | Script only. Checked on a civilian adviser and on the leader through the Characters tab. Generals and admirals have no separate recorded check. |
| Legacy editor on the CAPIE text field | Works today | 2026-10-13, CAPIE 1.1 release (rebuilt) | [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md) (PID40704) | Reported as working in a single run, with no per-target detail. Characters, states, cities and country name were not each listed. |
| Legacy editor, on-screen keyboard fallback without CAPIE | In progress | Not checked | [script and UI report](../evidence/session-20261011/claude-script-ui/report.txt), section "Subjects + legacy on CAPIE" | The keyboard itself works (2026-10-13, [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md)). The automatic switch to the keyboard on the first Apply without CAPIE has not been run. |
| Polish letters | Works today | 2026-10-13, CAPIE 1.1 | [15th run](../evidence/qa-20261011-day2/FIFTEENTH-RUN-NOTES.md), [19th run](../evidence/qa-20261011-day2/NINETEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md), [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md) | Typing with AltGr into the CAPIE field was checked on one keyboard layout. In the legacy on-screen keyboard only the letter buttons work; physical keys for Polish letters do not, by design. Letters outside the Rename Anything alphabet (such as é) are refused. |
| Save, load and full restart | Works today | 2026-10-14, CAPIE 1.2 developer build | [3rd run](../evidence/qa-20261011-day2/THIRD-RUN-NOTES.md), [12th run](../evidence/qa-20261011-day2/TWELFTH-RUN-NOTES.md), [13th run](../evidence/qa-20261011-day2/THIRTEENTH-RUN-NOTES.md), [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md), [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md) | Country, leader, party, state, city and character names survived save, exit, a new process and load. Loading a CAPIE 1.2 save without CAPIE was not checked. An earlier run loaded an `RN1` save without the country DLL and showed the original country name ([4th run](../evidence/qa-20261011-day2/FOURTH-RUN-NOTES.md)). |
| Enter applies | Works today | 2026-10-12, CAPIE 1.0 release | [17th run](../evidence/qa-20261011-day2/SEVENTEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md), [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md) | Uses the same game command as the Apply button, without keyboard hooks. |
| Esc cancels the edit | In progress | 2026-10-11, caption bridge | [17th run](../evidence/qa-20261011-day2/SEVENTEENTH-RUN-NOTES.md), [18th run](../evidence/qa-20261011-day2/EIGHTEENTH-RUN-NOTES.md), [19th run](../evidence/qa-20261011-day2/NINETEENTH-RUN-NOTES.md) | Esc closes the Politics panel and the editor with it, but edit mode stays on. Reopening the panel and applying works, and this was accepted as minor. |
| 5-day cooldown | Works today | 2026-10-12, CAPIE 1.0 release | [17th run](../evidence/qa-20261011-day2/SEVENTEENTH-RUN-NOTES.md), [19th run](../evidence/qa-20261011-day2/NINETEENTH-RUN-NOTES.md), [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md) | Pencil greys out with the date of the last change, and unlocks by itself after five in-game days. Checked for leader, state and city. The cooldown for subjects and for Restore name was not checked. |
| Subjects, renamed by the overlord (scripted diplomatic action) | Works today | 2026-10-14, CAPIE 1.2 developer build | [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md) (PID40704, PID22860, PID34504) | Checked as the United Kingdom on Canada. The action appears only for own subjects; the name survives save and restart under CAPIE 1.2. Leader and party tabs were reported working in one run without detail. Not checked: subjects whose name depends on the overlord, the notice to a player-controlled subject, and the permission check from the later game rule. |
| Restore name button | In progress | Not checked | [script and UI report](../evidence/session-20261011/claude-script-ui/report.txt), sections "Przycisk Przywróć nazwę" and "Reguły gry" | Added after the CAPIE 1.2 run. The CAPIE 1.2 run found no reset button in the editors, and the button was added in response. Under the current decision it uses the 5-day cooldown. The console reset is checked (see console row). |
| Game rules: rename rights | In progress | Not checked | [script and UI report](../evidence/session-20261011/claude-script-ui/report.txt), section "Reguły gry" | Offline tests only. The "host renames everyone" option works in single player only, because the game cannot identify the host in multiplayer yet. |
| Game rules: rename news event | In progress | Not checked | [script and UI report](../evidence/session-20261011/claude-script-ui/report.txt), section "Reguły gry" | Offline tests only. The old name appears in the title only when a player name existed before. |
| Factions | In progress | Not checked | [script and UI report](../evidence/session-20261011/claude-script-ui/report.txt), section "Reguły gry", item 4 | The script is in `mod/`, but Apply needs a CAPIE faction name layer (API level 5) that does not exist yet. With current CAPIE the editor shows a message and changes nothing. |
| Console: `change_nation_name`, `reset_nation_name` | Works today | 2026-10-14, CAPIE 1.2 developer build | [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md), [23rd run](../evidence/qa-20261013/TWENTY-THIRD-RUN-NOTES.md) | Developer builds with console support only. A name set from the console survived save and load (CAPIE 1.1); `reset_nation_name CAN` restored the game's name (CAPIE 1.2). `change_nation_name` was not rerun on CAPIE 1.2. Refusal in multiplayer and ironman was not checked. |
| Console: `capie_cmd` | Works today | 2026-10-13, CAPIE 1.1 console developer build | [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md) | Checked with the console demo mod (`echo`, `mark`). Listed in `help`. Not rerun on CAPIE 1.2. |
| CAPIE status line | Works today | 2026-10-13, CAPIE 1.1 | [20th run](../evidence/qa-20261011-day2/TWENTIETH-RUN-NOTES.md), [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md) | Rename Anything showed "CAPIE 1.0 loaded" and "CAPIE 1.1 loaded" in a campaign. The public `CAPIE_STATUS`, `CAPIE_API` and `CAPIE_BUILD` keys have no separate recorded check, and whether the main menu can draw them is open. |
| CAPIE script SDK and demo mod | In progress | 2026-10-13, CAPIE 1.1 console developer build | [21st run](../evidence/qa-20261011-day2/TWENTY-FIRST-RUN-NOTES.md), [22nd run](../evidence/qa-20261011-day2/TWENTY-SECOND-RUN-NOTES.md) | The demo editors failed in the first run and passed after a fix; the SDK's two parser errors were removed. Not checked: the SDK on CAPIE 1.2 (its country name effect now writes the name layer), the extended alphabet in different fonts, the lobby rule. |
| CAPIE launcher | In progress | 2026-10-12, CAPIE launcher candidate | [handoff of 2026-10-13](../evidence/notes/HANDOFF-20261013.md) | Through the Steam launch option the launcher found the game and loaded CAPIE. Shortcut mode, which starts the Paradox Launcher directly, did not work. Review findings L1 to L5 are not fixed. |
| Proxy loader (`version.dll`) | In progress | 2026-10-12, stage 2 release DLLs | [runtime report](../evidence/proxy-loader-20261012/runtime-report.md), [proxy loader](proxy-loader.md) | Startup acceptance only: four cases passed (off, `-capie` flag, config file, tampered bundle). No editor use, save or load through the proxy. Passing the flag through Steam and the Paradox Launcher is unverified. |
| Bootstrap installer | In progress | Not checked in game | [runtime report](../evidence/proxy-loader-20261012/runtime-report.md), [bootstrap README](../../tools/bootstrap/README.md) | Pester tests only. There is no release package, so the installer refuses to run. |
| Multiplayer | Not supported | Never | [multiplayer test plan](multiplayer-test-plan.md) | Never tested with two players. The version handshake with feature lock ([ADR-0012](../adr/0012-multiplayer-level-l0.md)) exists in the script SDK only and has never run with two players. |
| Ironman and achievements | Not supported | Never | None | Not checked in game. The console's ironman refusal and the game rule's achievement setting are untested. |

## Known failures and open checks

Open items, from the run notes:

- Esc closes the Politics panel, and the editor disappears with it, but edit mode and its epoch
  stay on. Reopening the panel and applying works.
- The text field accepts more than 32 characters (96 in one run). Apply refuses the name with a
  visible message and changes nothing, but typing is not stopped at 32.
- A long leader name runs under the pencil next to the portrait.
- The country editor was cramped, with a small serif font (11th and 16th runs). Later builds were
  not rechecked for this.
- Party groups 1 and 3 have no in-game check.
- Name layer checks G3 to G5, G7, G9, G10, G12 and G13 from the
  [stage 2 report](../evidence/session-20261013/custom-name-layer/final-report.txt) have not run:
  story cosmetic tags, cosmetic tag and ideology changes, subjects named after the overlord,
  loading without CAPIE, migration of `RN1` saves, decisions with the layer, and performance.
  Migrating an `RN1` save does not restore the cosmetic tag the country had before.
- Restore name, the game rules, the news event and factions have not run in the game. Factions
  also need a CAPIE version that does not exist yet.
- The legacy editor's switch to the on-screen keyboard without CAPIE has not run.
- Console refusal in multiplayer and ironman, and `reset_nation_name` on a country with a story
  cosmetic tag, are unchecked.
- Launcher shortcut mode fails when it starts the Paradox Launcher directly; a shortcut has to go
  through Steam.
- Proxy loader flag forwarding through Steam and the Paradox Launcher is unverified.
- Some runs that reloaded a save gained one extra copy of an existing vanilla error in `error.log`
  (`GER - Germany.txt`, Hs123 icon). No new error types appeared. Cause not established.
- Some runs showed a black rectangle at the top left of the main menu, gone in the campaign. Cause
  not established.

Failures that were fixed and checked again in the game:

- Country names leaking into decision titles and descriptions (an early build, before the `RN1`
  parser fix). Decisions kept their own texts in later runs.
- Stale or blank target label in the leader editor (1st and 2nd run), fixed in the 3rd run.
- Fractional values passing the payload validator (6th run), fixed in the 7th run.
- Leader caption not refreshing (18th run), fixed in the 19th run.
- Legacy on-screen Polish letter buttons not typing, SDK demo editors refused as badly built, and
  two SDK parser errors (21st run), all fixed in the 22nd run.
- Subject entry not showing, then drawn over the diplomacy header (23rd run), replaced by the
  scripted diplomatic action in the same set of runs.
- Subject flag changing with the name (23rd run), fixed by the CAPIE 1.2 name layer.

## How to run an in-game check

1. Close the game. Run `tools/test-session.ps1 -Action Begin -Language polish` to back up and set
   up a 1720x900 test profile with only the development mod. See
   [Testing in the game](building-and-testing.md#testing-in-the-game).
2. Start the game and load CAPIE with the build you are testing. Load a copy of a save, never your
   only copy.
3. Note the build, the CAPIE log lines and the `error.log` count at startup and at exit, then check
   the feature on screen.
4. Exit without overwriting saves you need, close the game, and run
   `tools/test-session.ps1 -Action Restore`. It checks the SHA-256 of the backed-up files before
   putting them back.
5. Record the result in a run note under `docs/evidence/`, then update the row in this table.
