# In-game test session: CAPIE 1.7.0 (stop signal and editor status)

Checks for the "CAPIE stopped" work of branch `capie-stop-signal` (sentinel `-4`, the fault log,
the "editor off" status keys, editor start logging, launcher and proxy status). The rest of the
1.7.0 checklist (API level 9) comes with that work; the full feature checklist of the previous
release is [test-session-1.6.0.md](test-session-1.6.0.md). Write the result into
[in-game-test-record.md](in-game-test-record.md), one row, named by the game process ID.

The report that started it: "old renamed names still show, but I can't rename anything after
updating CAPIE". The names come from `capie_core.dll`; renaming needs `capie_editor.dll`. Before
1.7 an editor that could not work answered "This editor is out of date. Cancel and open it
again.", so the player reopened it forever.

## Before you start

- Game: HOI4 1.19.4.2. Build: `plugin/build/capie` of this branch (release flavor), never a
  reviewed folder.
- Rename Anything **0.5.0** (worktree `rename-anything-wt-040`, branch `ra-0.4.0`), dev copy
  through `tools/test-session.ps1 -Action Begin`; `-Action Restore` afterwards. Play once in
  English and once in Polish (S9).
- Inject as in [test-session-1.6.1.md](test-session-1.6.1.md); some checks inject only one DLL.

Logs: `capie_core.log` and `capie_editor.log` next to the DLLs, `%LOCALAPPDATA%\RenameAnything\`
(`launcher.log`, `proxy.log`, `last-status.txt`) and the game's `error.log`.

## Checks

| # | Steps | Expect |
|---|---|---|
| S1 | Inject both DLLs. Open Rename Anything's Rename window. | Status "CAPIE 1.6 loaded" (the text still names 1.6 until the version is raised). `capie_core.log` has "CAPIE_CORE editor reported code=0 (editor on)". No `CAPIE_FAULT` line in either log. |
| S2 | Rename Anything's own **country** editor (pencil in the politics view): name and adjective, Apply. | Name and adjective change at once; status line shows the hint, no error. |
| S3 | Rename Anything's own **leader** editor: new name, Apply. Then Apply an empty field. | The leader's name changes; the empty field gives its own field error, not "out of date". |
| S4 | Rename Anything's own **party** editor: the ruling party (short and long name), then another party tab. | Both change; no "out of date" line. |
| S5 | Reopen the country editor, close the politics view with the editor open, reopen it and Apply. | The usual stale line "This editor is out of date. Cancel and open it again." (sentinel -1 is unchanged). |
| S6 | New game start. Inject **only `capie_editor.dll`** (no core). Open the country editor and Apply. | Line "CAPIE stopped after an error. Save, then restart the game." (the country editor without the core's name layers gets -4). The leader and party editors still work. `capie_editor.log`: "stale-sentinel epoch=-4 reason=capie-stopped". |
| S7 | Same session as S6: look at the status line. | The mod's own fallback ("CAPIE missing..."), because the core is not loaded. |
| S8 | New game start. Inject **only `capie_core.dll`**. Open the Rename window, then the country editor and Apply. | Status "CAPIE 1.6: editor off, restart the game"; Apply says "CAPIE is not loaded..." (no editor, no records). |
| S9 | Repeat S6 and S8 with the game in Polish. | "CAPIE zatrzymało się po błędzie. Zapisz grę, a potem uruchom ją ponownie." and "CAPIE 1.6: edytor wyłączony, uruchom grę ponownie". |
| S10 | Subjects, faction, flag, portrait and colour pickers in the S6 session (grammar editors). | Their Apply works or names its own reason; none shows "out of date" for a lasting cause. |
| S11 | Launcher run where the editor step fails (rename `capie_editor.dll` in the bundle copy of a scratch install, or use an old editor DLL). | Notice "CAPIE could not be fully loaded"; `last-status.txt` says `outcome=capie-editor-failed`, not `game-without-capie`; in the game the status says "editor off". |
| S12 | Proxy start (`-capie`) with a working bundle. | `last-status.txt` says `outcome=capie-loaded` with "by the version.dll loader". |
| S13 | (Optional, when a later game update makes this build unsupported.) Inject `capie_editor.dll` into the updated game. | Injector exit code 10, and `capie_editor.log` now has "RN_LIFECYCLE initialization failed code=10" (the log opens before the checks). |
| S14 | Not reproducible on purpose: a real fault inside a game call. If one ever happens, check the logs. | One `CAPIE_FAULT game call <name> faulted: exception=0x... thread=...` line in the DLL that faulted, one "refused" line per DLL, status "editor off", every editor Apply says "CAPIE stopped...". |

## After the session

- Fill in [in-game-test-record.md](in-game-test-record.md).
- A failure of S2..S4 blocks the release; S11..S13 can be fixed in a point release.
