# Releasing

The first release was CAPIE 1.5.0 (2026-10-15). This page is the checklist for every release, the first one and the ones after
it. It follows the distribution decisions: [ADR-0002](../adr/0002-distribution-model.md),
[ADR-0007](../adr/0007-antivirus-scan-before-release.md),
[ADR-0017](../adr/0017-versions-json.md), [ADR-0030](../adr/0030-installation-paths-and-playset-switch.md)
and [ADR-0031](../adr/0031-trust-without-paid-code-signing.md). Nothing is published, uploaded or
pushed without the maintainer's explicit go-ahead.

Steps 1, 3 (installer pins), 4, 5 (VirusTotal), 6 and 7 are automated on the maintainer's
machine by [`tools/release/release.py`](release-script.md). It creates a draft pre-release and
Hidden Workshop items unless told otherwise. Open decision: step 2 and
[ADR-0031](../adr/0031-trust-without-paid-code-signing.md) say release binaries come from CI
(GitHub Actions), but the script packages a local build. Either amend ADR-0031 (local builds
by the maintainer, ADR-0018), or build in CI and use the script only for the remaining steps.

## What a release contains

| Part | Where it goes |
|---|---|
| Rename Anything (`mod/`) | Steam Workshop item |
| "CAPIE" switch mod (`plugin/capie/toggle-mod/`) | Steam Workshop item |
| CAPIE Script SDK (`sdk/capie-script-sdk/mod/`) | Steam Workshop item, when the SDK ships |
| Launcher, `capie_core.dll`, `capie_editor.dll`, injector, proxy `version.dll`, MinHook license | ZIP on GitHub Releases |
| `release-manifest.json` and its signature `.sig` | GitHub Releases |
| `install.ps1` | GitHub Releases and the repository |
| `versions.json` (signed) | repository and GitHub Releases |

## Checklist

1. **Code is reviewed and tested.** Independent review of every changed native file. All offline
   suites, mutation checks and lint pass ([building and testing](building-and-testing.md)). The
   release build passed the in-game checks for every feature it ships
   ([in-game test record](in-game-test-record.md)).
2. **Build in CI.** Release binaries are built by GitHub Actions from a tagged commit. MSVC output
   is not bit-reproducible (PE timestamps), so the hashes in the manifest are the hashes of the
   CI build, never of a local build.
3. **Pin the binaries.** Update the hashes the launcher and the proxy compile in, and the
   bootstrap release configuration (`tools/bootstrap/src/Shared/Configuration.ps1`, then
   `build-install.ps1`). The replacements needed for the first CAPIE release are listed in
   [plugin/capie/deploy/DEPLOYMENT.md](https://github.com/capie-mods/capie/blob/main/plugin/capie/deploy/DEPLOYMENT.md).
4. **Manifest and signature.** Make and sign the manifest offline with
   `tools/bootstrap/sign-release-manifest.ps1` (`-MakeManifest`, `-Sign`). The private key never
   enters the repository. The manifest lists every file of the ZIP with its SHA-256 and the
   supported `hoi4.exe` hashes.
5. **Scan.** Scan the final artifacts with Windows Defender locally, then upload them to
   VirusTotal. An upload makes the file public, so it needs the maintainer's consent for each
   release. Put the VirusTotal links in the release notes. Every release, also report each
   remaining detection as a false positive to its vendor and record it in the status table of
   [antivirus false positives](antivirus-false-positives.md): new hashes are flagged again even
   when the previous build was cleared. In the release notes, explain in a few sentences why
   ML engines flag the files (DLL loading into the game, new unsigned files) and that the
   reports have been sent.
6. **Release notes.** What changed; which game build is supported; which features are switched
   off on which build ([ADR-0023](../adr/0023-display-only-features-switch-off-alone.md)); the
   VirusTotal links; how to get past the SmartScreen warning for unsigned files; the
   multiplayer statement (not supported until tested).
7. **Workshop.** Upload the mod and the switch mod with their descriptions in English and Polish
   (`docs/assets/promo/workshop/`: one BBCode file per item and language, the English one is the
   `description_file` in `tools/release/release.toml`).
8. **Documentation.** Fill in `OWNER` and `TAG` in the install commands, set `site_url` in both
   `zensical.toml` files, and enable the Pages workflow from `deploy-pages.yml.example`
   ([sites/README.md](https://github.com/capie-mods/capie-design/blob/main/sites/README.md)).

## Signing

There is no paid code-signing certificate. Once the repository is public the project applies to
the SignPath Foundation, which signs open source projects for free. Until then trust rests on
open code, CI builds, the signed manifest and the VirusTotal links.

## Not decided yet

- The final file and folder names: the install folder and shortcut still carry the Rename
  Anything name from before the naming decision ([ADR-0001](../adr/0001-product-names.md)).
- The license file. MIT is intended and not yet in place ([ADR-0003](../adr/0003-open-source-mit.md)).
