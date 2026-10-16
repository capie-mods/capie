# macOS loader: external settings staging investigation

Read-only static investigation, 2026-10-16. Neither dowser, installer nor game was executed.
No game files or launcher settings were modified.

## Finding

The shipped dowser **does accept an absolute external directory through
`--pdxlGameDir PATH` and forwards it to the launcher**. This makes external staging
plausible, but does not yet prove that the launcher accepts a staged `exePath`, starts
its wrapper, or preserves the real game's asset and working directories. It is a
candidate architecture, not a supported installation procedure.

Analyzed artifact: Steam HOI4 `dowser`, Mach-O x86_64, SHA-256
`09c1a0f8726ac58128eaa57590f5d5ec20e55fc33cde03b66c0894332323f685`.
Addresses below are unslid VAs in dowser, not in hoi4. Its Go symbols are present.

## Evidence

| Function / observation | Instruction evidence | Result |
|---|---|---|
| `arg.GetForwardingArgs` at `0x4293f30` | calls filter at `0x4293fa5`, directory resolver at `0x4293fdb` | input args are filtered, then directory is normalized |
| `arg.filterGameDirFromArgs` at `0x4294460` | `0x4294524` loads string VA `0x4342874`, length13 at `0x4294530`; `strings.Index` call `0x4294539`, branch `0x4294544` | recognizes `--pdxlGameDir` |
| Separate value | `0x4294bca` / `0x4294bcf` load next Go string's length/pointer; saved `0x4294be0` / `0x4294be5`; return fields `0x4294610` / `0x4294618` | retains the next argument as the forwarding directory, removing option and value from forwarded remainder |
| `arg.getForwardingGameDir` at `0x42942c0` | `0x4294331` loads `/` at `0x434034c`; one-byte `memequal` at `0x4294346`; successful branch returns original string at `0x4294352`–`0x429437d` | absolute paths are preserved |
| Relative/default directory | `cfg.GetAppExecutablePath` call `0x42942e1`; relative branch `filepath.join` at `0x42943ce`; empty branch `0x42943ff` | relative input is joined to executable directory; absent input uses executable directory |
| Output argument template | Go string headers at `0x43aef60`,`0x43aef70`,`0x43aef80`,`0x43aef90`: `--pdxlGameDir`, empty, `--gameDir`, empty. Copied `0x429406d`–`0x4294099`; normalized directory stored into both values `0x42940a2`/`0x42940b8`, `0x42940bc`/`0x42940cd` | output starts `--pdxlGameDir PATH --gameDir PATH`, followed by retained args |
| Legacy input `--gameDir` | literals `0x4341a4d` (`--gameDir=`), `0x43415e5` (`--gameDir`), log header `0x43a3010`: `Passed --gameDir - ignoring its value: ` | this input is explicitly ignored, not the supported override |
| Launcher process handoff | `main.checkAndStartBootstrapper` calls GetForwardingArgs at `0x42976b2`, calls execCommand at `0x42977a2`, Cmd.Start at `0x42977b5` | forwarding result is on actual bootstrapper-start path |

The known game's `launcher-settings.json` has gameId `hoi4`, distPlatform `steam`,
`exePath: ./hoi4.app/Contents/MacOS/hoi4`, argument `-gdpr-compliant`, a separate debug
alternative executable, and relative DLC/theme/settings-layout paths. A staging design
must account for all these relative paths, not only change exePath.

The expected `~/.paradoxlauncher` and `~/Library/Application Support/Paradox Interactive`
directories were absent. A filename search under Library found no app.asar / launcherpath
artifact. This is not proof that no launcher exists elsewhere. The shipped launcher installer
is a separate Mach-O executable; it was not run.

## Candidate design and remaining proof

1. A CAPIE front wrapper invokes the real dowser with `--pdxlGameDir` and an absolute,
   user-owned staging directory. It must not set DYLD injection for dowser/launcher.
   Staged settings and referenced resources must preserve HOI4 identity and real paths.
2. Paradox Launcher presents the normal playset UI and writes the normal `dlc_load.json`.
   Its selected executable would be a separate CAPIE child wrapper. **Acceptance of staged
   settings, executable override and working-directory behavior remains unproven.**
3. The child wrapper verifies the exact game hash, CAPIE playset opt-in and pin manifest;
   only then sets DYLD_INSERT_LIBRARIES immediately before exec of the real game. Any
   missing/mismatched input launches vanilla. This is consistent in intent with ADR-0004
   and ADR-0030, but their process ancestry/playset requirements require runtime checks.
4. Next evidence needed: inspect the installed launcher/bootstrapper's settings loader
   and spawn path, then test staging against a harmless recording executable before any
   game launch. Check main/debug launch, spaces, Steam identity, asset paths, active playset,
   child ancestry, loader environment, disabled/uninstalled fallback and concurrent launches.

No modification of the game's on-disk executable, original settings, or signed dowser is
required by this hypothesis. Do not publish installation instructions claiming it works
until the launcher-side and runtime gaps above are closed.

## Shipped launcher recovered without installation

Installer SHA-256 `70c4b70b4e29404d23a59d4fdc4dac8023cfe5b90526faa6577fcc515342cb38`.
It does not contain a directly readable complete ZIP: Go bindata wraps ZIP data in gzip.
The `_resourcesLauncherV2Zip` slice header at VA `0x16fffc10` points to VA `0x0d8c5b80`,
length158569779. Gzip decoding yields a158803385-byte ZIP with379 entries. The bootstrapper
slice at `0x16fffbd0` yields a different13-entry ZIP without app.asar. These were decoded
as data; neither installer nor launcher code was executed.

Recovered member `Paradox Launcher.app/Contents/Resources/app.asar` (37843006 bytes) SHA-256:
`a636f55fb42c071ade76fd1c75ec02c9f64f278a652e59bf8c44a322a40650f3`.
Temporary copy: `/tmp/capie-launcher-research/x64.asar`; extraction provenance JSON is next
to it. Full x64 application is now under `/tmp/capie-launcher-research/extracted/Paradox Launcher.app`.
All379 entries and14 internal symlinks were extracted with traversal/escape checks. Existing
signatures were retained. Read-only codesign inspection reports hardened runtime and
Paradox Team ID QCZVRHRS7Y. It was not registered or installed system-wide.

REA static analysis parent evidence:
`ev_468137449055606a34bd7dc645d79e069f035db1b28af46f08a49e25f0250af9`
(package.json and main entrypoints), and
`ev_79ad74ff23f6fa9ac18891353c277a02e7d033b2e9e240eda1db6a35d4d4a9b2`
(bytecode loader). Both are deliberately partial source selections. The entrypoint is
`dist/main/index.js`: it requires `./bytecode-loader.cjs`, then `./index.jsc`.
The worker uses the same pattern. Main implementation is8282512 bytes of V8 cached bytecode,
not a JavaScript source bundle. The2308-byte loader source is available as
`/tmp/capie-launcher-research/bytecode-loader.cjs`. It reads cached data, replaces its flag
hash from dummy V8 data, synthesizes a dummy source of matching length, constructs vm.Script,
then calls runInThisContext and applies the returned wrapper. **Do not require/import this
loader to inspect it: that executes target code.** REA's available static JavaScript tools
do not establish bytecode control/data flow.

Bounded strings in index.jsc contain `pdxlGameDir` with help "Path to directory containing
launcher-settings.json", launcher settings validation, `_getExeFilePath`, `_getExeArgs`,
`spawnExecutable`, `exeDirName`, `spawn`, `shell`, `SteamAppId`, `DYLD_INSERT_LIBRARIES`, and
`STEAM_DYLD_INSERT_LIBRARIES`. They support selecting the next probe but **do not prove**
exePath acceptance, cwd, argument quoting or environment behavior. Those remain open.

## Prepared isolated probe (not executed)

`/usr/bin/sandbox-exec` is present. Prepared files under
`/tmp/capie-launcher-research/probe`:

- `sandbox.sb`: default allow, deny all network, deny filesystem writes outside
  `/private/tmp/capie-launcher-research`. It preserves real HOME and blocks writes there.
- `staging/launcher-settings.json`: gameId `capie_fixture`, platform `pdx`, temporary
  gameDataPath, `exePath: ./recorder`, spaced argument and alternate recorder launch.
- `staging/recorder`: compiled native harmless recorder; source `recorder.c`. It only
  writes record.txt with argv/cwd/parent PID and five selected environment values.
- `electron-data` and `game-data`: dedicated temporary data directories.

Proposed direct launcher command (not run):

```sh
/usr/bin/sandbox-exec -f /tmp/capie-launcher-research/probe/sandbox.sb \
  '/tmp/capie-launcher-research/extracted/Paradox Launcher.app/Contents/MacOS/Paradox Launcher' \
  --user-data-dir=/tmp/capie-launcher-research/probe/electron-data \
  --pdxlGameDir /tmp/capie-launcher-research/probe/staging \
  --pdxlDisableHardwareAcceleration
```

The application-specific pdxl flags are present in shipped strings. Electron user-data-dir
is a proposed isolation option; its effect in this build is not yet observed. No HOME,
CODEX_HOME, or DYLD variable is overridden. The sandbox blocks network even if launcher
attempts it; that may prevent UI startup and must be reported as an isolation limitation,
not bypassed silently. The fixture gameId/platform/schema may be rejected; do not replace
it with real HOI4 user data to make the test pass. The sandbox does not isolate reads,
Keychain/XPC requests, or system-service side effects; this is a filesystem/network probe,
not a full VM. A host policy may also reject nested sandbox use.

Before launcher execution, validate sandbox enforcement with harmless fixture commands:
one temporary write must succeed, one preselected new file outside that root must fail,
and a loopback TCP attempt must fail. These checks and the launcher have **not been run**.
Then inspect fixture loading and use the recorder launch controls. Evidence must include
actual command/argv/cwd, sandbox outcomes, exit code, recorder parent ancestry, and bounded
logs. A successful fixture proves launcher mechanics only; the separate Steam/dowser chain,
playset gate, DYLD injection and HOI4 behavior still need their own tests.

## Isolation failure follow-up (2026-10-16)

Parent investigator reports all three outer-sandbox enforcement probes passed. The first
launcher probe (PID41130) exited133 with Chromium nested-sandbox initialization failure
(Operation not permitted) and fatal GPU failure before renderer startup. This observation
concerns the probe environment, not rejection of the staged executable. It does not prove
that settings loaded successfully either. The next proposed experiment adds `--no-sandbox`
and `--disable-gpu` **inside the same enforced outer sandbox**, preserving the network and
filesystem write denials; it has not been executed by this investigator.

A bounded search of the recovered index.jsc found no raw `pdxlLogDir` or `pdxlUserData`
strings. Do not invent those options. Observed candidates/limitations:

| Literal | index.jsc byte offset / context | What it establishes |
|---|---|---|
| `pdxlDataSuffix` | `0x6cad59`; help says append suffix to launcher data folder | suffix option exists; no evidence of an absolute directory override |
| `LOGS_PATH` | `0x6cb7d6`, next to LOG_TIMESTAMPS | configuration-name candidate only; process.env handling not decoded |
| `chromium-data` | `0x6b1935`, near onReady, setPath and userData log messages | launcher may set Electron userData itself; command-line user-data-dir cannot yet be assumed authoritative |
| `pdxlEnableInProcessGpu` | help says enables Chromium in-process GPU switch | application-specific isolation experiment candidate; effect not observed |

Related log strings are "Setting 'userData' to" and "Failed to set userData, using default
location". Platform-path strings include APPDATA, LOCALAPPDATA, XDG_DATA_HOME,
XDG_CACHE_HOME, XDG_CONFIG_HOME and HOME alongside Darwin Library/Application Support.
These are not evidence that XDG overrides affect the Darwin branch. Preserve HOME and keep
outer-sandbox denials; blocked writes are useful evidence of attempted paths, not grounds to
redirect the user's real launcher state. Source inventory has no plain application .env
file that resolves this ambiguity.

## Controlled execution results (2026-10-16)

The external sandbox profile was tested: writing a new file inside the temporary
probe directory succeeded; writing a preselected absent file outside it raised
PermissionError; a loopback TCP connection raised PermissionError. HOME was not changed.

Three attempts used the extracted signed launcher and fixture game settings,
never HOI4 or its real user-data directory. First attempt (PID41130) reached a
native modal error; a one-second sample showed Node fatal-exception handling and
NSAlert.runModal. Chromium child sandbox initialization then failed with
Operation not permitted, followed by GPU-process failure; process exit133.
The UI tool could not read the alert (timeouts), so the JavaScript exception text
was not recovered. The exact sample is retained only in the temporary probe directory.

Second attempt (PID42488) added --no-sandbox and --disable-gpu inside the unchanged
external filesystem/network sandbox, with candidate LOGS_PATH pointing into the
temporary tree. It produced no recorder output and was terminated by the test.
Third attempt (PID43215) additionally requested a Node exception-monitor preload;
Electron explicitly rejected the NODE_OPTIONS preload in packaged-app mode. It was
terminated. No exception monitor or recorder result was obtained.

The doubtful assumption is that Electron --user-data-dir plus LOGS_PATH isolates
all launcher initialization writes. Shipped bytecode contains its own userData
setPath logic, and this has not been resolved. These failures neither prove nor
disprove staged exePath support. Further identical retries are not useful. A
separately controlled launcher profile or resolved initialization paths are required
before repeating this test. No native CAPIE loader is validated by these attempts.

## Live baseline supplied by the user, 2026-10-16

The user reported reaching the menu and started HOI4 through Steam. Read-only
inspection at 19:39 UTC found PID 52442, started 19:37:12 UTC, command `./hoi4`,
current PPID 1. REA process-observation evidence:
`ev_c48a9617c91074b8377ef3f900aa6312f0746655c4b00d43f5fd6e70cbc01023`.
This records `ps` output, not instrumentation or a capture of the game's startup.

`lsof` resolves the executable and working directory to the expected Steam
`hoi4.app/Contents/MacOS` directory. The file SHA-256 still matches the pinned
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.
Mapped files include the Rosetta runtime/AOT image, Steam's `steamloader.dylib`
and `gameoverlayrenderer.dylib`, and the game's `libsteam_api.dylib`.
This demonstrates those Steam libraries are present, not arbitrary CAPIE injection.

The installed launcher is now available under
`~/Library/Application Support/Paradox Interactive/launcher-v2.2024.14/Paradox Launcher.app`.
Bootstrapper logs at 19:35:47 UTC select this version via `/usr/bin/open`, with
`--pdxlGameDir` and `--gameDir` pointing to the real game directory. Although a
2026.12 directory exists, the bootstrapper rejects it as an invalid installation.
The launcher log at 19:37:12.232 UTC records launching
`./hoi4.app/Contents/MacOS/hoi4` and setting its working directory to the executable
directory. Authentication/session arguments are deliberately not copied here.
Current PPID 1 cannot reconstruct the historical parent chain by itself.

Installed 2024.14 `app.asar` SHA-256:
`e12388902f9d9eacd746c912e3fbf45de0f0d59d4206196bc7fefb50fcc3364e`.
Its `dist/main/index.js` loads `bytecode-loader.cjs` and `index.jsc`; it does not
provide unobfuscated main-process source. Previous tests of the separately extracted
installer payload must not be treated as observations of this installed session.

No debugger, hook, CAPIE library, launch-setting change or game restart was performed.
The observed working launch removes the missing-baseline uncertainty. Staged launch
redirection, final-playset gating, hook activation timing and CAPIE gameplay remain
unverified. Menu reachability is the user's report, not a captured UI assertion.

## Installed-launcher child-wrapper test (2026-10-16, 21:45–21:46 Warsaw)

After the user restarted the launcher, version 2026.12 was installed and running.
This supersedes the earlier observation of an invalid partial 2026.12 installation;
it does not invalidate the earlier 2024.14 recorder result.

An external staging directory supplied `launcher-settings.json`, gameId
`capie_fixture`, an isolated temporary game-data path and an absolute executable
path to our unsigned x86_64 child wrapper. `--pdxlDataSuffix capie-probe` created
`~/Library/Application Support/Paradox Interactive/launcher-v2-capie-probe`.
This separates application state but is not an OS sandbox; the launcher still
performed normal network/update activity. No HOI4 settings file was edited.
A running normal launcher prevented the initial staged invocation from presenting
its fixture. Closing that window and starting with the staged arguments presented
`CAPIE DYLD fixture only`; only that fixture's PLAY button was activated.

All three scenarios passed through the actual 2026.12 launcher UI:

| Input to wrapper | Child PID | Launcher parent PID | DYLD present | Marker library loaded |
|---|---:|---:|---:|---|
| `--enabled` | 56243 | 56190 | 1 | same PID 56243 |
| `--disabled` | 56357 | 56291 | 0 | no marker |
| `--invalid` | 56427 | 56385 | 0 | no marker |

Retained hashes and results: `launcher-child-results.json`. Fixture source:
`plugin/capie/tests/macos_launcher/`. The enabled wrapper sets DYLD only immediately
before exec of the recorder. This demonstrates a viable child-loading boundary
through the installed launcher, without re-signing it or altering game binaries.
The launcher chooses cwd from the executable's parent directory; a production
wrapper must explicitly restore HOI4's expected cwd before exec.

Important boundary: these modes are explicit fixture switches, NOT a tested JSON
playset gate. The wrapper does not preserve arbitrary game arguments or Steam's
existing insertion libraries and must not be installed as a production loader.
No CAPIE library or hook was loaded into HOI4. Safe hook installation, exact-build
runtime guards, final-playset parsing, Steam/dowser integration and game behavior
remain separate work. A UI capture error after the first PLAY coincided with the
launcher closing; success is established by the child and library PID records,
not the failed screenshot request.
