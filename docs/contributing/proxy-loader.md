# CAPIE proxy loading

The development loader is `plugin/capie/src/proxy/`, built with
`plugin/capie/build_proxy.ps1`. It adds `version.dll` beside `hoi4.exe`, forwards all 17
system exports by name and ordinal, and resolves the original from an absolute System32
path. The original game executable and Steam DLL are unchanged.

## Activation and installation

The bootstrap defaults to `-LoadMethod Proxy`. It installs the verified package under
`%LOCALAPPDATA%\RenameAnything\current\`, then copies `proxy\version.dll` into the discovered
game directory. A pre-existing foreign or modified `version.dll` is a conflict; it is never
overwritten or removed. `-LoadMethod Launcher` keeps the existing wrapper alternative.
The release URL, signing key and manifest remain unpublished placeholders, so there is no
public install command yet.

Activation is explicit:

- `-ProxyActivation Flag` (default): add the whole, case-sensitive argument `-capie`.
  Removing it disables CAPIE while the proxy continues forwarding Windows APIs.
- `-ProxyActivation Config`: bootstrap writes `%LOCALAPPDATA%\RenameAnything\proxy.enabled`
  with `CAPIE_PROXY_ENABLED_V1` and a newline. This is the fallback if Steam/Paradox drops
  the flag. It enables each subsequent game startup until removed or uninstalled.

Actual Steam → Paradox Launcher → game argument propagation is not established by the
game's import table or a direct-executable startup test. Choose the config fallback when
that path cannot pass the flag; do not claim that removing a Steam flag disables config mode.

## Startup and verification

`DllMain` starts a worker without waiting. Hashing, file access, payload mapping and
`RNInitialize` run on the worker after loader initialization. This avoids the main-menu log
heuristic and its 15-second settling delay. Scheduling still races the game's main thread;
there is no guarantee that hooks precede the game's first calls.

The worker accepts only `hoi4.exe`, checks the pinned executable SHA-256 and validates both
`current\runtime\capie_core.dll` and `capie_editor.dll` against hashes compiled into the
proxy. It retains read handles that deny replacement, loads resolved absolute paths with
restricted dependency search, checks mapped file identity and both initializer exports,
then initializes core before editor. Unknown builds and damaged bundles load no CAPIE.
An initializer failure is recorded in `proxy.log`; a core already activated is retained if
editor initialization fails, and the game must be restarted. No module with possible live
hooks is unloaded.

The DLL hashes compiled into the proxy come from `plugin/capie/deploy/bundle-inputs.json`, the
same list the launcher bundle is built from (format in `plugin/capie/deploy/DEPLOYMENT.md`);
`build_proxy.ps1` reads the generated header back and fails when its pins differ from that list.

Crash-loop breaker: right before the first `RNInitialize` the worker writes
`%LOCALAPPDATA%\RenameAnything\proxy-init.pending` and deletes it once both initializers have
returned. If the file is still there at the next start (the previous game ended inside an
initializer), that start loads no CAPIE, logs `crash_guard_skipped` in `proxy.log` and deletes
the file, so the start after it tries again. The launcher uses its own sentinel,
`capie-inject.pending`, around its two injector steps.

This method creates no helper process, opens no other process and uses no remote thread.
The system forwards remain available regardless of activation or validation outcome.

## Test evidence

[Prior analysis and REA coverage](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/proxy-loader-20261012/prior-analysis.md) records the
exact game hash, three imported version functions, 17 system exports and historical task
transcript findings. REA validates artifact identity; its Windows PTY runtime adapter is
unavailable. Native runtime observations must be reported separately.

[Windows startup acceptance](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/proxy-loader-20261012/runtime-report.md) passed all
four real-game cases with the reviewed release DLLs. Both enabled paths returned initializer
code 0; the modified bundle mapped no CAPIE DLLs. Cleanup restored all 35 top-level game
files byte-for-byte and removed the test proxy.

`tools/test-proxy-loader.ps1` temporarily installs the proxy for bounded startup checks:
disabled, flag enabled, config enabled and tampered payload. Each case uses isolated
`LOCALAPPDATA`, observes actual mapped modules and initializer diagnostics, then stops only
its own game process. Cleanup checks the proxy hash before deletion, restores user settings
and compares every top-level game file hash with the initial inventory.

These checks cover startup and module loading. They do not establish editor interactions,
multiplayer compatibility, antivirus detection rates or Steam/Paradox forwarding.
