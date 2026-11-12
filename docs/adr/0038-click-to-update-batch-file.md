# ADR-0038: Updates are started by the player through Update CAPIE.bat, never automatically

- Status: Accepted; amended the same day (signed installer in the manifest, Update CAPIE shortcut)
- Date: 2026-11-12
- Source: maintainer request for CAPIE 1.7.0 (easier updates after a game patch)

## Context

After every Hearts of Iron IV patch the game starts without CAPIE until the player installs a
CAPIE release for the new build. Until now that meant running the one-line `irm ... | iex`
command again or the downloaded `install.ps1 -Update`. Players asked for something simpler.

An automatic update was designed first: opt-in at install time, the launcher checking GitHub
Releases at game start (rate-limited, always after a game patch) and running a local copy of the
installer with `-Update -Unattended` while the game loads, possibly loading the new version in
the same start. It was rejected for safety before it was implemented:

- The launcher would gain network access and a code path that changes the installation while the
  game and the Paradox Launcher run. Today every installer action that changes files refuses while
  `hoi4.exe` or `dowser.exe` runs (`Assert-RaGameClosed`), and the launcher has no network code.
- Loading a release in the same start means an older launcher verifies and injects files of a
  newer release whose hashes it does not have built in: a combination no release is tested with.
- A background download is a network call the player did not start, against the "no network"
  statement in safety.md.

## Decision

- Every install, update, repair (`-Update` when already current) and rollback writes two files in
  `%LOCALAPPDATA%\RenameAnything`:
  - `setup\install.ps1`: a copy of the installer that is running, taken from its own syntax tree,
    so it is byte for byte what the player ran (with `-File`, `irm | iex` or a script block). Only
    a generated `install.ps1` (pure ASCII, generated-file marker, entry point) is copied;
    otherwise the existing copy and batch file stay as they are. It lives outside the version
    folders, which hold exactly the files of their signed manifest and stay valid for rollback.
  - `Update CAPIE.bat`: names the copy by absolute path and records its SHA-256. It starts
    Windows PowerShell 5.1 by absolute path (`%SystemRoot%\System32\WindowsPowerShell\v1.0`,
    never PATH, never pwsh) with `-NoProfile -ExecutionPolicy Bypass -Command`. The path and hash
    reach PowerShell as environment variables, so no path is parsed as code; `%` is doubled for
    cmd.exe, a double quote or line break is refused, delayed expansion is off. PowerShell reads
    the copy once, compares its SHA-256 and runs exactly those bytes with `-Update`, so there is no
    time between check and use. A missing or changed copy runs nothing and names the download
    address of the installer. The window stays open until a key is pressed.
- The update itself is the existing `-Update`: signed manifest, tag check, every SHA-256, hold-back
  of a release that does not support the installed `hoi4.exe` while the installed one does, the
  refusal while the game runs, and the Steam questions exactly as before. No new verification code.
- `-Uninstall` removes both files with the rest of the root folder (the log stays).
- The launcher's unsupported-build notice and the docs name `Update CAPIE.bat` as the easiest way
  to update, next to the one-line command. The launcher only names the file; it never runs it and
  still has no network code.

## Threat model

- `Update CAPIE.bat`, the copy, the launcher and the CAPIE DLLs are all in the player's own
  `%LOCALAPPDATA%`, writable by every program the player runs. Anyone who can change the batch
  file can also change the launcher Steam starts, so the batch file adds no new way in. Its hash
  check protects against a damaged or partly written copy, not against such a program.
- Release integrity does not depend on the local files: whatever starts `-Update`, a release is
  accepted only when its manifest verifies with the RSA key built into the installer, and the
  private key never leaves the maintainer's offline storage. A compromised GitHub account can
  publish a release but cannot sign it.
- Privacy is unchanged: the network requests are those of `-Update` (GitHub API, manifest,
  signature, ZIP), without identifiers, and only when the player double-clicks the file.

## Amendment: the installer copy follows the signed releases

- From CAPIE 1.7.0 the signed `release-manifest.json` lists the release's `install.ps1` asset in
  the optional field `installer` (`name`, `sha256`). The schema number stays 1: older installers
  read only the fields they know, and newer installers accept a manifest without the field.
  `tools/release` writes the installer pins first, then lists the SHA-256 of that final
  `install.ps1` (`sign-release-manifest.ps1 -InstallerPath`), and the release step refuses to
  upload an `install.ps1` that changed since.
- `-Update` (and so `Update CAPIE.bat`) and `-Install -Latest` download that asset (or take
  `install.ps1` next to an offline `-ManifestPath`), require its SHA-256 to equal the signed one
  and require a generated installer (ASCII, marker, entry point), then keep it as
  `setup\install.ps1` and write the batch file with its hash. A mismatch, a missing asset or a
  download error is a warning: the running installer stays the copy and the update goes on.
  `-CheckUpdate` and `-Version` never fetch it.
- Chosen: the update always finishes with the installer that is running, and the verified newer
  installer is used from the next click on. Re-running the update in the same run with the
  downloaded installer was rejected: it would run freshly downloaded code inside the run that
  fetched it, hand state (Steam closed by us, answers already given) between two installer
  versions and need a loop guard, all to gain one click. The cost: a fix in the newer
  installer's `-Update` applies one update later, and if a future manifest format were
  unreadable by the old copy, that copy could not refresh itself and the player would need the
  one-line command once. The schema stays compatible to avoid exactly that.
- When the player chooses shortcuts, "Update CAPIE" (to the batch file) is created next to the
  game shortcut (the Desktop by default) and in the Start menu. Both are recorded in state.json,
  count as ours only while they point at `Update CAPIE.bat` inside the root folder, are never
  written over a foreign file, and are removed by `-Uninstall`.

## Consequences

- Updating after a patch is one double-click once the player knows the folder (or the Start
  menu entry); the notice says where it is.
- Known limit: cmd.exe expands `%NAME%` in the batch file's own path when it is started, so a
  Windows user folder whose name contains a `%...%` pair breaks the double-click; run the
  one-line command instead.
