# Development macOS child launcher

This is an unsigned x86_64 development tool for Paradox's staged executable slot.
It is not a supported installer or a production-ready launch path. It never starts
Paradox itself; the staged-settings parent chain is a separate requirement.

Build from the repository root:

```sh
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror -Wno-deprecated-declarations \
  plugin/capie/src/launcher/macos_child_main.cpp -o /tmp/capie-dev-child-launcher
clang++ -std=c++20 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_launcher/policy_test.cpp -o /tmp/capie-child-policy-test
/tmp/capie-child-policy-test
python3 plugin/capie/tests/macos_launcher/dev_child_test.py
```

Invocation format (not a request to launch the game):

```text
child [Paradox-prepended game arguments] --capie-dev-child GAME_ABSOLUTE GAME_CWD_ABSOLUTE DLC_LOAD_ABSOLUTE DYLIB_ABSOLUTE DYLIB_SHA256 -- [remaining game arguments]
```

The marker must occur exactly once and have five configuration values followed by `--`.
The configured executable becomes argv[0]; game arguments before the marker and after
the configuration delimiter are preserved in that order as separate execv arguments.
Only the marked configuration block is removed. Missing/duplicate/malformed markers
return64 before any child is started. This accommodates Paradox-prepended session/account
arguments without logging them. No shell quoting/reparsing and no argv/auth logging.
The launcher changes cwd to GAME_CWD before exec. An invalid invocation, failed chdir or
failed exec reports failure; it cannot run vanilla if the configured executable/cwd is unusable.

The strict existing Windows JSON reader was moved verbatim into `playset_json.hpp`.
Windows `playset_gate.hpp` includes it; Windows identity, verdict and file reader are
unchanged. The macOS adapter recognizes normalized `mod/capie.mod` and
`mod/ugc_3819929471.mod`, from `mod_identity.hpp`, shared with Windows `config.hpp`; there is no copied Workshop
ID in the macOS adapter. Reads are limited to1MiB and regular files;
missing/invalid/oversize/nonregular input disables injection. Duplicate enabled_mods,
non-string array entries and trailing garbage are rejected by the shared parser.

Injection additionally requires the pinned HOI4 SHA-256
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`
and the exact supplied64-character dylib SHA. A failed check runs without the configured
CAPIE library. Existing DYLD_INSERT_LIBRARIES and STEAM_DYLD_INSERT_LIBRARIES entries
are merged, preserving first-seen order and removing duplicates and the configured
CAPIE path and its currently resolvable same-device/inode aliases; that path is appended
only when all checks succeed. The Steam variable is
left unchanged. Existing foreign/Steam libraries are not treated as CAPIE and are retained.
A dylib path containing `:` cannot be injected by this colon-delimited mechanism.

Offline checks passed: pure-policy assertions (including marker validation and UTF-8) and5 process cases (missing, malformed,
disabled, enabled-but-wrong-game-build, oversized playset). Process tests exec only their
compiled recorder and check exact spaced argv, cwd and absence of CAPIE injection.
A controlled real-game launch through this tool loaded the read-only diagnostic dylib
on 2026-10-16; see [the diagnostic report](read-only-diagnostic.md). This used an explicit
temporary opt-in file, not a real mod playset. Separate explicit-switch fixtures
proved the Paradox→wrapper→marker child mechanism; they do not validate this tool's full gate.

Remaining production requirements: immutable bundle manifest/trusted installation paths,
hash-to-exec/dylib-loading race mitigation, process ancestry enforcement, and complete
runtime pin verification before hooks. Alias filtering is a filesystem snapshot, not a
race-free identity guarantee; missing or inaccessible files cannot be classified by inode.
An allocation/environment-construction failure currently aborts this development launcher
instead of attempting to rebuild an uncertain vanilla environment. The dylib hash is supplied
by the developer, not an independent trust root. A matching hash never substitutes for the
runtime library's fail-closed pin/ABI checks. No runtime hooks were changed here.

Fatal setup exit codes are64 (configuration),71 (environment/allocation),72 (cwd/exec).
Gate/hash rejection is different: it still execs the configured clean child. The whole
allocating launch path, including argv vector construction, is inside the exception boundary.
The regular-file descriptor owner is noncopyable to prevent double-close.

Extraction proof: the429-line original parser/normalizer block from Windows HEAD was
compared byte-for-byte with the new portable header and matched. Block SHA-256:
`3d77f2a3ed8f27834eedf35994f016d3f2f55b20d6b41a106cc7eceff2adad34`. Windows compile/runtime testing
remains separate; equality proves extraction preservation, not that platform builds passed.

macOS additionally validates UTF-8 across the whole JSON document before the unchanged
shared parser. Invalid leading/continuation bytes, overlong encodings, surrogate scalars,
values above U+10FFFF and truncation disable injection even in ignored fields. Tests
include invalid ignored content, valid Polish text and emoji. Windows parser semantics
remain unchanged; the earlier byte-for-byte extraction proof still holds.

The process fixture checks non-sensitive `--pdxfake nonsensitive` arguments prepended
before the marker and a spaced argument after the configuration block. Four malformed
configuration cases return64; no authentication value is logged or written.

## Alias rejection verified offline

The development child now removes inherited symlink and hardlink aliases of its configured
library by comparing `st_dev` and `st_ino`, in addition to exact path matching. Relative
insertion paths are resolved for this comparison against GAME_CWD, where the eventual
child runs. Foreign libraries keep their original spelling and order. This does not remove
independent copies of CAPIE with a different inode, inspect arbitrary library contents,
or undo libraries already loaded into the wrapper by its own parent.

TDD reproduced the disabled gate failure first: an own-library symlink inherited through
STEAM_DYLD_INSERT_LIBRARIES survived filtering and dyld aborted the recorder trying to load
the deliberately invalid fixture file. After the fix, all three alias cases (absolute
symlink, absolute hardlink, relative hardlink) launch the clean recorder. A fourth case
loads a distinct foreign dylib with the same basename while removing both own aliases;
the foreign constructor marker proves it actually loaded. All tests use temporary files,
not the game or launcher. The process suite now covers 9 successful recorder launches
plus 4 rejected configurations. Policy tests and warning-clean builds passed on arm64 and
x86_64; process execution was x86_64 under Rosetta.

## Smallest production path and shared release boundary

The child remains only one component. A minimal complete installation needs:

1. A user-owned versioned CAPIE directory outside the game bundle, with reviewed runtime
   binaries, child launcher and a verified release manifest. Developer-supplied dylib
   hashes are not an independent trust root. Installer updates must stage and validate
   a complete version before switching, and uninstall must remove only owned artifacts.
2. A parent entry point that stages launch settings outside the game bundle while keeping
   the real HOI4 game identity, asset paths and existing user-data/playset location. It
   must preserve the normal Paradox selection workflow, not create a fixture playset or
   reconstruct a guessed mod list. Startup must handle an already-running launcher and
   concurrency; the existing isolated fixture does not establish those behaviors.
3. A child selected by that parent which reads the final real dlc_load.json after Play,
   verifies ancestry/artifact identity, preserves argument boundaries and Steam libraries,
   restores cwd, and gates only CAPIE. Disabled/malformed/mismatched inputs must still
   reach the original game when executable/cwd are usable. The real-playset on/off path,
   concurrent changes and hash-to-exec/load identity are still unproven here.
4. A runtime that independently checks its exact image and all pins before activation,
   with proven hook lifecycle and API/script behavior. The diagnostic dylib establishes
   static checks only; its successful real-game launch is not acceptance of this installer.
5. One CAPIE version/tag and one GitHub release containing complete Windows and macOS
   platform assets, their hashes/manifests and platform-specific acceptance results.
   `tools/release/release.toml` currently builds only `CAPIE-{version}-win64.zip` and
   reviews Windows DLL/EXE files. Before publication the pipeline must require both
   platform inputs, reject version/API mismatch and missing acceptance, and keep the
   release draft until both are ready. Do not ship the diagnostic as the macOS runtime.

No release pipeline, common Windows parser, installed game files or real playset were
changed by this loader validation work. Windows build/acceptance remains a separate
required check, not inferred from macOS policy tests.

## External settings generator and real-playset recorder

`tools/stage-macos-child.py GAME CHILD DYLIB SHA256 NEW_STAGING_DIRECTORY` prepares
settings only; it never executes dowser, Paradox or HOI4. The source is the installed
GAME/launcher-settings.json, bounded to 1 MiB with duplicate keys rejected. Staging
must be a new directory outside both the game and game-data directories. No source
settings or playset file is written.

For the observed Steam HOI4 1.19.3.0 configuration, `gameDataPath` is
`~/Documents/Paradox Interactive/Hearts of Iron IV`. The generator expands that one
value against the current user's home, stores the absolute result as gameDataPath,
and derives the child's DLC_LOAD argument by appending `dlc_load.json`. It does not
copy a playset into staging. The normal and debug executable entries both receive
the same child configuration, retaining their original argv and translated labels.
Every alternative must identify the same real HOI4 executable or staging fails;
none is silently discarded. DLC, theme and settings-layout resource paths become
absolute paths to the original game directory. Game identity, Steam distribution,
ingameSettingsPath, gameCachePaths and remaining fields are preserved.

Offline verification:

```sh
python3 plugin/capie/tests/macos_launcher/staging_test.py
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_launcher/playset_recorder.cpp -o /tmp/capie-playset-recorder
```

Four unittest cases check normal/debug routing, preserved data/resources/arguments,
rejection of unsupported alternatives or ambiguous input, and refusal to overwrite
an existing destination. The recorder was separately run with a temporary fake HOME
and a synthetic enabled playset: all four report booleans were true. It never execs
any child or loads a dylib. It reads only the configured path when that path equals
HOME/Documents/Paradox Interactive/Hearts of Iron IV/dlc_load.json and creates
`/tmp/capie-playset-proof-PID.json` exclusively, mode 0600. Reports contain only
same_real_playset_path, readable, valid and capie_enabled; no mod names or argv.

A controlled launcher test can stage CHILD=/tmp/capie-playset-recorder and an unused
fixture dylib path/hash, then start the installed launcher with both `--pdxlGameDir`
and `--gameDir` pointing to the prepared directory. Do not use a fixture gameId or
pdxlDataSuffix for this real-playset test. Leave the current selection unchanged and
activate Play once; the recorder replaces the game. The launcher may itself rewrite
dlc_load.json normally on Play, so this is not a promise that its bytes never change.
The agent implementing staging has not performed that UI action. A successful
recorder result will establish the child path/read boundary, not hook activation,
production ancestry checks, or the game's subsequent behavior.

This Python tool is a development staging component, not a shipped Python dependency
or complete installer. Production parent execution, running-launcher coordination and
trusted release artifact selection remain separate work.

### Real playset boundary observed (2026-10-16)

The installed Paradox Launcher 2026.12 was started with the generated external
settings and the real HOI4 identity/data directory. A fixture-only version label
`CAPIE RECORDER ONLY - no game process` visibly confirmed staging before Play.
The active selection remained unchanged (`Brak modów`). The launcher showed a
Steam communication warning; Ignore was selected. This test therefore does not
prove the normal Steam entry chain or overlay/session integration.

Play launched recorder PID 78440 with parent PID 78298, the observed Paradox
Launcher process. The recorder reported:

```json
{"same_real_playset_path":true,"readable":true,"valid":true,"capie_enabled":false}
```

The real dlc_load.json SHA-256 was identical before and after:
`b28a99338a45655c4a25cfee44602a56451960142c9dd9e767b78c21a08c91bb`.
Its mtime_ns changed from `1792180641700575201` to `1792182952462616361`, consistent
with the launcher rewriting identical contents. No mod selection was changed and
no mod-list contents were recorded. The fixture additionally emits numeric PID/PPID
in its own `/tmp/capie-playset-parent-PID.txt` to retain the ancestry observation.
The launcher closed itself after the recorder exited. The pre-existing HOI4 process
PID 66226 was still present in the immediate post-test process check; it was not
closed or restarted by this test.

This confirms real-data-path delivery and the disabled playset result through the
actual launcher. Enabled CAPIE selection, alternate/debug Play, normal Steam startup,
concurrent launch behavior and runtime activation remain separate acceptance items.

### Development parent command

`python3 tools/launch-macos-child.py GAME CHILD DYLIB SHA256 NEW_STAGING_DIRECTORY`
uses the same staging contract, then execs the original GAME/dowser with separate
`--pdxlGameDir` and `--gameDir` arguments. It sets cwd to the original game directory
and inherits the environment unchanged, including Steam values. It refuses an observed
running Paradox Launcher instead of stopping that process or delivering settings to an
unknown existing instance. Process-list failure also aborts before staging/launch.

`python3 plugin/capie/tests/macos_launcher/parent_test.py` passes four tests: command
boundaries, existing-launcher refusal, distinction from a running game, and an actual
parent exec of an owned fake dowser recording argv/cwd/SteamAppId. No real dowser was
executed by this parent test. Combined staging and parent tests: 8 unittest cases.

This remains a development entry point requiring Python. The process snapshot is not
an atomic exclusion mechanism; a launcher may start immediately afterward. A production
parent still needs installation-owned concurrency control, trusted executable identity,
standalone packaging and actual Steam→dowser acceptance. The successful recorder UI test
used the installed launcher directly; it does not prove the new parent chain end to end.

## Native parent and child build (no Python runtime)

```sh
tools/build-macos-launcher.sh /absolute/new-private-output
python3 plugin/capie/tests/macos_launcher/native_process_test.py
```

The build creates unsigned x86_64 `capie-parent` and `capie-child`. The parent is
Objective-C++/C++20 linked to Apple's Foundation; the child remains C++20. Python
is used only by the test harness, not by either launcher binary. Invocation:

```text
capie-parent GAME CHILD DYLIB SHA256 NEW_STAGING_DIRECTORY
```

`macos_child_staging.hpp` implements the tested staging policy with Foundation
NSJSONSerialization, preserving unknown JSON values. Because Foundation accepts
repeated object keys, a scanner runs only after Foundation validates the grammar
and rejects repeated decoded keys within each object (including escaped spellings).
It does not replace the system JSON grammar parser. Input must be UTF-8 and at most
1 MiB. The same constraints apply to all alternative executable entries; malformed
or unsupported input rejects the complete configuration before creating staging.

`macos_child_parent.mm` reads only a bounded regular settings file, checks currently
visible launcher processes through libproc, creates a new private staging directory
outside the canonical game/data directories, writes settings exclusively with mode
0600, then changes cwd and execs original dowser without a shell. No copied dowser,
Python interpreter or game-bundle modification is needed. An incomplete staging
artifact is retained on write failure; the launcher is not started. Exit codes:
64 invalid configuration, 71 preparation, 72 cwd/exec, 73 observed instance or failed
enumeration. A missing executable/cwd cannot truthfully fall back to vanilla.

Native policy tests: 26 checks each on arm64 and x86_64. Native process tests exercise
one successful fake-dowser exec (argv/cwd/SteamAppId and real-data-path mapping),
existing-stage refusal, and three invalid-settings refusals. The fake dowser is owned
by the test and never launches a GUI/game. Builds use `-Wall -Wextra -Werror`.

The native entry point removes the Python runtime dependency; it does not remove
remaining release/activation requirements. libproc enumeration is a snapshot, not
an atomic lock against launcher startup, and individual inaccessible process paths
cannot establish absence. Artifact manifest trust, update/uninstall ownership,
complete concurrency handling and the native Steam→dowser end-to-end test remain
required before claiming a supported installer. The earlier successful real-playset
UI observation used a recorder with directly started Paradox, not this native parent.

## Optional bootstrap/runtime pair

The native parent accepts an optional trailing configuration triple:

```sh
capie-parent GAME_DIR CHILD BOOTSTRAP BOOTSTRAP_SHA256 NEW_STAGE_DIR \
  --capie-runtime RUNTIME RUNTIME_SHA256
```

The Python development staging/parent tools accept the same trailing option.
The generated child configuration places it **before** the configuration delimiter:

```text
child [launcher game arguments] --capie-dev-child GAME GAME_CWD DLC_LOAD \
  BOOTSTRAP BOOTSTRAP_SHA256 --capie-runtime RUNTIME RUNTIME_SHA256 \
  -- [original game arguments]
```

A genuine game argument named `--capie-runtime` after that delimiter is preserved.
Without the option, the existing single-library mode remains unchanged. All supported
alternative executable modes receive the same pair and retain their own game arguments.
Both library paths must be absolute and distinct; staging validates lowercase SHA-256
syntax. The child hashes the exact pinned game and both library files before admitting
injection. It also refuses a pair whose paths resolve to the same device/inode.

In paired mode, the child constructs `DYLD_INSERT_LIBRARIES` in this order:

1. Configured bootstrap.
2. Retained foreign/Steam entries, deduplicated in their existing order.
3. Configured runtime.

The child removes both configured paths and their resolvable symlink/hard-link aliases
before rebuilding the list. Relative inherited aliases resolve against the game working
directory. A disabled, missing, invalid or oversized playset, or any artifact mismatch,
removes **both** owned libraries and launches without the pair. Foreign entries are
preserved, not trusted: bootstrap admission must independently verify the allowed loaded
image closure. An unknown foreign library is not made safe by appearing in this list.

The file reads, alias checks and hashes are snapshots. They do not prevent concurrent
replacement before `exec`/dyld loading. This interface alone supplies no immutable bundle,
loaded-image integrity proof, startup quiescence proof or API-level guarantee. The parent
stages paths/hashes; it does not validate the bootstrap/runtime implementation. Actual
paired loading into HOI4 has not been tested by this launcher fixture suite.

Offline checks cover pair ordering and hash policy, malformed/truncated pair configuration,
preservation of a genuine game `--capie-runtime` argument, removal of both owned aliases
when disabled or when the game hash mismatches, and native paired staging. A separate
case uses a read-only disabled playset and checks its bytes, mtime and mode remain unchanged.
The native parent process fixture also checks that legacy and paired staging leave the
source launcher settings bytes and mtime unchanged. These checks execute only owned
recorders/fake dowser binaries; they never start HOI4 or Paradox.

Review scope: this paired-path review was performed by its author and is a self-review,
not an independent review. No new blocker was found in delimiter bounds, pair removal,
ordering, game argument forwarding or staging source preservation. The snapshot limitation
above remains explicit; a separate reviewer is still needed before production activation.
