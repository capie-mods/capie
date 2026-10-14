# Multiplayer test plan

Plan for a multiplayer test of Rename Anything, based on reading the installation files and static analysis of the pinned `hoi4.exe` 1.19.3.0. Investigation date: 2026-10-11. Original notes (Polish): [../evidence/research/mp-test-plan.md](../evidence/research/mp-test-plan.md).

**Multiplayer has not been tested.** No second instance was started, and no user configuration or game file was changed. Rename Anything makes no claim of multiplayer compatibility. This plan is not evidence that multiplayer works.

## Finding

The `-userdir=<path>` parameter exists and lets you pass a separate user data directory. It does not remove the second-instance lock, though: this build looks for an existing window of class `Hearts of Iron IV` and terminates the process if it finds one. Two ordinary launches on the same desktop, even with separate directories, are not a ready procedure for an MP test.

A second, independent host with its own network identity is the simpler route to a reliable test. The single-machine variant needs a separate experiment on the window lock and on how the network backend identifies the two players.

## Static evidence

File: `C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV/hoi4.exe`.
SHA-256: `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`.
ImageBase: `0x140000000`; the code addresses below are RVAs.

Method: `pefile` and `capstone` from the existing `revenv`, function boundaries from `.pdata`, RIP-relative reads and resolution of PE imports. Recovered functions were not given unconfirmed names. An attempt to run `REA search_strings("userdir=")` returned `target_unavailable / No app is open`; the shared analysis target was neither opened nor closed. The results below come from reading the same file directly.

| Observation | Evidence | Level |
|---|---|---|
| Token `userdir=` | file offset `0x2715280`, RVA `0x2716C80` | CONFIRMED-STATIC |
| The main parser joins the argv arguments and splits them on `-` characters | function `0x125090`; `mov r8b,0x2D` / call `0x24CD080` at `0x1251CA..0x1251D5` | CONFIRMED-STATIC |
| The parser recognises the `userdir=` prefix and copies the suffix | `0x1255CF..0x125661`; prefix length 8, result to context `+0x50` | CONFIRMED-STATIC |
| A non-empty explicit directory takes precedence over the default paths | resolver `0x120DA0`: input length check at `0x120DC4`, copy and jump to exit `0x120DCA..0x120DCF` | CONFIRMED-STATIC |
| Without an explicit path, `userdir.txt` and `launcher-settings.json/gameDataPath` are examined | `0x120DD4`, call `0x1223E0` at `0x120DF7`; key `gameDataPath` at `0x122555` | CONFIRMED-STATIC |
| The game window class is passed as the constant `Hearts of Iron IV` | `0x147F83..0x147FAA`: string creation and the third argument of constructor `0x222B420` | CONFIRMED-STATIC |
| The constructor checks for a window of this class and terminates the process if one exists | `0x222B62D..0x222B649`: `FindWindowExA`; the error branch leads to `FatalExit` at `0x222B6F6` | CONFIRMED-STATIC |
| A second identical variant of the window check exists | function `0x222DCB0`; `FindWindowExA` at `0x222DCDA`, `FatalExit` at `0x222DD90` | CONFIRMED-STATIC |
| `windowtag` is read only when the window is created, after the singleton check | `0x222C3CD..0x222C3DB` in function `0x222C1E0`; the constructor calls it at `0x222B6FF` | CONFIRMED-STATIC |

Lock excerpt, with imports resolved from the PE table:

```text
0222b62d lea rcx,[r14+48h]       ; stored class name
0222b631 call 00129af0
0222b636 mov r8,rax              ; lpszClass
0222b639 xor r9d,r9d             ; lpszWindow = NULL
0222b63c xor edx,edx             ; hwndChildAfter = NULL
0222b63e xor ecx,ecx             ; hwndParent = NULL
0222b640 call [USER32!FindWindowExA]
0222b646 test rax,rax
0222b649 je 0222b6fc             ; no window: continue startup
... message about an existing instance ...
0222b6f6 call [KERNEL32!FatalExit]
```

`launcher-settings.json` confirms version `1.19.3.0`, `distPlatform: steam`, `gameDataPath: %USER_DOCUMENTS%/Paradox Interactive/Hearts of Iron IV` and the `-debug_mode` argument for the debug variant. The installation currently has no `userdir.txt`.

## Network backend and limits of the findings

- `interface/frontendmultiplayerview.gui:10` contains a `join_direct` button, but its localisation in `localisation/english/multiplayer_l_english.yml:39` reads **Connect to Server ID**. The element name `join_direct` alone does not prove a TCP/UDP connection to `127.0.0.1`.
- The PE imports `SteamAPI_Init`, `SteamAPI_IsSteamRunning`, Steam callbacks and `SteamGameServer_*`. Strings in `.rdata` include `SteamNetworking005` (offset `0x2B6C6D0`) and `SteamMatchMaking009` (`0x2B6BDA0`). This proves the integration is present, not which backend is selected.
- A scan of standalone ASCII literals found no `nakama`, `nosteam`, `no_steam`, `connect=` or `port=`. This does not rule out other parsers, encoded names or paths inside libraries. There is no basis for recommending such switches as working.
- Different player names in the settings do not prove different network identities. Two processes under one Steam session may share an identity; this was not checked on this installation. The user's existing `steam_api64.dll` stays untouched.

## Preparing directories and a launch recipe

The recipe below is **a candidate for a single-process isolation test only**. It has not been run and does not get around the lock described above. Close the running game first, with the agreement of whoever is running QA.

1. Create two new, empty test directories, for example `C:/Users/user/Documents/hoi4_rn_mp_a` and `C:/Users/user/Documents/hoi4_rn_mp_b`. Use names without spaces or hyphens, because the recovered parser splits its input on `-`. Do not point either of them at the normal user data directory.
2. In each directory, prepare only the settings needed to start, a separate `dlc_load.json` with just the test mod, and `mod/rename_anything_dev.mod` pointing with an absolute path to `repo/mod`. Do not copy the whole user profile or its saves. Verify that the DLC and mod files are identical, and for the mixed-language test set EN and PL separately.
3. First start one process without changing `launcher-settings.json` or `userdir.txt`:

   ```powershell
   $rnGameDir = 'C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV'
   $rnExe = Join-Path $rnGameDir 'hoi4.exe'
   Start-Process -FilePath $rnExe -WorkingDirectory $rnGameDir -ArgumentList @(
       '-debug_mode',
       '-userdir=C:/Users/user/Documents/hoi4_rn_mp_a'
   ) -WindowStyle Hidden -PassThru
   ```

4. Before going further, confirm live that logs and new files appear in `hoi4_rn_mp_a` and that the normal user directory receives no changes. Also check that the right mod loads and the checksum. Reading the switch in the binary does not replace this IO check.
5. For the second process, change only `mp_a` to `mp_b`, but **do not launch it on the normal desktop as if that were a working solution**. An approved and verified way to separate the instances and the network identities is needed first.

Hypotheses for a separate experiment: a second Windows desktop or session, or test instrumentation in the memory of the second process limited to this `FindWindowExA` check. Neither route has been implemented or confirmed. Changing the window title, the EXE name or only `userdir` does not change the constant class used by the check shown above.

## Criteria for a proper multiplayer test

Once two independent clients are available: host plays POL 1936, the guest plays a different country or joins in co-op; identical mod version and checksum. First get both into one session and confirm they show the same date and world state. Then use the UI to rename a character, a state and a city, and confirm both players see the same literals. Repeat with Polish characters and with clients set to different languages.

Save, quit both clients and load the session again; compare the names and the logs on both sides. Collect screenshots from both processes, excerpts of `game.log` / `error.log`, and the checksum, mod version, language and any OOS events. The absence of an OOS message, without evidence that two clients were connected, does not pass this definition-of-done item.
