# Release script

`tools/release/release.py` makes a release on the maintainer's machine, without GitHub Actions:
it checks the commit, packages the reviewed CAPIE binaries into the installer's ZIP format,
signs the manifest, creates a GitHub release, optionally scans the files on VirusTotal, and
uploads the mods to the Steam Workshop. It follows the [release checklist](releasing.md).

By default nothing goes public. The GitHub release is a **draft pre-release**, a new Workshop
item is **Hidden**, and a VirusTotal upload, a published or stable release and any Workshop
visibility other than Hidden each ask you to type a confirmation word. `--dry-run` shows every
command and file and sends nothing.

## Requirements

- `uv sync` (Python 3.13, ruff, ty, pytest). The tool uses only the standard library.
- PowerShell 7 (`pwsh`) for the build scripts and `sign-release-manifest.ps1`.
- Visual Studio C++ tools and a MinHook checkout for the native builds
  ([building and testing](building-and-testing.md)).
- [GitHub CLI](https://cli.github.com/), logged in with `gh auth login`.
- [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD) for the Workshop step.
- The release signing key, created once with
  `tools/bootstrap/sign-release-manifest.ps1 -NewKey -PrivateKeyPath <path outside the repo>`.
  Its public part goes into `tools/bootstrap/src/Shared/Configuration.ps1`.
- Optional: a VirusTotal API key in the environment variable `VT_API_KEY`.

## Commands

```powershell
uv run tools/release/release.py all --dry-run --virustotal --steam-user <account>   # the whole plan
uv run tools/release/release.py preflight --minhook-dir C:\src\minhook
uv run tools/release/release.py build --signing-key D:\keys\capie-release.private.xml
uv run tools/release/release.py release
uv run tools/release/release.py virustotal --update-notes
uv run tools/release/release.py workshop --steam-user <account>
```

`all` runs preflight, build, VirusTotal (only with `--virustotal`), release and workshop (unless
`--skip-workshop`) and stops at the first failed step. A dry run never stops early. Each run
ends with a summary of every warning and failure, and writes a log to
`dist/release/logs/` (git-ignored). Output folder of a version: `dist/release/<version>/`.

Common options: `--dry-run`, `--yes` (skips only the plain "upload?" question, never a typed
confirmation), `--verbose` (tool output on the console; it is always in the log),
`--no-color` (also `NO_COLOR=1`), `--config <file>`.

## What each step does

**preflight**: a clean working tree on `main`; the version sources agree; the changelogs have
a section for the versions being released; installer placeholders; the switch mod's Workshop ID
compiled into the launcher; the installed `hoi4.exe` is the supported build. Then the commands
in `[[preflight.commands]]`: ruff, ty, the docstring check, pytest, `gen.py` (its output must
leave `mod/` unchanged), `check.py`, the in-place editor checks, the SDK and site theme checks,
the switch mod check, `build-install.ps1 -Check`, the installer's Pester suite,
PSScriptAnalyzer (with `--psscriptanalyzer-dir`) and the CAPIE native build with all offline
tests (with `--minhook-dir`; `--skip-slow` leaves it out). A command that needs the game,
MinHook or PSScriptAnalyzer is skipped and reported when that is missing.

**build**: checks that `plugin/build/capie/` holds the binaries whose hashes are in
`plugin/capie/deploy/bundle-inputs.json`, runs `build_launcher.ps1` and `build_proxy.ps1` (they pin
those hashes and run their own tests), checks that every shipped PE file carries the build
identity of `game_build.hpp` (see below), and writes:

| File | Content |
| --- | --- |
| `CAPIE-<version>-win64.zip` | `RenameAnythingLauncher.exe`, `LICENSE.MinHook.txt`, `runtime/` (core, editor, injector), `proxy/version.dll`, `mod/capie.mod` and `mod/capie/` |
| `release-manifest.json`, `.sig` | from `sign-release-manifest.ps1 -MakeManifest` and `-Sign`, then checked again: every file hash, the ZIP hash, the supported game hash, the signature |
| `SHA256SUMS.txt` | checksums of the ZIP, manifest and signature |
| `build-info.json` | version, commit, file hashes (not uploaded) |
| `installer-pins.ps1.txt` | the `$script:RaPinnedRelease` block for the installer (not uploaded) |

The ZIP is reproducible: the same inputs give the same bytes. `--write-pins` writes the pin
block into `Configuration.ps1` and rebuilds `install.ps1`; commit both before `release`.

MSVC builds are not bit-reproducible, and the launcher and proxy compile in the reviewed DLL
hashes, so `build` never recompiles the CAPIE DLLs silently. `--rebuild-capie --minhook-dir ...`
runs `build.ps1` into the scratch folder `rebuild_output` (`plugin/build/capie-rebuild/`) and
stops with the new hashes; the reviewed folder is never written, and `build.ps1` refuses it as
an output folder. Have the new binaries reviewed, copy them into the reviewed folder, re-pin
them in `plugin/capie/deploy/bundle-inputs.json`, commit, then run `build` again. The reviewed
folder is not configured anywhere else: it is the folder the `source` entries of
`bundle-inputs.json` name (all of them must name the same one).

### Reviewed bundle hashes: `bundle-inputs.json`

`plugin/capie/deploy/bundle-inputs.json` is the one place where the reviewed CAPIE binaries are
pinned. `build_launcher.ps1` and `build_proxy.ps1` read it (and compile the hashes into the
launcher and the proxy), and the `build` step checks the build folder and the ZIP against it.
It is a JSON array with one object per file and exactly these keys:

| Key | Value |
| --- | --- |
| `name` | file name in `runtime/` (`capie_core.dll`, `capie_editor.dll`, `capie_injector.exe`) |
| `source` | path of the reviewed file, relative to the repository root |
| `sha256` | SHA-256 of the reviewed file, 64 **lower-case** hex digits |
| `dll` | `true` for a DLL the launcher injects, `false` for the injector |

The step refuses a file that is not valid JSON, has another key, an upper-case or short hash,
a non-boolean `dll` or a name twice, and it refuses when the names differ from
`reviewed_files` in `release.toml`. The SHA-256 tables in `DEPLOYMENT.md` are history only.

### Build identity of the shipped binaries

The manifest's `supportedGameSha256` and the release version come from
`plugin/capie/src/core/game_build.hpp`. So that the installer never promises a game build the
DLLs would refuse, `build` checks that every `.dll`/`.exe` in the ZIP (core, editor, injector,
launcher and, when present, `proxy/version.dll`) contains `game_exe_sha256` (either case) and
`capie_release` as ASCII or UTF-16LE text, not as part of a longer token (`1.5.1` does not match
`1.5.10`). A binary without them was built from another header: rebuild it, re-pin
`bundle-inputs.json`, commit, and run `build` again.

### Installer pins

`install.ps1 -Install` (without `-Latest`) installs the release pinned in
`tools/bootstrap/src/Shared/Configuration.ps1` (`$script:RaPinnedRelease`): version, ZIP name
and SHA-256, every file hash and `SupportedGameSha256`. After every install the installer
compares the player's `hoi4.exe` with that list and prints a clear notice, with the release page
and the update command, when it is not supported. So the list must be the one the binaries
accept. Never edit the block by hand; the build step writes it:

1. `python tools/release/release.py build --signing-key <key outside the repo> --write-pins`
   builds and signs the bundle, renders the block from it (`SupportedGameSha256` =
   `game_exe_sha256` of `game_build.hpp`, already checked inside every shipped PE file) into
   `installer-pins.ps1.txt` and `Configuration.ps1`, and rebuilds `install.ps1`.
2. Run the installer Pester suite, commit `Configuration.ps1` and `install.ps1`.
3. Run `release` from that commit (it refuses when the pins do not name this ZIP).

Without `--write-pins` the step only writes `installer-pins.ps1.txt` and warns. Until CAPIE
1.5.1 is released, the block still names CAPIE 1.5.0 for HOI4 1.19.3.0 (`hoi4.exe`
`7DC947BE...`); on HOI4 1.19.4.0 a pinned install therefore ends with the "does NOT support"
notice instead of a silent success.

The signing key path is a command-line option only. The script refuses a key inside the
repository and reads nothing from the key file except the public modulus and exponent (for the
signature check).

**release**: `gh release create v<version>` in `capie-mods/capie` (or `--repo`), at the exact
local commit (`--target <sha>`), with `--draft --prerelease`, the assets (ZIP, manifest,
signature, checksums, `install.ps1`) and notes made from `plugin/capie/CHANGELOG.md` plus the
supported game build, the multiplayer statement, SmartScreen guidance, the VirusTotal links and
the checksums. Before that it checks that the tree is clean, the installer pins name this ZIP,
the release does not exist yet and the commit is on GitHub. `--publish` and `--stable` each ask
for a typed word. Note: `install.ps1 -Update` reads `releases/latest`, which ignores drafts and
pre-releases, so players only get a release that is published and stable.

**virustotal** (ADR-0007): looks up the ZIP and every `.exe`/`.dll` in it by SHA-256. Files
VirusTotal already knows are not uploaded. For the others it asks you to type `upload`, because
an upload makes the file public. Results go to `virustotal.json` and `virustotal.md`, and from
there into the release notes. `--update-notes` refreshes the notes of an existing draft. The
key is read from `VT_API_KEY` (or `--api-key-env NAME`), sent only as the `x-apikey` header and
redacted from the output and the log.

**workshop**: for each item in `[[workshop.items]]` (or `--item <key>`), the step stages a fresh
copy under `dist/release/<version>/workshop/<item>-<version>/content`. The copy holds only
files tracked by git, without developer files (`exclude`). Its `descriptor.mod` has no `path`
or `archive` line, and has `remote_file_id` once the item has an ID. A change note or
description that still contains the word `DRAFT` is refused (preflight and the GitHub step
refuse it too). The step writes one `item.vdf` per item, asks every question (a visibility
other than Hidden, then `upload`), and only then runs SteamCMD once for all items, so you log
in once:

```text
steamcmd +login <account> +workshop_build_item <item 1.vdf> +workshop_build_item <item 2.vdf> +quit
```

SteamCMD reads the VDF without escape sequences: a `"` always ends a string and a backslash is
an ordinary character. The script therefore never escapes. It writes every `"` in a value as
`'` (straight quotes, as the design system asks for English text) and every backslash as `/`
(paths included). Before SteamCMD starts, it parses each VDF back with the same rules and stops
if any value would be read differently. It also stops while a `steamcmd.exe` from an
interrupted run is still running, because that process would print into the prompts. Ctrl+C
during an upload stops SteamCMD too.

| VDF key | Value |
| --- | --- |
| `appid` | 394360 |
| `publishedfileid` | from `release.toml`; 0 creates a new item |
| `contentfolder`, `previewfile` | the staged copy, the configured preview image (under 1 MB) |
| `visibility` | new item: 2 (Hidden); existing item: not sent (unchanged), unless `--visibility hidden\|unlisted\|friends\|public` |
| `title`, `description` | new item, or with `--update-description` |
| `changenote` | the item's changelog section for its `descriptor.mod` version, as BBCode |

Visibility values are Steam's `ERemoteStoragePublishedFileVisibility`: 0 public, 1 friends only,
2 private (shown as Hidden), 3 unlisted. Anything but Hidden asks you to type its name.

**Steam login.** Only the account name reaches SteamCMD (`--steam-user`, letters, digits and
`_` only). SteamCMD asks for the password and the Steam Guard code in your console itself. The
script does not accept, store or log them, and it does not capture SteamCMD's output at all.
Run the workshop step yourself, never from automation (ADR-0018).

After a new item is created, SteamCMD writes its ID into `item.vdf` (if it cannot be read, the
script asks you to type it). The script stores it as `published_file_id` in
`tools/release/release.toml`; commit that. Upload once more so the published `descriptor.mod`
carries `remote_file_id`. For the "CAPIE" switch mod, also set `capie_mod_workshop_id` in
`plugin/capie/src/launcher/config.hpp` and release a rebuilt launcher, so the launcher
recognises the Workshop copy (`mod/ugc_<id>.mod`) in the playset.

Compared with uploading through the Paradox Launcher: the launcher uploads the mod folder, which
must contain `descriptor.mod` and `thumbnail.png` (PNG, square, under 1 MB); the local `*.mod`
file is not uploaded. Copies that players subscribe to carry the item ID as `remote_file_id` in
their descriptor, and the launcher registers them as `mod/ugc_<id>.mod`. The staged copy follows
that layout. SteamCMD does not set Workshop tags (the launcher takes them from `tags` in the
descriptor), so set them on the item page once. Valve documents SteamCMD Workshop uploads as a
testing tool for developers, and this path has not been tried with HOI4 yet. Make the first
upload Hidden, subscribe to it and check it in the game before you make it visible.

## Version sources

| Version | Source of truth | Used for |
| --- | --- | --- |
| CAPIE release | `capie_release` in `plugin/capie/src/core/game_build.hpp` | tag `v<version>`, ZIP name, manifest, notes |
| CAPIE short version, build number | `capie_version`, `capie_build_number` (same file) | checked against `capie_release` (player status text, multiplayer handshake) |
| Supported game build | `game_exe_sha256`, `game_version` (same file) | manifest `supportedGameSha256`, installer pin `SupportedGameSha256` (with `--write-pins`), notes; checked inside every shipped PE file |
| Rename Anything | `version` in `mod/descriptor.mod` | Workshop change note (`CHANGELOG.md`) |
| "CAPIE" switch mod | `version` in `plugin/capie/toggle-mod/capie/descriptor.mod` | Workshop change note (`plugin/capie/toggle-mod/CHANGELOG.md`) |

Preflight also warns about the launcher's `-dev` version and about a player-facing description
that names an older CAPIE version.

## Configuration

`tools/release/release.toml` holds every path, the GitHub repository, the bundle layout, the
commands and the Workshop items with their IDs. Paths are relative to `root`, which is relative
to the file. After the repository split ([ADR-0034](../adr/0034-repository-split-evidence-and-plan.md)),
each repository keeps the tool and trims the file: `capie` keeps build, bundle, signing, GitHub,
VirusTotal and the switch-mod item; `rename-anything` keeps the Rename Anything item and its
preflight commands. Never put a password, token, API key or key path in the file.

## Tests

```powershell
uv run pytest tools/release/tests
```

The tests run against a synthetic repository in a temporary folder. Fakes stand in for git,
gh, `pwsh`, SteamCMD and the VirusTotal API, so nothing real is started or sent. They cover the
draft and pre-release defaults, the typed confirmations, Hidden for new items, the
account-name-only login, key placement, reviewed hashes, the manifest and its signature,
redaction of the API key and a dry run that sends nothing.
