# In-game test session: CAPIE 1.6.0 (API level 8)

This is the checklist for the in-game runs of CAPIE 1.6.0. It covers the new country colour
layer (CONTRACT section 19), the open in-game unknowns of the colour spec, and two checks left
over from the hot-path work (commit `c9a19df`). The ruling-party ideology name layer (CONTRACT
section 20) was withdrawn after the first run and is no longer tested (see "Results of the first
run" below). Write the results into [in-game-test-record.md](in-game-test-record.md), one row per
feature, and name each run by its game process ID.

## Results of the first run (1.6.0-rc, 2026-10-29)

1. **Ruling-party ideology name layer: dropped.** The project decided the layer is not needed.
   Its code, hooks, pins, RTTI table, tests, mutants, port anchors, the `capie_subideo` record,
   the v4 capability bit `0x8` and the `lookup_n` frame it used are removed; CONTRACT section 20
   is marked withdrawn, not implemented. API level 8 is the colour layer only. The former
   section 2 of this checklist (N1..N10) and the sub-ideology parts of S1..S3 are gone.
2. **Colour Apply wrote "Getting continent for <N>" to `error.log`.** Every colour Apply added 98
   such lines (5 Applies: 490). Cause: the layer marked all 23 map modes dirty after the game's
   refresh (`mark_all_modes`); the game's next map update then rebuilt mode 21, whose builder logs
   the continent of each province. Static analysis of the 1.19.4.1 exe (the 23-way rebuild
   switch and a call-graph search from every case) shows that only the builders of mode 0
   (political, also the faction colour) and mode 19 (claims) read the country colour; mode 21 is
   the only case that loads the "Getting continent for %d" format. The layer now marks exactly
   these two modes (`colour::colour_map_modes`, pinned in `colour_layer_pins.hpp` and checked
   against the exe by `colour_layer_pins_exe_test`); answer to unknown 1 and T6 in
   `docs/research/country-colour-2026-10-28.md`. Re-run T6 on the next build.

## Before you start

- Game: HOI4 **1.19.4.1** (`hoi4.exe` SHA-256 `CF781ED1…10476F`). Branch `api8-layers` is rebased
  on the 1.19.4.1 port of main and every colour pin was re-ported
  (`docs/research/port-1.19.4.1.md`, section "API level 8"). On 1.19.4.0 CAPIE stays off as on
  any unknown build.
- Build: `plugin/build/capie-1.6.0-rc` (release flavor) and `capie-1.6.0-rc\debug` (debug flavor,
  for the `CAPIE_COLOUR` debug lines). Never use `plugin/build/capie-1.5.1`
  or `plugin/build/capie-1.5.2`.
- Rename Anything: a build whose country editor has the colour picker (`rn_cp_apply`,
  `capie_cap >= 8`, `capie_colour`) and no party-ideology name field.
- Log: `Documents\Paradox Interactive\Hearts of Iron IV\logs\capie.log` (or the debugger output).

## 0. Start-up and records

| # | Steps | Expect |
|---|---|---|
| S1 | Start the game with CAPIE; open the main menu. | Status line "CAPIE 1.6 loaded". The init line reads `colour layer on` with no failed pin id, and has no `subideology layer` part. |
| S2 | Open any grammar editor of the demo mod and Apply. | The Apply has `capie_cap = 8`, `capie_ver = 10600`, `capie_colour = 1`, and no `capie_subideo` record. Localisation keys: `CAPIE_API` = `8`, `CAPIE_BUILD` = `1.6.0`. |
| S3 | Break one pin with a throwaway build that changes the expected bytes of one colour pin (a patched copy of the exe fails the SHA-256 check; offline, the flip checks of `colour_layer_pins_exe_test` cover this). Never commit that build. | `colour layer off <pin id>`, vanilla colours, `capie_colour = 0`, the other layers work. |

## 1. Country colour layer (CONTRACT 19, spec section 9.4)

| # | Steps | Expect |
|---|---|---|
| T1 | New game as POL. Apply a preset in the colour picker. | The political map repaints within about a second. Borders and fill match. No errors in the log. |
| T2 | Pick the `colors.txt` RGB of GER for POL. | POL looks exactly like GER on the map (the HSV adjustment is right). |
| T3 | POL leads a faction without a custom colour. Open the faction view and the faction colour picker. | Both show the new colour; the faction colour follows. |
| T4 | Puppet a country with the "subjects in overlord colour" rule on, then off. | Rule on: the puppet shows POL's override. Rule off: it shows its own override (if any), else its own colour. |
| T5 | `set_cosmetic_tag` by console, then wait one day. | The game's cosmetic colour shows at once (snapshot mismatch hides the layer). The mod clears the variables the next day. |
| T6 | Note the line count of `error.log`. Apply five colours, then switch to the claims map mode and two other modes. | No new "Getting continent for" lines in `error.log` (the 1.6.0-rc wrote 98 per Apply). The political and claims modes show the new colour. Debug flavor: `CAPIE_COLOUR modes marked written=2 of 2`. Measure the frame time at Apply. |
| T7 | Restore. | The vanilla colour returns, and the faction colour too. |
| T8 | Save and load with CAPIE; then load the same save without CAPIE. | With CAPIE the colour persists. Without CAPIE: vanilla country colour, the faction colour keeps the override (expected leak, 19.7). |
| T9 | Apply on a country, then start a civil war in it. | Record whether the breakaway has `capie_cc_ver`. If it does, Rename Anything must clear it in its on_action. |
| T10 | Multiplayer, two clients ([multiplayer-test-plan.md](multiplayer-test-plan.md)): A applies; B hot-joins; create a faction led by A; play one year at speed 5. | Same colours on both. No out-of-sync. The hot-joiner sees the colour. |
| T11 | `tag_color POL 255 0 0` while a layer is set. | The layer still wins. After Restore, `tag_color`'s colour shows. |

### Open in-game unknowns of the colour spec (record each answer)

1. Answered statically (2026-10-29): only modes 0 (political) and 19 (claims) read the country
   colour; marking all 23 modes rebuilt mode 21, which floods `error.log`. The layer marks 0 and
   19 only. T6 confirms it in the game.
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

## 2. Pending checks from the hot-path work (commit `c9a19df`)

| # | Steps | Expect |
|---|---|---|
| H1 | Bind-time verdict for a reused window: open the demo mod's editor window, close it, open another scripted window of a different mod, then the editor again; repeat a few times (the game reuses freed model addresses). | The editor keeps working on every reopen (prefill, Apply, caption refresh), and the foreign window is never treated as an editor (no CAPIE records, no caption refresh on it). Debug: Bind logs the fresh verdict before the window's first Tick. |
| H2 | A C++ exception inside one of CAPIE's game calls produces a Paradox crash dump. No shipped build can trigger one, so this needs a throwaway local build: make the function one guarded call runs throw a C++ exception (for example, point the colour layer's `refresh` at a test function that does `throw std::length_error("test")`), start a game and Apply a colour. Never commit that build. If no such build is made, record H2 as "not run". | The game's crash reporter (`CPdxCrashReporter`) shows its dialog and writes a dump under `Documents\Paradox Interactive\Hearts of Iron IV\crashes`. It must not be a silent exit with code `0xC0000409`. (Offline, `game_calls_test` already shows the exception is handed to `UnhandledExceptionFilter`.) |

## 3. After the session

- Fill in the rows of [in-game-test-record.md](in-game-test-record.md) for the colour layer, H1
  and H2.
- Answer the open unknowns above in `docs/research/country-colour-2026-10-28.md` (section
  "Remaining unknowns").
