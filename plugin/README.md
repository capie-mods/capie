# Native country names (Windows x64, HOI4 1.19.3.0)

This component adds runtime country name/adjective support to the scripted mod.
It reads the name and adjective encoded by the in-game Apply button in a vanilla
`cosmetic_tag` string: `RN1|Name|Adjective|`. It supplies those texts to the game's
localization lookups. The game updates its own country-name caches and map labels;
the component does not directly overwrite `CCountry` fields or generate localization files.

Only this executable is accepted:

`hoi4.exe` SHA-256 `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`.

Initialization also requires exactly one match for each of three signatures at its
expected RVA, within the loaded `.text` section. Any mismatch refuses initialization.
The injector and DLL both check the executable's hash. All patches are in process
memory through MinHook. No files in the game directory are modified, including
`steam_api64.dll`.

## Build

Prerequisites: Visual Studio 2022 with the C++ x64 tools and Windows SDK, and a local
MinHook source checkout. Run from PowerShell:

```powershell
.\plugin\build.ps1 -MinHookDir 'C:\path\to\minhook'
```

Optional `-VisualStudioDir` selects another VS 2022 installation. Use an explicit
`-OutputDirectory` for a candidate build that must preserve earlier binaries;
the default remains `plugin/build`. Building over a DLL currently loaded by HOI4
is refused. Project C++ compiles with `/W4 /WX`.
The script builds
MinHook from source into the DLL, links the MSVC runtime statically, and runs the
18 field checks and 13 localization-boundary checks. Outputs are ignored by Git under `plugin/build/`:

- `rename_anything_country.dll`
- `rename_anything_injector.exe`
- `protocol_test.exe`
- `LICENSE.MinHook.txt` copied from the MinHook checkout used for this build

The MinHook license, including the bundled HDE notices, is preserved in
[LICENSE.MinHook.txt](LICENSE.MinHook.txt). Include this complete file with any
redistributed binary containing MinHook. Build warnings from MinHook's upstream
C code do not indicate warnings in the project's C++ sources.

## Run

1. Enable the scripted mod and start the supported game normally.
2. While at the main menu, find the process ID with `Get-Process hoi4`.
3. Load the DLL from this repository; do not copy it into the game installation:

```powershell
.\plugin\build\rename_anything_injector.exe 12345 "$PWD\plugin\build\rename_anything_country.dll"
```

4. A successful run prints `Country hooks initialized`. Then start or load the game.
5. In the mod's Country tab, check `DLL: ACTIVE (RN1)`, enter both fields, and Apply.

Run the injector once per process. Restart the game before updating or rebuilding
an already loaded DLL. A timed-out remote call reports uncertain state, leaves any
still-needed argument allocation until process exit, and requires a game restart
before retry. It never waits indefinitely or unloads a partially initialized DLL.

New source also exports the read-only `RNCountryCapabilities()` query. It returns
`0x524E4301` only after successful initialization, and zero before initialization
or after a rejected initialization. Module presence or a saved country flag is
insufficient. The isolated `plugin/build/country-capability` candidate passed
31 protocol checks and the wrong-host negative test. In PID42200 the isolated
five-field inline caller passed its live country transport: both names, all64
character slots, matching command/opening epoch and cap1 reached one native
constructor with independent storage. That caller checks the versioned export
twice before dispatch; the game log observes its marker, not a separate export
trace. This is log-only acceptance, without a real inline rename or game save.
The separate render candidate B20FBE88 subsequently passed first-open rendering
of all five fields in PID42556, with unchanged35/69/70-record transport and160
compared character slots. Cancel restored current names and the error log stayed
593→593. Autofocus/select-all and real inline Apply remain separate pending tests.
See [the audit](../docs/evidence/session-20261011/day2-unified-country-transport-audit.json).
Earlier accepted binaries do not include this export. The query does not write
to saves or establish multiplayer compatibility.

Initialization error codes: 10 unsupported executable hash, 11 wrong PE machine/header,
12 missing `.text`, 20 wrong/ambiguous signature, 30 MinHook initialization failure,
31 hook construction failure, 32 hook activation failure. The injector also reports
process access, DLL loading/export, and remote-thread failures.

If activation fails after enabling only some hooks, the DLL disables them best-effort
and retains their trampolines until process exit so an in-flight callback remains valid.
Any residual hook forwards to vanilla while initialization is inactive. Restart after
any failure. Thread notifications remain enabled for the statically linked MSVC runtime.

## Persistence, multiplayer and limits

The payload is held in the game's own cosmetic tag, with no external text sidecar.
Only the exact base key, `_DEF` and `_ADJ` are intercepted; `_ADJ` supplies the adjective.
Ideology-specific country lookups fall back to these base forms through the vanilla
country-name resolver. All other suffixes fall through to vanilla, including decision
titles/descriptions that also probe country cosmetic-tag prefixes. The supported UI
permits 32 Unicode codepoints in each field, using
the alphabet documented by the scripted mod. The native parser validates UTF-8 and
rejects delimiters, control characters and script markup. It accepts the Polish letters.

The scripted GUI transports the payload through the game's normal commands and
save mechanism. Actual multiplayer synchronization must be established by a two-client
test; the architecture alone is not evidence that multiplayer works. See
`../docs/country-re-findings.md` and `../docs/test-report.md` for observed results and gaps.
Every client needs the identical mod and native component for consistent country text.
Do not advertise ironman/achievement compatibility; it has not been established.
Workshop can distribute the scripted mod; this native component requires explicit
injection on each Windows client. Steam Workshop generally supports DLL content,
but acceptance by HOI4's uploader has not been tested. Subscription alone does not
load this DLL. See the [Workshop research](../docs/workshop-native-research.md) for
policy sources, EU4/CK2 precedents, and reported compatibility/antivirus problems.
Other executable builds are refused.

Country Apply replaces the current cosmetic tag. Reset drops the tag rather than
recovering an earlier cosmetic tag; other mods that use cosmetic tags may conflict.
With the DLL loaded before loading the save, a full game restart preserved the
custom country name on the map, in diplomacy and in its tooltip without another
Apply (PID 42732, Polish UI). A later independent own Apply test also showed
Polska QA, adjective próbny and leader Jan Żółć in the original news.350 event
(PID 22876, Polish UI); see the [evidence](../docs/evidence/qa-20261011-day2/47-vanilla-news350-country-adjective-leader.jpg)
and [report](../docs/test-report.md).
Without the native component, retaining the RN1 tag and falling back to original
localization remains the expected behavior pending the uninstall/load check.

## Uninstall and observation

Reset the country in game and save if the payload should be removed. Close the game,
stop using the injector, and disable/delete the scripted mod according to its README.
The DLL is released at process exit; there is no hot-unload operation. Delete the
local `plugin/build/` outputs if no longer needed. No game-installation repair is required.

For a guarded observational probe, use Python with Frida installed:

```powershell
python plugin/tools/observe.py --pid 12345 --seconds 120 --out docs/evidence/observe.log
```

The host verifies the exact image hash before attaching. `verify.js` requires that
host guard and refuses moved/ambiguous signatures. Frida instrumentation changes
instructions temporarily; this probe performs no gameplay-state or on-disk writes.
The first attach attempt in this session did not complete; see the finding ledger.
