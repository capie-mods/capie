# ADR-0035: Releases are built and published locally for now

- Status: Accepted. Partly supersedes the CI build in [ADR-0031](0031-trust-without-paid-code-signing.md).
- Date: 2026-10-14

## Context

ADR-0031 assumed that release binaries would be built in GitHub Actions, mainly so a free
SignPath Foundation certificate could be requested later. The maintainer asked to postpone CI and
to have one local tool that builds, creates the GitHub release and uploads the mods to the Steam
Workshop.

## Decision

- Releases are made with `uv run tools/release/release.py` on the maintainer's machine
  ([guide](../contributing/release-script.md)). It covers preflight checks, the release ZIP with
  the signed manifest, a draft prerelease on GitHub, an optional VirusTotal scan and the Workshop
  upload through SteamCMD.
- GitHub releases start as draft prereleases. Workshop items start as Hidden. Publishing either
  needs an explicit flag and a typed confirmation.
- Steam credentials are typed only into SteamCMD; the tool never reads, stores or logs them.
- GitHub Actions is not set up yet. It comes back on the table before applying to SignPath,
  which requires builds from CI.

## Consequences

- Every CAPIE rebuild changes the DLL hashes pinned in the launcher and proxy, so a new build
  stops for review and a pin update commit before the release ZIP is made.
- Players only receive updates through `install.ps1 -Update` once a release is published as
  stable; drafts and prereleases are invisible to it.
- Uploading HOI4 mods with SteamCMD is not verified yet; the first upload is Hidden and is checked
  in game.
