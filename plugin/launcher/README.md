# Rename Anything launcher development candidate

This is an isolated Windows x64 launcher candidate. Its normal launch path has not
been tested with HOI4. It is not a release package and does not enable or install
the mod. Multiplayer and product integration remain pending.

The candidate is our own implementation. No reference repository code is copied.
The two bundled native modules contain MinHook; preserve `LICENSE.MinHook.txt` in
any eventual distribution and complete the project's distribution review.

## Intended use after independent review and runtime acceptance

Extract the complete folder into a writable user directory. Double-click
`RenameAnythingLauncher.exe`, choose the installed `hoi4.exe` once, and the launcher
remembers its path in `%LOCALAPPDATA%\RenameAnything\hoi4.path`. The existing mod
must already be enabled in the user's normal materialized playset. The launcher
starts the selected game directly with its normal working directory and no extra
game arguments. It does not change launcher/playset configuration, saves, or game
files. Steam redirection to another process is unsupported and stops injection.

The launcher requires the exact supported HOI4 binary and four fixed bundled
binaries. It starts a fresh process, then the country injector, then the editor
injector. Each helper must naturally finish with exit code zero and the original
fresh game must still pass identity checks before proceeding. Existing HOI4
processes are refused; there is no attach, arbitrary PID, DLL, command, elevation,
or process termination option. No Visual Studio, Python, PowerShell, or developer
runtime is needed by the prebuilt `/MT` launcher.

## Fixed development bundle

`build.ps1` copies these accepted inputs into its own output's `runtime` folder;
it does not rebuild or modify them. The manifest is generated from their complete
SHA256 values and compiled into the launcher.

| Input | Accepted SHA256 prefix |
| --- | --- |
| Country DLL | `18841A6D7552224F` |
| Country injector | `DF8D60E9F1C40319` |
| Render editor DLL | `B20FBE88756F7F22` |
| Render editor injector | `31B510BECD5DCBA3` |

The fixed editor is the render candidate, without autofocus or Enter dispatch.
Accepted gameplay tests currently cover the own POL/neutrality QA fixture; this
launcher does not broaden the native module's accepted feature scope. The mod
itself is not included in this development bundle.

## Failure and file lifetime

The selected game and all four bundle files are opened read-only with sharing
that denies writes and deletion. SHA256 is read through the same pinned handles.
Final path and file identity are checked again before child creation; the game
process additionally retains its own creation time, process handle, and image
identity. Only the two fixed helpers may run. Each inherits an explicit handle
list containing five duplicated file pins and its diagnostic pipe, never the
game process handle or other launcher handles.

After any failure or uncertain result following game creation, one message tells
the user to close the game before another attempt. The game remains open. The
launcher then becomes a quiet guardian retaining its process and file handles
until both its own game and every already-created helper naturally exit. It does
not retry, unload a module, kill anything, or start the remaining helper. This
also covers a helper's internal remote-thread timeout and an outer helper timeout.
The guardian can remain indefinitely if an already-created helper hangs; this is
an explicit safety limitation. Closing or killing the guardian externally defeats
that lifetime guarantee. Successful completion of both helpers releases pins.

Startup uses `WaitForInputIdle` with a 120-second bound. This is not proof of main
menu readiness. Each helper has a 90-second outer deadline. Real startup timing,
direct Steam game launch, normal playset loading, helper success, and the failure
guardian all require future controlled runtime QA before release.

## Read-only CLI

```text
RenameAnythingLauncher.exe --help
RenameAnythingLauncher.exe --game C:\absolute\path\hoi4.exe --verify-only
```

Both modes are noninteractive. Verification opens/hashes files only: no game or
helper process, dialog, process inventory, preference read/write, log creation,
writable-directory probe, or guardian. A missing/modified host or bundle returns
nonzero. Exit codes: `0` success/cancel, `2` invalid CLI, `10` unsupported host,
`20` invalid bundle/files, `30` game/startup failure, `40` helper failure,
`50` unexpected error. Normal `--game ABS` is a launch request and is not a test.

## Isolated development build

Run `plugin/launcher/build.ps1` using PowerShell 7 and Visual Studio x64 C++ tools.
It writes only `plugin/build/launcher-candidate`, compiles with C++20 `/MT /W4 /WX`,
runs component tests and read-only CLI negative cases, and checks protected source
and accepted binary hashes before/after. It never starts HOI4 or an injector.
`build-review.json` records the exact inputs, output/source hashes and CLI results.

Component tests exercise Windows argv round trips, same-handle hashing, real
read-only file pins and replacement rejection, bounded PE parsing and fail-closed
stage policy. They do not claim runtime injection, UI, playset or guardian tests.
