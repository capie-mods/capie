# In-game test session: API 9 spikes S1 and S2 (throwaway build)

These two experiments decide whether CAPIE may show runtime catalogs (flags, portraits) by
re-pointing slot sprites (spec `api9-runtime-catalogs-spec.md` §4 and §12.1; texture release:
`../research/texture-release-2026-11.md`). The build is **throwaway**: it exists only to answer
S1 and S2, it is never released, packaged or given to players. Write the results into
[in-game-test-record.md](in-game-test-record.md) (one row per step, with the game process ID).

Run the whole session twice: once on **vanilla**, once with **KaiserreduX** (Workshop 2076426030).

## What the spikes test

| # | Question | Pass |
|---|---|---|
| S1 | CAPIE re-points one declared sprite (`+0x1A0` id, `+0x170` texture, `+0xA4` size) from a click, on the UI thread. Does the image show the new file? | The icon shows the new flag after the click (at the latest on the next window refresh). No crash. No new line in `error.log`. |
| S2 | Two sprite sets. CAPIE loads the next flag into the **hidden** set, then the click's effect switches the image text to that set. 200 times, automatically. | Each flip shows the flag that the log names for it. No flicker of a wrong flag. The texture manager's live count stays bounded (no leak). No crash, no new `error.log` line. |
| R | Texture release: CAPIE releases the old texture of a re-pointed sprite 3 window updates and 300 ms after the swap. | `released id=…` lines follow the flips; `outstanding` stays small; `reload_textures` with the window open does not crash. |

## Before you start

- Game: HOI4 **1.19.4.1** (`hoi4.exe` SHA-256 `CF781ED1…10476F`). On any other build the injector refuses.
- Build the spike (from the repository root; it refuses any output folder but `plugin\build\capie-spike`):
  ```powershell
  pwsh -NoProfile -File plugin/capie/build.ps1 -MinHookDir C:\Users\cypko\AppData\Local\capie-deps\minhook -Spike -SkipBenchmarks
  ```
  The last lines say `SPIKE BUILD (CAPIE_SPIKE=1): throwaway…` and `CAPIE build and offline tests passed`.
- Install the test mod: `pwsh -NoProfile -File plugin/capie/spikes/run_spike.ps1 -InstallMod`.
  In the Paradox launcher, make a playset with **CAPIE spike S1-S2 (throwaway)** (vanilla run), or with
  KaiserreduX and the spike mod **after** it (KaiserreduX run). Do not add Rename Anything or the CAPIE switch mod.
- Start the game from the launcher **without CAPIE** (no CAPIE launch option, no CAPIE launcher). CAPIE
  must not be loaded yet, otherwise the injector stops with exit 11.
- Note the line count of `Documents\Paradox Interactive\Hearts of Iron IV\logs\error.log` (say `E0`).
- In the main menu, inject the spike: `pwsh -NoProfile -File plugin/capie/spikes/run_spike.ps1 -Inject`.
  It prints `Injected capie_core.dll …` and `Injected capie_editor.dll …`.
- Log: `plugin\build\capie-spike\capie_editor.log`. Keep it open in an editor that reloads (or
  `Get-Content -Wait`). Every spike line starts with `CAPIE_SPIKE`.

## 0. Start-up

| # | Steps | Expect | Fail |
|---|---|---|---|
| 0.1 | After the injection, read `capie_editor.log`. | `CAPIE_SPIKE armed (THROWAWAY BUILD, S1 and S2; model capie_spike)` and the usual `RN_LIFECYCLE initialized … CAPIE 1.6` line. | `CAPIE_SPIKE off: pin <id> failed` (a pin or RTTI check failed: stop, report the id). No `armed` line at all: the editor DLL did not initialise (check the injector exit code). |
| 0.2 | Start a new game as **GER** (vanilla) or as **GER** (KaiserreduX), any date. Open the decisions tab. | A category "CAPIE spike (throwaway)" with the decision "CAPIE spike: open window". | No category: the mod is not loaded (playset). |
| 0.3 | Click the decision. | A window "CAPIE spike S1 / S2 (throwaway)" with two icons (a "?" goal icon on the left, a "?" goal icon on the right) and seven buttons. | No window: check `error.log` for `capie_spike` lines (a script error in the mod). |
| 0.4 | **KaiserreduX run only:** click **List vanilla/KRX** once. | Log: `CAPIE_SPIKE flag list kaiserredux`. | `flag list vanilla`: click it again. |

## 1. S1: re-point one sprite

| # | Steps | Expect | Fail |
|---|---|---|---|
| 1.1 | Click **S1 next flag**. | The left icon becomes a flag: vanilla `GER_neutrality` (the black-white-red flag), KaiserreduX `GER` (the Imperial flag). Log, in this order: `CAPIE_SPIKE placeholder sprite=GFX_capie_spike_s1 path=gfx/interface/goals/goal_unknown.dds`, then `CAPIE_SPIKE repoint sprite=GFX_capie_spike_s1 path=gfx/flags/<stem>.tga id=<n> old=<m> tex=<pointer> size=82x52 live=<L> outstanding=<o>` and `CAPIE_SPIKE s1 click path=… ok=1`. | The icon stays "?" while the log says `ok=1`: the widget copied the texture at bind (O2 = no). Write it down, then hide and show the window (close, decision again) and record whether the flag appears then. `ok=0` with `sprite … not usable`: the sprite was not found. `missing texture path=…`: the file is not in this mod set (note the stem). A crash: stop, keep `minidump.dmp` and the log. |
| 1.2 | Wait 1 second, then read the log. | `CAPIE_SPIKE released id=<m> live=… outstanding=…` for the `old` id of 1.1 (if `old` was `-1`, the placeholder had never been drawn and nothing is released). | No `released` line after several seconds: the window did not tick (record it). |
| 1.3 | Click **S1 next flag** 10 more times, about one click per second. | The icon follows the log every time (vanilla: SOV, ENG, FRA, ITA, JAP, USA, POL, CZE, AUS, HUN). One `released` line per click. `outstanding` stays between -1 and 2. | A flag that differs from the logged path, a frame of garbage, or `outstanding` that keeps growing. |
| 1.4 | Hover over the icon, pause and unpause, switch to the political map mode and back. | The flag stays the same. | The icon reverts to "?" (the game reloaded the sprite: record when). |
| 1.5 | Click **Reset**. | The left icon is the "?" goal icon again. Log: `CAPIE_SPIKE reset sprite=GFX_capie_spike_s1 ok=1` (and for the S2 sprites once they were used). | The icon keeps the flag. |
| 1.6 | Compare `error.log` with `E0`. | No new lines that mention `capie`, `flags` or a texture. | Any new texture error: copy the lines. |

S1 passes when 1.1, 1.3 and 1.6 pass. Record O2: "the widget reads the sprite texture per draw" is
**yes** if the icon changed in 1.1 without closing the window.

## 2. S2: two sets, 200 flips

| # | Steps | Expect | Fail |
|---|---|---|---|
| 2.1 | Click **S2 flip** once. | The right icon shows the first flag of the list, the label reads `S2: set 1, flip 1`. Log: `CAPIE_SPIKE repoint sprite=GFX_capie_spike_s2_1 …`, `CAPIE_SPIKE flip n=1 show_set=1 path=gfx/flags/<stem>.tga`. `game.log` has `CAPIE_SPIKE script s2 par=1 n=1`. | Label stays `set 0, flip 0`: the records did not reach the effect (look for `flip n=1: constructor calls 0` in the log). The icon shows the old texture of set 1: the image did not switch. |
| 2.2 | Click **S2 flip** 4 more times. | The sets alternate 0, 1, 0, 1 and the flag follows the log each time. | The visible icon changes **before** the label (the visible set was touched: a bug, report it). |
| 2.3 | Note the last `live=` value of the log (`L0`). Unpause the game at speed 1 (or leave it paused; both work). Click **S2 auto 200**. Watch the right icon for about 50 seconds. | Log: `CAPIE_SPIKE auto start flips=200 interval_ms=250`, then one `flip n=…` line every 250 ms and `released id=…` lines in between. The icon changes four times a second, each time to a whole flag. At the end: `CAPIE_SPIKE auto done flips=<n> outstanding=<o>`. | A flag that flickers to the previous one, a blank or garbage frame, a crash, or `auto done` never appearing. |
| 2.4 | Check the numbers of the last 20 `repoint` lines. | `live=` stays within about `L0 + 4` (the two sets, plus the textures waiting for release; the list repeats after 43 or 48 flags, which then hit the cache). `outstanding` stays between -2 and 3. | `live` keeps growing by about one per flip: textures are not released (R fails). |
| 2.5 | Compare the label with the log: the label's flip number and set equal the last `flip n=… show_set=…` line; the icon shows that line's flag. | All three agree. | Any mismatch: copy the last 10 lines. |
| 2.6 | Click **S2 auto 200**, and after about 5 seconds click **Stop**. | Log: `CAPIE_SPIKE auto stop left=<n>` and no more flips. | Flips continue. |
| 2.7 | With the window open, type `reload_textures` in the console (`~`). | The game reloads; the icons may return to their placeholders ("?"). No crash. After it, **S1 next flag** and **S2 flip** still work. | A crash or a hang: keep `minidump.dmp`, `exception.txt` and the log. |
| 2.8 | Close the window (**Close**), open it again with the decision, click **S2 flip** twice. | It keeps working; the sets continue alternating. | Wrong set shown after reopening. |
| 2.9 | Compare `error.log` with `E0` again. | No new texture or `capie` lines (except `CAPIE_SPIKE script` lines if the game writes `log` effects there). | New texture errors. |

S2 passes when 2.3, 2.4, 2.5 and 2.9 pass; R passes when 1.2, 2.4 and 2.7 pass.

## 3. Memory paging check (texture release)

| # | Steps | Expect | Fail |
|---|---|---|---|
| 3.1 | Note the game's private memory in Task Manager (Details, `hoi4.exe`, "Memory (private working set)"): `M0`. | | |
| 3.2 | Run **S2 auto 200** four times in a row (800 flips; KaiserreduX run: 800 flips over 48 different flags). | Memory after the runs is within about 50 MB of `M0`, and the last `live=` value is within about 4 of `L0`. | Memory grows steadily (more than about 100 MB) with `live` growing: a texture leak. |
| 3.3 | Click **Reset**, wait 2 seconds. | `released` lines for the last flag ids; `outstanding` near 0. | |

## 4. After the session

- Quit the game. CAPIE was injected only into that process; nothing is installed.
- Remove the spike mod from the playset (and, if wanted, delete `mod\capie_spike_mod` and
  `mod\capie_spike_mod.mod`).
- Record in `in-game-test-record.md`: S1 pass/fail, O2 answer (1.1), S2 pass/fail, R pass/fail,
  the `live`/memory numbers of 2.4 and 3.2, and anything unexpected, for vanilla and KaiserreduX.
- If S1 or S2 fails, the flag and portrait pickers (spec §4) must not ship as designed; keep the
  log and report it before the catalog commits are released.
