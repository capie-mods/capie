# Rename Anything — bootstrap (PowerShell)

`install.ps1` installs CAPIE for Hearts of Iron IV. The default **Proxy** method places
one `version.dll` beside `hoi4.exe`. The game loads it during startup; it forwards the
Windows version API and loads CAPIE only when the game's command line contains the
complete `-capie` argument. The **Launcher** alternative keeps the game directory
unchanged. The script also updates, rolls back and uninstalls.

Status: **development**. The release URL, the pinned release ZIP hash and the update
signing key are placeholders, so the script refuses to install until a real release
exists. Proxy mode also requires `proxy/version.dll` in the verified release manifest;
an older launcher-only package must be installed explicitly with `-LoadMethod Launcher`.

* Windows PowerShell 5.1 (built into Windows) or PowerShell 7, without extra modules.
  Proxy mode needs write access to the game folder; the script does not elevate itself.
* Proxy mode adds only `version.dll` to the game directory. It never overwrites another
  tool's DLL. Workshop content and saves are untouched.
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
irm https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1 | iex
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

## Interactive questions

1. Install or update the bundle into `%LOCALAPPDATA%\RenameAnything\<version>`?
2. Loading method: `1` Proxy (default), `2` Launcher (no game-local DLL).
3. Proxy integration: `1` Steam launch option, `4` install only. Launcher integration
   additionally offers `2` shortcut and `3` both.
4. For Steam and shortcuts, a multiplayer warning (PL/EN, based on the Windows UI
   language) and a confirmation. **While the extension is enabled the game is not
   declared multiplayer-compatible.** Undo the change before playing multiplayer.
5. Shortcut folder: Desktop (default), a folder picked in an Explorer dialog, or a typed path.
6. Several Steam accounts: choose one, several, or all.

## Switches

| Switch | Meaning |
| --- | --- |
| `-Install` | install/verify the pinned release (`-PackagePath zip` for an already downloaded ZIP) |
| `-LoadMethod Proxy\|Launcher` | Proxy is the default for setup; Launcher removes only our owned proxy and uses the external launcher |
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
| `-Language pl|en` | override the detected language |

Omitted loading and activation options preserve an existing installation's choices;
a fresh install defaults to Proxy with Flag activation. Use an explicit option to
change a saved preference.

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

## Source layout and the single-file build

`install.ps1` is what users download, so it stays **one pure-ASCII file without BOM**
(`irm | iex` runs exactly one file). It is **generated**; do not edit it by hand. The
source lives in vertical slices under `src/`:

| Fragment | Content |
| --- | --- |
| `src/Header.ps1` | help and `param()` block of the distributed script |
| `src/Shared/Configuration.ps1` | the only release values: GitHub repo, update public key, pinned manifest |
| `src/Shared/Constants.ps1` | fixed names, formats, patterns and limits (no magic strings elsewhere) |
| `src/Shared/Adapters.ps1` | every external dependency: console, prompts, Steam process, registry, clock, network, file bytes, `.lnk` COM, folder dialog |
| `src/Shared/Messages.ps1` | EN/PL texts (`\uXXXX` escapes) |
| `src/Shared/Logging.ps1`, `Vdf.ps1`, `TextFile.ps1`, `State.ps1`, `Manifest.ps1`, `Steam.ps1`, `VersionStore.ps1` | shared building blocks |
| `src/Features/Install.ps1`, `Proxy.ps1`, `SteamLaunchOption.ps1`, `Shortcut.ps1`, `Update.ps1`, `Uninstall.ps1` | one file per user-visible action |
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

They run offline: every network call is mocked, and unexpected requests fail. Steam is a
synthetic folder tree, and shortcuts are created only in the work folder. Pester 3.4's
`Should Throw` is broken on PowerShell 7, so the tests use their own `Assert-RaThrow`.
The unit tests run under `Set-StrictMode -Version Latest`. Besides the behaviour tests
there are tests for the build (`-Check`, ASCII, documented functions, adapter discipline)
and for each adapter.

Mutation test (each mutant disables one safety check; all must be killed):

```powershell
.\tools\bootstrap\mutation-test.ps1 -WorkRoot $env:TEMP\ra-mutation -PowerShellExe pwsh
```

Static analysis for every PowerShell file in the repository (PSScriptAnalyzer with
`PSScriptAnalyzerSettings.psd1` and the project rules in `tools/lint/RaScriptRules.psm1`).
Nothing has to be installed into your profile:

```powershell
Save-Module PSScriptAnalyzer -Path $env:TEMP\psmodules
.\tools\lint-powershell.ps1 -ModulePath $env:TEMP\psmodules      # exit 0 = no findings
```

## Launcher alternative

`-LoadMethod Launcher` preserves the wrapper integration:
`"…\RenameAnythingLauncher.exe" %command%` in Steam, or
`"…\RenameAnythingLauncher.exe" "<game>\dowser.exe"` in the shortcut. The wrapper source
and offline checks live in `plugin/capie/build_launcher.ps1`. A release must include
the reviewed launcher and matching runtime bundle; the manifest pinned here remains
an unpublished development placeholder.

---

## Po polsku (skrót)

Skrypt instaluje CAPIE do `%LOCALAPPDATA%\RenameAnything`. Domyślny tryb Proxy dodaje
`version.dll` do folderu gry i aktywuje CAPIE argumentem `-capie`. Cudzych DLL nie
nadpisuje. Tryb `-LoadMethod Launcher` zachowuje launcher i skróty bez plików w folderze
gry. Nie wymaga administratora, nie zmienia Workshop ani zapisów.
**Z włączonym rozszerzeniem gra nie jest deklarowana jako zgodna z trybem
wieloosobowym.** Przed grą MP uruchom skrypt z `-RemoveSteamLaunchOption` albo
`-Uninstall`, albo usuń wpis ręcznie we Właściwościach gry w Steam. Przy jawnej aktywacji
`-ProxyActivation Config` trzeba też usunąć `%LOCALAPPDATA%\RenameAnything\proxy.enabled`.
Aktualizacja:
`-CheckUpdate` (tylko na żądanie) i `-Update`, cofnięcie: `-Rollback`. Bezpieczniej
pobrać skrypt, przeczytać i dopiero uruchomić niż używać `irm | iex`.
