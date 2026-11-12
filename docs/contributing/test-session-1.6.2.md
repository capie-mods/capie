# In-game test session: CAPIE 1.6.2 (HOI4 1.19.5.0 hotfix)

Smoke test of the 1.19.5.0 port (`docs/research/port-1.19.5.0.md`). The game's code did not
change in any place CAPIE pins, and CAPIE changed only its pinned hash and version, so this checks
that every layer still switches on and works once; the full feature checklist is
[test-session-1.6.0.md](test-session-1.6.0.md). Write the result into
[in-game-test-record.md](in-game-test-record.md), one row, named by the game process ID.

## Before you start

- Game: HOI4 **1.19.5.0** (main menu, bottom left; `hoi4.exe` SHA-256 `709F916A…FCE148`).
- Build: `plugin/build/capie-1.6.2` of the `hotfix-1.6.2` worktree (release flavor) and its
  `debug` folder (debug flavor, more log lines). Never inject `capie-1.6.1` or older: their
  injector refuses 1.19.5.0 with exit code 10.
- Rename Anything 0.3.x enabled (Workshop item, or the dev copy from
  `rename-anything\tools\test-session.ps1 -Action Begin`; then `-Action Restore` afterwards).
- Start the game normally from Steam or the Paradox Launcher and wait for the main menu.

```powershell
$b = 'C:\Users\cypko\Documents\GitHub\capie-worktrees\capie-wt-hotfix162\plugin\build\capie-1.6.2'  # or "$b\debug"
$game = (Get-Process hoi4).Id
& "$b\capie_injector.exe" $game "$b\capie_core.dll";   "core exit $LASTEXITCODE"
& "$b\capie_injector.exe" $game "$b\capie_editor.dll"; "editor exit $LASTEXITCODE"
```

Logs: `capie_core.log` and `capie_editor.log` next to the DLLs, and the game's `error.log`.

## Checks

| # | Steps | Expect |
|---|---|---|
| P1 | Inject both DLLs (above). | Both exit codes 0 (10 = wrong game build, 11 = already loaded). |
| P2 | Main menu after injection. | Status "CAPIE 1.6 loaded". `capie_core.log` shows the activation and no "layer off" line or failed pin id (the debug output, e.g. DebugView, shows "country hooks initialized (capie_core, pinned 1.19.5.0)"). |
| P3 | New game as POL. Rename the country and its adjective in Rename Anything's editor: type, Backspace, Enter, try more than 32 characters, Apply. | The game's own text field works; the limit holds; the name shows at once in the politics view and on the map (caption refresh). |
| P4 | Rename the leader and the ruling party. | New names show at once. |
| P5 | Change the flag, then the leader portrait. | Both show at once, in the politics view and the top bar. |
| P6 | Note the `error.log` line count. Apply a map colour; switch to the claims map mode. | Political and claims maps repaint; no new "Getting continent for" lines. |
| P7 | Lead a faction and rename it. | The faction name shows in the faction view. |
| P8 | After an Apply, press Space. | Focus is released: the game pauses/unpauses, no typing into a hidden field. |
| P9 | Apply records (debug flavor or the mod's info). | `capie_cap = 8`, `capie_ver = 10602`, `capie_colour = 1`; `CAPIE_BUILD` = `1.6.2`. |
| P10 | Save, load the save (same session). | Every change from P3..P7 persists. |
| P11 | Restart the game without injecting; load the save. | Vanilla fallbacks, "CAPIE missing" indicators, no crash. |
| P12 | (Optional) Repeat P1..P3 with the `debug` DLLs in a fresh game. | No pin failure lines; the host probe reports single player. |

## After the session

- Fill in [in-game-test-record.md](in-game-test-record.md).
- Release steps (not part of the port): pin the tested binaries (`deploy/bundle-inputs.json`,
  `build_launcher.ps1`, `release.toml`), installer pins (`tools/bootstrap`), Workshop texts,
  then `tools/release/release.py`.
