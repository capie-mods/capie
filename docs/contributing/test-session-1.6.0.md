# In-game test session: CAPIE 1.6.0 (API level 8)

This is the checklist for the first in-game run of CAPIE 1.6.0. It covers the two new layers
(country colour, CONTRACT section 19; ruling-party ideology name, CONTRACT section 20), the open
in-game unknowns of the colour spec, and two checks left over from the hot-path work (commit
`c9a19df`). Write the results into [in-game-test-record.md](in-game-test-record.md), one row per
feature, and name each run by its game process ID.

## Before you start

- Game: HOI4 **1.19.4.1** (`hoi4.exe` SHA-256 `CF781ED1…10476F`). Branch `api8-layers` is rebased
  on the 1.19.4.1 port of main and every colour and ideology-name pin was re-ported
  (`docs/research/port-1.19.4.1.md`, section "API level 8"). On 1.19.4.0 CAPIE stays off as on
  any unknown build.
- Build: `plugin/build/capie-1.6.0-rc` (release flavor) and `capie-1.6.0-rc\debug` (debug flavor,
  for the `CAPIE_COLOUR` and `CAPIE_SUBIDEOLOGY` debug lines). Never use `plugin/build/capie-1.5.1`
  or `plugin/build/capie-1.5.2`.
- Rename Anything: a build whose country editor has the colour picker (`rn_cp_apply`) and the
  party-ideology name field (`capie_cap >= 8`, `capie_colour`, `capie_subideo`).
- Log: `Documents\Paradox Interactive\Hearts of Iron IV\logs\capie.log` (or the debugger output).

## 0. Start-up and records

| # | Steps | Expect |
|---|---|---|
| S1 | Start the game with CAPIE; open the main menu. | Status line "CAPIE 1.6 loaded". The init line reads `colour layer on` and `subideology layer on`, with no failed pin id. |
| S2 | Open any grammar editor of the demo mod and Apply. | The Apply has `capie_cap = 8`, `capie_ver = 10600`, `capie_colour = 1`, `capie_subideo = 1`. Localisation keys: `CAPIE_API` = `8`, `CAPIE_BUILD` = `1.6.0`. |
| S3 | Break one pin with a throwaway build that changes the expected bytes of one colour pin, then of one ideology-name pin (a patched copy of the exe fails the SHA-256 check; offline, the flip checks of `colour_layer_pins_exe_test` and `subideology_layer_pins_exe_test` cover this). Never commit that build. | With a colour pin broken: `colour layer off <pin id>`, vanilla colours, `capie_colour = 0`, the other layers work. Same for a sub-ideology pin (`subideology layer off <id>`, `capie_subideo = 0`). |

## 1. Country colour layer (CONTRACT 19, spec section 9.4)

| # | Steps | Expect |
|---|---|---|
| T1 | New game as POL. Apply a preset in the colour picker. | The political map repaints within about a second. Borders and fill match. No errors in the log. |
| T2 | Pick the `colors.txt` RGB of GER for POL. | POL looks exactly like GER on the map (the HSV adjustment is right). |
| T3 | POL leads a faction without a custom colour. Open the faction view and the faction colour picker. | Both show the new colour; the faction colour follows. |
| T4 | Puppet a country with the "subjects in overlord colour" rule on, then off. | Rule on: the puppet shows POL's override. Rule off: it shows its own override (if any), else its own colour. |
| T5 | `set_cosmetic_tag` by console, then wait one day. | The game's cosmetic colour shows at once (snapshot mismatch hides the layer). The mod clears the variables the next day. |
| T6 | Apply, then switch to the claims map mode and two other modes. | Fresh colours in every mode. Measure the frame time at Apply. Decide `mark_all_modes` (on by default) from this. |
| T7 | Restore. | The vanilla colour returns, and the faction colour too. |
| T8 | Save and load with CAPIE; then load the same save without CAPIE. | With CAPIE the colour persists. Without CAPIE: vanilla country colour, the faction colour keeps the override (expected leak, 19.7). |
| T9 | Apply on a country, then start a civil war in it. | Record whether the breakaway has `capie_cc_ver`. If it does, Rename Anything must clear it in its on_action. |
| T10 | Multiplayer, two clients ([multiplayer-test-plan.md](multiplayer-test-plan.md)): A applies; B hot-joins; create a faction led by A; play one year at speed 5. | Same colours on both. No out-of-sync. The hot-joiner sees the colour. |
| T11 | `tag_color POL 255 0 0` while a layer is set. | The layer still wins. After Restore, `tag_color`'s colour shows. |

### Open in-game unknowns of the colour spec (record each answer)

1. Mode 0 is the political map mode and mode 19 the claims mode (inferred from builders and
   strings). T6 decides whether marking all 23 modes dirty is needed or mode 0 is enough.
2. The two UI callers of `GetMapColors` that read `color_ui` but are not tied to a named view:
   look for any icon or widget in the country, diplomacy and naval mission screens that still
   shows the old colour after T1.
3. Whether `start_civil_war` copies country variables to the breakaway (T9).
4. Whether the faction colour is in the out-of-sync checksum. T10 covers the case that matters
   (all clients agree); if a client runs with the layer off (S3 on one machine only), note
   whether the game reports out-of-sync.
5. Thread identity: the repaint call runs inside the `set_variable` and `UpdateNames` hooks, which
   the analysis puts on the logic thread. A wrong thread would show as a crash or an assert in
   `RefreshCountryColor`; note any assert dialog during T1, T5 and T8.
6. Load order (spec risk "map built before `UpdateNames` at load"): after T8's load, the colour
   must show without another Apply. If it shows only after the next change, record it.

## 2. Ruling-party ideology name layer (CONTRACT 20)

The layer renames the ideology line of the ruling-party box in the politics panel (the line that
shows, for example, "Non-Aligned"). It shows at the panel's next refresh.

| # | Steps | Expect |
|---|---|---|
| N1 | New game as POL. Enter a name in the party-ideology field and Apply. Reopen the politics tab. | The ideology line shows the name. The leader name and the election text are unchanged. Debug flavor: `CAPIE_SUBIDEOLOGY bound` then `panel answered`. |
| N2 | Apply again without reopening the tab. | Record whether the panel redraws by itself (expected: not until the tab is reopened). |
| N3 | Other screens that show the ideology (diplomacy view, country tooltip, the ideology map mode). | They keep the game's name (documented limit). |
| N4 | `set_politics` by console to another ruling party; reopen the tab. | The game's name shows again (lapsed). Debug: `CAPIE_SUBIDEOLOGY lapsed`. |
| N5 | Back to the first party with the same leader; reopen the tab. | Still the game's name: the binding keeps the government it was bound to only while nothing changed. Apply the name again: it shows. |
| N6 | Coup or a new country leader in the same party (console `kill_country_leader` or a focus that swaps the leader). | The game's name shows again (the leader is part of the binding). |
| N7 | Save, load with CAPIE; reopen the tab. | The name shows (bound to the government at load). |
| N8 | Load that save without CAPIE. | The game's name; nothing else changes. |
| N9 | Observer / spectator view of another country (if available) and switching tags with `tag`. | The panel shows the name only for the country that has it. |
| N10 | Multiplayer, one client with CAPIE and one without. | Only the CAPIE client shows the name; no out-of-sync (the layer writes nothing). |

## 3. Pending checks from the hot-path work (commit `c9a19df`)

| # | Steps | Expect |
|---|---|---|
| H1 | Bind-time verdict for a reused window: open the demo mod's editor window, close it, open another scripted window of a different mod, then the editor again; repeat a few times (the game reuses freed model addresses). | The editor keeps working on every reopen (prefill, Apply, caption refresh), and the foreign window is never treated as an editor (no CAPIE records, no caption refresh on it). Debug: Bind logs the fresh verdict before the window's first Tick. |
| H2 | A C++ exception inside one of CAPIE's game calls produces a Paradox crash dump. No shipped build can trigger one, so this needs a throwaway local build: make the function one guarded call runs throw a C++ exception (for example, point the colour layer's `refresh` at a test function that does `throw std::length_error("test")`), start a game and Apply a colour. Never commit that build. If no such build is made, record H2 as "not run". | The game's crash reporter (`CPdxCrashReporter`) shows its dialog and writes a dump under `Documents\Paradox Interactive\Hearts of Iron IV\crashes`. It must not be a silent exit with code `0xC0000409`. (Offline, `game_calls_test` already shows the exception is handed to `UnhandledExceptionFilter`.) |

## 4. After the session

- Fill in the rows of [in-game-test-record.md](in-game-test-record.md) for the colour layer, the
  ideology name layer, H1 and H2.
- Answer the open unknowns above in `docs/research/country-colour-2026-10-28.md` (section
  "Remaining unknowns") and decide `mark_all_modes`.
- If N2 shows that players expect an immediate redraw, open the follow-up for a politics-view
  refresh after a `capie_sn_rev` commit (design note section 3, "Redraw").
