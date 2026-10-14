# plugin/: native code

| Folder | What it is |
|---|---|
| [`capie/`](capie/README.md) | CAPIE, the native library, its launcher and proxy loader. This is the current code. |
| `src/`, `build.ps1`, `tests/` | The pre-CAPIE country name DLL (`rename_anything_country.dll`) and its injector, described below. Kept as the reference that CAPIE was checked against. |
| [`launcher/`](launcher/README.md) | The first launcher candidate, kept as a reference. The current launcher is in `capie/src/launcher/`. |
| [`probes/`](probes/README.md) | Research probes: bridges, loggers and observers used to find and test the hooks. Not shipped. |
| `tools/` | Frida observation scripts for research (`observe.py`, `verify.js`). |
| `NativeBuild.psm1` | Build steps shared by the build scripts: game DLL guard, Visual Studio environment, MinHook, compiler flags. Tests: `tests/NativeBuild.Tests.ps1`. |
| `LICENSE.MinHook.txt` | The MinHook license, including the bundled HDE notices. Ship it with every binary that contains MinHook. |

Builds go to `plugin/build/`, which Git ignores.

## The pre-CAPIE country name DLL

This was the first native component: runtime country names for the scripted mod, before CAPIE
existed. CAPIE 1.0 (`capie_core.dll`) took over its behaviour unchanged in stage 2, and CAPIE 1.2
replaced its storage with the country name layer.

It reads a name and adjective that the mod stores in a vanilla cosmetic tag, `RN1|Name|Adjective|`,
and supplies them to the game's localisation lookups. The game then updates its own name caches
and map labels; the DLL writes no `CCountry` field and no localisation file. Only the exact base
key, `_DEF` and `_ADJ` are answered; ideology-specific lookups fall back to these through the
game's own resolver, and every other suffix (decision titles and descriptions among them) goes to
the original localisation. The parser accepts up to 32 Unicode code points per field, validates
UTF-8 and rejects delimiters, control characters and script markup.

Only `hoi4.exe` 1.19.3.0 with SHA-256
`7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5` is accepted. Initialization
also requires exactly one match of each of three signatures at its expected RVA inside the loaded
`.text` section; any mismatch refuses initialization. Both the injector and the DLL check the
executable's hash. All patches are made in process memory through MinHook; no file in the game
folder changes, `steam_api64.dll` included.

### Build

Visual Studio 2022 with the C++ x64 tools and a Windows SDK, and a local MinHook checkout:

```powershell
.\plugin\build.ps1 -MinHookDir 'C:\path\to\minhook'
```

`-VisualStudioDir` selects another Visual Studio 2022 installation. `-OutputDirectory` builds a
candidate without overwriting earlier binaries (default `plugin/build`). Building over a DLL that
HOI4 has loaded is refused. Project C++ compiles with `/W4 /WX`; warnings from MinHook's upstream C
code are not warnings in the project's sources. MinHook is built from source into the DLL and the
MSVC runtime is linked statically. The build runs 18 field checks and 13 localisation boundary
checks and writes `rename_anything_country.dll`, `rename_anything_injector.exe`,
`protocol_test.exe` and a copy of the MinHook license.

### Run (development only)

1. Enable the scripted mod and start the supported game normally.
2. At the main menu, find the process ID with `Get-Process hoi4`.
3. Load the DLL from the repository; never copy it into the game folder:

   ```powershell
   .\plugin\build\rename_anything_injector.exe 12345 "$PWD\plugin\build\rename_anything_country.dll"
   ```

4. A successful run prints `Country hooks initialized`. Then start or load a campaign.

Run the injector once per game process, and restart the game before loading a rebuilt DLL. A
timed-out remote call reports an uncertain state, keeps any argument allocation still in use until
the process exits, and needs a game restart before a retry. The injector never waits forever and
never unloads a partly initialized DLL.

`RNCountryCapabilities()` is a read-only export that returns `0x524E4301` only after a successful
initialization and zero otherwise. A loaded module or a flag in the save is not proof that the
DLL is active; the editors check this export before a country Apply.

Initialization error codes: 10 unsupported executable hash, 11 wrong PE machine or header, 12
missing `.text`, 20 wrong or ambiguous signature, 30 MinHook initialization failure, 31 hook
construction failure, 32 hook activation failure. The injector also reports process access, DLL
loading and export, and remote thread failures. If activation fails after only some hooks were
enabled, the DLL disables them as far as it can and keeps their trampolines until the process
exits, so a callback already running stays valid; a remaining hook forwards to vanilla while
initialization is inactive. Restart the game after any failure.

### Limits

- The name lives in the game's own cosmetic tag. Apply replaces the current cosmetic tag, which
  also replaces the flag and colour, and reset drops the tag without restoring an earlier one.
  Other mods that use cosmetic tags can conflict. This is why CAPIE 1.2 moved the name into its
  own layer ([ADR-0028](../docs/adr/0028-custom-name-layer.md)).
- With the DLL loaded before a save is loaded, a custom name survived a full restart on the map,
  in diplomacy and in tooltips, and showed in the vanilla event `news.350`. Without the DLL the
  save keeps the `RN1` tag and the game shows the original name.
- Every client would need the same mod and the same native component to show the same text.
  Multiplayer was never tested. Ironman and achievements are not supported.
- Steam Workshop can carry DLL files in general, but subscribing does not load this DLL, and
  whether HOI4's uploader accepts it was not tested
  ([Workshop and native code](../docs/research/workshop-native-code.md)).

Results and gaps: [in-game test record](../docs/contributing/in-game-test-record.md) and
[country name research](../docs/research/country-re-findings.md).

### Uninstall

Reset the country in the game and save if the name should go. Close the game; the DLL is released
when the process ends and cannot be unloaded while it runs. Delete `plugin/build/` if you no
longer need it. Nothing in the game installation needs repairing.

### Observation probe

For a guarded observation run, with Python and Frida installed:

```powershell
python plugin/tools/observe.py --pid 12345 --seconds 120 --out docs/evidence/observe.log
```

The host verifies the exact image hash before it attaches, and `verify.js` refuses moved or
ambiguous signatures. Frida changes instructions temporarily; the probe writes no game state and
nothing to disk except its log.
