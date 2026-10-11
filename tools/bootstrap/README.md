# Rename Anything — bootstrap (PowerShell)

`install.ps1` installs the Rename Anything native launcher bundle for Hearts of Iron IV
and, if you want, wires it into the game's start: a **Steam launch option**, a
**shortcut** in a folder you choose, or both. It also updates, rolls back and uninstalls.

Status: **development**. The release URL, the pinned release ZIP hash and the update
signing key are placeholders, so the script refuses to install until a real release
exists. The launcher does not support the wrapper mode yet (see "Launcher changes"
below), so do not use the Steam/shortcut modes with the current launcher candidate.

* Windows PowerShell 5.1 (built into Windows) or PowerShell 7. No admin rights and no extra modules.
* Never touches the game installation, Workshop content or saves.
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
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1))) -Install -Shortcut -Yes
```

## Interactive questions

1. Install or update the launcher and DLLs into `%LOCALAPPDATA%\RenameAnything\<version>`?
2. How to start the game **with** the extension: `1` Steam launch option, `2` shortcut,
   `3` both, `4` neither.
3. For Steam and shortcuts, a multiplayer warning (PL/EN, based on the Windows UI
   language) and a confirmation. **While the extension is enabled the game is not
   declared multiplayer-compatible.** Undo the change before playing multiplayer.
4. Shortcut folder: Desktop (default), a folder picked in an Explorer dialog, or a typed path.
5. Several Steam accounts: choose one, several, or all.

## Switches

| Switch | Meaning |
| --- | --- |
| `-Install` | install/verify the pinned release (`-PackagePath zip` for an already downloaded ZIP) |
| `-SteamLaunchOption` | add the launcher to the HOI4 launch options (`-SteamAccount <id>[,<id>]` or `all`) |
| `-Shortcut` / `-ShortcutPath <folder>` | create the shortcut (Desktop by default) |
| `-RemoveSteamLaunchOption` | remove only our part of the launch options |
| `-CheckUpdate` | one request to the GitHub Releases API: reports the newest version and whether it supports your `hoi4.exe` |
| `-Update` | install the newest signed release and switch to it (`-ManifestPath` + `-PackagePath` work offline) |
| `-Rollback` | switch back to the previous version (kept on disk) |
| `-Uninstall` | remove the launch option part, our shortcuts and `%LOCALAPPDATA%\RenameAnything` (the log is kept) |
| `-DryRun` (alias `-WhatIf`) | show what would happen, write nothing (not even the log) |
| `-Yes` | no questions; actions must be given as switches |
| `-SteamRoot <dir>` | Steam folder override (default: `HKCU\Software\Valve\Steam\SteamPath`) |
| `-Language pl|en` | override the detected language |

## What exactly is changed

**Steam launch options:** `Steam\userdata\<account>\config\localconfig.vdf`, key
`UserLocalConfigStore/Software/Valve/Steam/apps/394360/LaunchOptions`. Steam must be
closed; the script waits for you to close it and never closes Steam itself. It makes a
byte-exact backup `localconfig.vdf.renameanything-backup-<timestamp>` and replaces only
the value bytes. If the key or the `394360` block is missing, it inserts it with
matching indentation. The written file is compared byte for byte with the expected
content and parsed again; on any difference the backup is restored. The script refuses
to edit corrupted, ambiguous, non-UTF-8 or unusual files.

Your existing options are kept. Our fragment goes directly before `%command%`:

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
delete the quoted `…\RenameAnythingLauncher.exe` part.

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
  previous version stays on disk for `-Rollback`; older ones are pruned.
* After a game patch the launcher (planned) starts the game without the DLL. Use
  `-CheckUpdate` to see whether a release supporting your `hoi4.exe` exists. Each manifest
  lists the supported `hoi4.exe` SHA256 values.
* Maintainers: `sign-release-manifest.ps1 -NewKey | -MakeManifest | -Sign` (offline).
  Keep the private key outside the repository.

## Tests

```powershell
$env:RA_BOOTSTRAP_TEST_ROOT = "$env:TEMP\ra-bootstrap-tests"   # work folder
# optional: a COPY of a real localconfig.vdf
$env:RA_BOOTSTRAP_REAL_LOCALCONFIG = "C:\somewhere\localconfig-copy.vdf"
Invoke-Pester -Script .\tools\bootstrap\install.Tests.ps1         # Pester 3.4 (built into Windows)
```

They run offline: every network call is mocked, and unexpected requests fail. Steam is a
synthetic folder tree, and shortcuts are created only in the work folder. Pester 3.4's
`Should Throw` is broken on PowerShell 7, so the tests use their own `Assert-RaThrows`.

## Launcher changes required (separate task, not in this folder)

The bootstrap only configures `"…\RenameAnythingLauncher.exe" %command%` and the
shortcut `"…\RenameAnythingLauncher.exe" "<game>\dowser.exe"`. The current launcher
rejects unknown arguments (exit 2) and starts `hoi4.exe` itself. Before the Steam or
shortcut mode can be used it needs a reviewed **wrapper mode**: accept an absolute
`dowser.exe`/`hoi4.exe` path from the selected game folder plus the remaining arguments,
start exactly that, bind the resulting `hoi4.exe` to its own child process tree, run the
game **without** the DLL on an unknown game hash (with a clear message), and keep working
when started through the `current` junction. Details:
`docs/evidence/session-20261011/claude-bootstrap/report.txt`.

---

## Po polsku (skrót)

Skrypt instaluje launcher i DLL do `%LOCALAPPDATA%\RenameAnything` i pyta, czy dodać
launcher do opcji uruchamiania HOI4 w Steam, utworzyć skrót (folder wybierasz w oknie
Eksploratora), czy oba. Nie wymaga administratora, nie zmienia gry, Workshop ani zapisów.
**Z włączonym rozszerzeniem gra nie jest deklarowana jako zgodna z trybem
wieloosobowym.** Przed grą MP uruchom skrypt z `-RemoveSteamLaunchOption` albo
`-Uninstall`, albo usuń wpis ręcznie we Właściwościach gry w Steam. Aktualizacja:
`-CheckUpdate` (tylko na żądanie) i `-Update`, cofnięcie: `-Rollback`. Bezpieczniej
pobrać skrypt, przeczytać i dopiero uruchomić niż używać `irm | iex`.
