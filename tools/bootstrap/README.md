# CAPIE installer (PowerShell bootstrap)

`install.ps1` installs CAPIE for Hearts of Iron IV ([ADR-0030](../../docs/adr/0030-installation-paths-and-playset-switch.md)).
The default **Launcher** method puts the CAPIE launcher into the Steam launch options and
keeps the game directory unchanged; CAPIE loads only when the "CAPIE" switch mod is enabled
in the playset, and `-Install` places that mod in your mod folder. The **Proxy** method is
an explicit alternative (`-LoadMethod Proxy`, alias `-Method Proxy`): one `version.dll`
beside `hoi4.exe`, loading CAPIE when the game's command line contains `-capie`; it does not
read the playset. The script also updates, rolls back, disables and uninstalls.

Status: **development**. The release URL, the pinned release ZIP hash and the update
signing key are placeholders, so the script refuses to install until a real release
exists. Proxy mode also requires `proxy/version.dll` in the verified release manifest;
a launcher-only package refuses `-LoadMethod Proxy`.

* Windows PowerShell 5.1 (built into Windows) or PowerShell 7, without extra modules.
  Proxy mode needs write access to the game folder; the script does not elevate itself.
* Proxy mode adds only `version.dll` to the game directory. It never overwrites another
  tool's DLL. Workshop content and saves are untouched.
* Keep only one copy of each mod with the same name. HOI4 does not load a subscribed Workshop
  mod while a local mod with the same `name` exists, and the Paradox Launcher takes that name
  from `descriptor.mod` inside the mod's folder (it rewrites the `.mod` files in the mod folder
  at every start). The local switch mod is therefore named "CAPIE (local copy)", and when the
  Workshop item is subscribed (`mod\ugc_3819929471.mod` exists) the script installs no local
  copy at all and removes an earlier one. Do the same with your own local copies of Rename
  Anything or other Workshop mods.
* The "CAPIE" switch mod goes to `Documents\Paradox Interactive\Hearts of Iron IV\mod`
  (`capie.mod` and `capie\`), copied from the verified bundle (`mod/` in the release ZIP).
  Every file is recorded with its SHA-256 in `state.json`. A file that was there before is
  backed up first (byte-verified copy `<file>.renameanything-backup-<time>`) and put back
  by `-Uninstall`; a file you changed after installation is left in place. `-Update` and
  `-Rollback` bring the mod to the active version.
* The DLL never talks to the network. The launcher does nothing in the background.
  The only network access is this script, and only when you run `-Install` (download)
  or ask for `-CheckUpdate` / `-Update`. It sends no identifiers and no telemetry.
* Log: `%LOCALAPPDATA%\RenameAnything\bootstrap.log`.

## Run it

Recommended: download, read, then run.

```powershell
# 1. download (replace OWNER/TAG once the repository is public)
Invoke-WebRequest https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1 -OutFile install.ps1
# 2. read it (notepad install.ps1) and compare its SHA256 with the release notes
Get-FileHash .\install.ps1
# 3. run it (interactive)
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

One line (convenient, less safe):

```powershell
irm https://github.com/capie-mods/capie/releases/latest/download/install.ps1 | iex
```

**Security trade-off of `irm | iex`:** whatever the URL serves at that moment runs
immediately with your user rights. You do not see it first, and nothing checks its
hash. If the GitHub account or the URL were compromised, the code would run anyway.
Pinning a tag in the URL helps a little; downloading and reading first is the real
protection. The script itself verifies everything it downloads after that point
(pinned SHA256 or signed manifest), but nothing can verify the script that `iex`
runs. `iex` runs in your current session, so the script's functions stay defined
there until you close the window. It never calls `exit` in that mode.

Passing switches with the one-liner:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1))) -Install -LoadMethod Launcher -Shortcut -Yes
```

## Terminal interface

Run without action switches on an interactive console (the usual `irm | iex`), the script
opens a small terminal interface instead of the plain questions below:

* a splash in the design system's colours: a staff map sheet with a flag in square C3 and the
  pencil writing "Rename Anything" into the margin (one character per frame, under a second;
  the still frame is complete),
* the current state (installed version, Steam launch option),
* a menu: Install, Update, Disable, Uninstall, Quit (arrows or 1-5, Enter, Esc),
* the questions the action needs (multiplayer note; add the launcher to Steam [Y/n];
  uninstall [y/N] and purge [y/N]; close Steam first),
* progress as status lines in the CLI pattern (`OK`, `ASK`, `SKIP`, `NOTE`, `FAIL`),
* a summary: what is installed, what Steam does, that the game folder is unchanged, how to
  switch CAPIE on and off with the "CAPIE" mod, how to undo, where the log is.

Install from the interface uses the launcher method (user folder only, no file in the game
folder) and, once the release channel is configured, the newest signed release; an existing
installation keeps its method. Colour needs a virtual-terminal host and is off with `NO_COLOR`
or `TERM=dumb`; the animation is off with `-NoAnimation`, `RA_NO_ANIMATION=1` or `CI`. A
redirected console, `-Plain`, `-Yes` or any action switch uses the plain prompts. Tests drive
the interface through the key, terminal, environment and raw-output adapters.

## Turning CAPIE on and off: the "CAPIE" mod

With the launcher method, CAPIE loads only when the small "CAPIE" mod
(`plugin/capie/toggle-mod/`) is enabled in the playset the game starts with. The launcher reads
`Documents\Paradox Interactive\Hearts of Iron IV\dlc_load.json` after the game process exists.
If the mod is disabled, the game starts clean. If the file cannot be read, the game also starts
clean and you get one notice. The launcher never blocks the game. `-Disable` removes the launcher from Steam entirely; `-Uninstall -Purge`
restores the state from before the installation.

## Without an interactive console

If no action switch is given and the console cannot answer questions (input or output
redirected, for example a CI job or an editor panel), the script prints the list of actions
and exits with code 2. It writes nothing and does not wait. Pass an action (`-Install`,
`-Update`, `-Disable`, `-Uninstall`, ...) with `-Yes`, or `-Plain` to force the questions.

All output is English and pure ASCII, so it reads the same in every console code page; the
script does not change the console encoding of your session. `install.ps1` has no BOM on
purpose: ASCII needs none, and a BOM at the start of the downloaded text would break
`irm | iex` (a test covers this).

## Interactive questions

1. Install or update the bundle into `%LOCALAPPDATA%\RenameAnything\<version>`?
2. Loading method: `1` Launcher (default, no game-local DLL, switch mod in the playset), `2` Proxy.
3. Proxy integration: `1` Steam launch option, `4` install only. Launcher integration
   additionally offers `2` shortcut and `3` both.
4. For Steam and shortcuts, a multiplayer warning and a confirmation. **While the extension is enabled the game is not
   declared multiplayer-compatible.** Undo the change before playing multiplayer.
5. Shortcut folder: Desktop (default), a folder picked in an Explorer dialog, or a typed path.
6. Several Steam accounts: choose one, several, or all.

## Switches

| Switch | Meaning |
| --- | --- |
| `-Install` | install/verify the pinned release (`-PackagePath zip` for an already downloaded ZIP) |
| `-Install -Latest` | release installer: newest signed release from GitHub Releases into `%LOCALAPPDATA%\RenameAnything` (`-Latest` alone implies `-Install`; offline with `-ManifestPath` + `-PackagePath`) |
| `-Disable` | turn CAPIE off: remove only our part of the Steam launch options and our proxy activation marker; files stay |
| `-Uninstall -Purge` | uninstall, then also delete our `localconfig.vdf` backups and the log: nothing of the installation is left |
| `-NoAnimation` / `-Plain` | terminal interface without the splash animation / plain prompts instead of the interface |
| `-LoadMethod Launcher\|Proxy` (alias `-Method`) | Launcher is the default; Proxy is the explicit alternative (game-local DLL, no playset switch); switching to Launcher removes only our owned proxy |
| `-ProxyActivation Flag\|Config` | Flag is the default; Config creates the explicit persistent fallback marker described below |
| `-SteamLaunchOption` | append `-capie` (Proxy), or add the launcher wrapper (Launcher); `-SteamAccount <id>[,<id>]` or `all` |
| `-Shortcut` / `-ShortcutPath <folder>` | create the launcher shortcut; requires `-LoadMethod Launcher` (Desktop by default) |
| `-RemoveSteamLaunchOption` | remove only our part of the launch options |
| `-CheckUpdate` | one request to the GitHub Releases API: reports the newest version and whether it supports your `hoi4.exe` |
| `-Update` | install the newest signed release and switch to it (`-ManifestPath` + `-PackagePath` work offline) |
| `-Rollback` | switch back to the previous version (kept on disk) |
| `-Uninstall` | remove the launch option part, our shortcuts and `%LOCALAPPDATA%\RenameAnything` (the log is kept) |
| `-DryRun` (alias `-WhatIf`) | show what would happen, write nothing (not even the log) |
| `-Yes` | no questions; actions must be given as switches |
| `-SteamRoot <dir>` | Steam folder override (default: `HKCU\Software\Valve\Steam\SteamPath`) |

If you leave out the loading and activation options, an existing installation keeps its
choices. A fresh install uses Launcher (and, if you choose Proxy, the Flag activation). Pass an
explicit option to change a saved preference.

## What exactly is changed

**Steam launch options:** `Steam\userdata\<account>\config\localconfig.vdf`, key
`UserLocalConfigStore/Software/Valve/Steam/apps/394360/LaunchOptions`. Steam must be
closed; the script waits for you to close it and never closes Steam itself. It makes a
byte-exact backup `localconfig.vdf.renameanything-backup-<timestamp>` and replaces only
the value bytes. If the key or the `394360` block is missing, it inserts it with
matching indentation. The written file is compared byte for byte with the expected
content and parsed again; on any difference the backup is restored. The script refuses
to edit corrupted, ambiguous, non-UTF-8 or unusual files.

Proxy mode keeps your existing options and appends `-capie` once. For example,
`-debug_mode` becomes `-debug_mode -capie`; no executable wrapper is added. Removing
`-capie` disables CAPIE on the next game start. If you switch from Launcher to Proxy,
the script removes its own old wrapper. Undo restores the previous launch option
bytes when the value is unchanged, otherwise it removes only the complete activation
argument and preserves subsequent user edits.

Launcher mode puts its fragment directly before `%command%`:

| Before | After |
| --- | --- |
| *(empty)* | `"…\RenameAnything\current\RenameAnythingLauncher.exe" %command%` |
| `-debug_mode` | `"…\RenameAnythingLauncher.exe" %command% -debug_mode` |
| `%command% -debug_mode` | `"…\RenameAnythingLauncher.exe" %command% -debug_mode` |
| `"C:\other.exe" %command% -x` | `"C:\other.exe" "…\RenameAnythingLauncher.exe" %command% -x` (warning: chained wrapper) |
| `%command% … %command%` | refused (edit manually) |

Undo restores your exact previous value if nobody changed it since. Otherwise it removes
only our quoted fragment, and a leading bare `%command%` is dropped, which means the same to Steam.

Manual undo: Steam > Library > Hearts of Iron IV > Properties > General > Launch Options,
delete `-capie` (Proxy) or the quoted `…\RenameAnythingLauncher.exe` part (Launcher).

**Game-local DLL:** Proxy mode copies the manifest-verified `proxy/version.dll` to the
Steam-discovered directory containing `hoi4.exe`. The installer records its absolute
path, release version and SHA256 in `state.json`. An existing `version.dll` is changed
or removed only when both the ownership record and stored release manifest prove its
current hash. Foreign files, modified files, directories and links are left untouched.
Removal moves an owned file to a random recovery path, checks its hash again and only
then deletes it; a racing foreign replacement is restored or retained for recovery.
This protects ordinary concurrent replacements, not a hostile process with the same
user rights continuously altering random recovery paths.
Updates and rollback synchronize the proxy with the runtime version; close the game
first because Windows can lock loaded DLLs. If replacement fails, the current runtime
and previous proxy remain available for retry. Steam file verification may leave this
extra file in place; use `-Uninstall` to remove an owned copy.

**Explicit configuration fallback:** If Paradox Launcher does not pass `-capie` to
`hoi4.exe`, run `-Install -ProxyActivation Config`. This writes
`%LOCALAPPDATA%\RenameAnything\proxy.enabled` containing exactly the ASCII bytes
`CAPIE_PROXY_ENABLED_V1` followed by LF (no BOM). It enables CAPIE for every normal
game start while the marker exists. Removing the Steam flag alone no longer disables
it: remove this marker, run `-Install -ProxyActivation Flag`, or uninstall. The script
removes only a marker whose saved hash still matches; it preserves foreign content.

**Shortcut:** `Hearts of Iron IV + Rename Anything.lnk`, target
`%LOCALAPPDATA%\RenameAnything\current\RenameAnythingLauncher.exe`, argument the game's
Paradox Launcher `"<game>\dowser.exe"` (the same thing Steam's `%command%` points to),
icon from `hoi4.exe` (read-only), and a comment saying it is the extension build, not
declared multiplayer-compatible. The script never overwrites a file it did not create.
Uninstall deletes only shortcuts recorded in `state.json` (or on the Desktop) that still
point to our launcher.

**Files:** `%LOCALAPPDATA%\RenameAnything\` contains `<version>\` (verified bundle),
`current` (a directory junction to the active version, no admin needed),
`manifests\<version>.json`, `state.json` and `bootstrap.log`. Steam and shortcuts always
point at `current\`, so an update or rollback never needs Steam to be closed and never
edits `localconfig.vdf` again.

## Integrity and updates

* `-Install` uses the release manifest **pinned in the script**: the ZIP SHA256 is checked
  before extraction. Every ZIP entry must be in the manifest, the SHA256 of each file is
  checked, and missing, extra, duplicate or `..` entries are refused. Installation goes to
  a staging folder and is moved into place only after a full re-verification.
* `-CheckUpdate` / `-Update` fetch `releases/latest` from the GitHub API plus
  `release-manifest.json` and `release-manifest.json.sig`. The manifest must be signed
  (RSA ≥ 2048, PKCS#1 v1.5, SHA-256) with the key whose **public part is pinned in the
  script**. The tag must match the manifest version, and asset URLs must be on
  `github.com/<repo>/releases/download/`. The ZIP is then verified exactly as above. The
  previous version stays on disk for `-Rollback`; older ones are pruned. The proxy is
  covered by the same signed manifest and per-file hash validation as the runtime.
* After an unsupported game patch the proxy forwards Windows calls and skips CAPIE. Use
  `-CheckUpdate` to see whether a release supporting your `hoi4.exe` exists. Each manifest
  lists the supported `hoi4.exe` SHA256 values.
* Maintainers: `sign-release-manifest.ps1 -NewKey | -MakeManifest | -Sign` (offline).
  Keep the private key outside the repository. Paste the public key into
  `src/Shared/Configuration.ps1` and rebuild `install.ps1`.

## Release installer and signing

The release path is `-Install -Latest`: one GitHub API request for the latest release, then
`release-manifest.json` and `.sig` (RSA signature against the key pinned in this script; tag
equals manifest version; asset URLs inside this repository), then the ZIP: SHA-256 before
extraction, every file's SHA-256 after extraction, full re-verification of the staging folder,
then an atomic move into `%LOCALAPPDATA%\RenameAnything\<version>` and the `current` junction.
Nothing is written to the game folder in launcher mode.

What a public release still needs: the repository name and the public update key in
`src/Shared/Configuration.ps1`, a signed manifest per release (`sign-release-manifest.ps1`),
and the release assets (`install.ps1`, ZIP, manifest, `.sig`). Authenticode for the EXE/DLL
files and for `install.ps1` is decided in [ADR-0031](../../docs/adr/0031-trust-without-paid-code-signing.md):
no paid certificate. Trust comes from open code, CI builds, the signed manifest, VirusTotal
and clear notes on the SmartScreen warning. Once the repository is public, the project applies
to the SignPath Foundation (free for open source). When that is granted, every binary is signed
(`/fd SHA256` with an RFC 3161 timestamp) before the ZIP and manifest are built, so the
manifest hashes cover the signed files. Until then SmartScreen and some antivirus engines see
the launcher as an unknown publisher; the signed manifest still proves the files came from
the maintainer's key.

## Source layout and the single-file build

`install.ps1` is what users download, so it stays one pure-ASCII file without a BOM
(`irm | iex` runs exactly one file). It is **generated**: do not edit it by hand. The
source lives in vertical slices under `src/`:

| Fragment | Content |
| --- | --- |
| `src/Header.ps1` | help and `param()` block of the distributed script |
| `src/Shared/Configuration.ps1` | the only release values: GitHub repo, update public key, pinned manifest |
| `src/Shared/Constants.ps1` | fixed names, formats, patterns and limits (no magic strings elsewhere) |
| `src/Shared/Adapters.ps1` | every external dependency: console, prompts, Steam process, registry, clock, network, file bytes, `.lnk` COM, folder dialog |
| `src/Shared/Messages.ps1` | user-facing texts (English only, pure ASCII) |
| `src/Shared/Logging.ps1`, `Vdf.ps1`, `TextFile.ps1`, `State.ps1`, `Manifest.ps1`, `Steam.ps1`, `VersionStore.ps1` | shared building blocks |
| `src/Shared/Tui.ps1` | terminal interface: capability detection, palette, map sheet, line models, menu and key reading |
| `src/Features/Install.ps1`, `Proxy.ps1`, `SteamLaunchOption.ps1`, `Shortcut.ps1`, `Update.ps1`, `Uninstall.ps1`, `Tui.ps1` | one file per user-visible action (Uninstall also holds `-Disable` and `-Purge`) |
| `src/Main.ps1` | option handling, dispatch, entry point (always last) |

The fragments are plain `.ps1` files with functions, not `.psm1` modules: all slices
share one script scope (configuration, adapters, runtime switches), and a module would
isolate that state per slice. Production code reaches the outside world only through
the adapters, so tests replace them after dot-sourcing `install.ps1`
(for example `$script:RaGetSteamProcess = { @('fake-steam') }`). A Pester test enforces
that no fragment except `Adapters.ps1` calls `Write-Host`, `Read-Host`, `Get-Process`,
`Get-ItemProperty`, `Get-UICulture`, `Get-Date`, `WebClient` or `WScript.Shell` directly.

```powershell
.\tools\bootstrap\build-install.ps1          # regenerate install.ps1 from src/
.\tools\bootstrap\build-install.ps1 -Check   # exit 1 if install.ps1 is stale (also a Pester test)
```

The builder concatenates the fragments in a fixed order (each in a `#region`), uses LF
line endings, rejects any non-ASCII byte and refuses output that does not parse. Every
function has comment-based help (`.SYNOPSIS`, `.DESCRIPTION`, `.PARAMETER`, `.OUTPUTS`,
`.EXAMPLE`), `[CmdletBinding()]`, `[OutputType()]` and typed parameters. `Invoke-RaMain`
enables `Set-StrictMode -Version Latest` (only for the call: under `irm | iex` the top
level runs in the user's session, which must not be changed).

## Tests

```powershell
$env:RA_BOOTSTRAP_TEST_ROOT = "$env:TEMP\ra-bootstrap-tests"   # work folder
# optional: a COPY of a real localconfig.vdf
$env:RA_BOOTSTRAP_REAL_LOCALCONFIG = "C:\somewhere\localconfig-copy.vdf"
Invoke-Pester -Script .\tools\bootstrap\install.Tests.ps1         # Pester 3.4 (built into Windows)
```

The tests run offline: they mock every network call, and an unexpected request fails. Steam is
a synthetic folder tree, and shortcuts go only into the work folder. Pester 3.4's
`Should Throw` is broken on PowerShell 7, so the tests use their own `Assert-RaThrow`.
The unit tests run under `Set-StrictMode -Version Latest`. Besides the behaviour tests
there are tests for the build (`-Check`, ASCII, documented functions, adapter discipline)
and for each adapter.

Mutation test (each mutant disables one safety check; all must be killed):

```powershell
.\tools\bootstrap\mutation-test.ps1 -WorkRoot $env:TEMP\ra-mutation -PowerShellExe pwsh
```

Every PowerShell file in the repository also goes through static analysis: PSScriptAnalyzer
with `PSScriptAnalyzerSettings.psd1` and the project rules in `tools/lint/RaScriptRules.psm1`.
You do not have to install anything into your profile:

```powershell
Save-Module PSScriptAnalyzer -Path $env:TEMP\psmodules
.\tools\lint-powershell.ps1 -ModulePath $env:TEMP\psmodules      # exit 0 = no findings
```

## Launcher integration (default)

The launcher method uses the wrapper integration:
`"…\RenameAnythingLauncher.exe" %command%` in Steam, or
`"…\RenameAnythingLauncher.exe" "<game>\dowser.exe"` in the shortcut. The wrapper source
and offline checks live in `plugin/capie/build_launcher.ps1`. A release must include
the reviewed launcher and matching runtime bundle; the manifest pinned here remains
an unpublished development placeholder.
