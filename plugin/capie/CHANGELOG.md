# CAPIE changelog

Release notes of the CAPIE native bundle (launcher, `capie_core.dll`, `capie_editor.dll`,
injector, proxy `version.dll`, the "CAPIE" switch mod inside the ZIP) published on GitHub
Releases. `tools/release/release.py` copies the section of the released version into the
release notes, so write it for players and mod authors. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the version is `capie_release` in
`src/core/game_build.hpp`.

## [Unreleased]

## [1.5.1] - unreleased

Support for Hearts of Iron IV 1.19.4.0 ("Operation Postern" update). The script API is unchanged
(level 7), so mods built for CAPIE 1.5.0 work without changes.

### Changed

- CAPIE now runs on HOI4 1.19.4.0 only (Steam, x64, `hoi4.exe` SHA-256 `B6650C85…D888`). On
  1.19.3.0 it stays off and the game starts without it, as on any other unsupported build.
- Every address, byte signature and structure offset was re-derived for the new build. The game
  changed the layout of edit boxes, the politics view, scripted windows, the focus manager and
  the multiplayer session, and now finds GUI widgets by name in per-type tables. CAPIE follows
  all of it; the details are in `docs/research/port-1.19.4.0.md`.

### Fixed

- CAPIE no longer refuses to start after the 1.19.4.0 game update.

## [1.5.0] - unreleased

First public release of CAPIE (Cyprian's API Extender), a native library that extends what
Hearts of Iron IV mods can do. Rename Anything is the first mod built on it.

### Added

- Custom country names and adjectives as a separate name layer (script API level 4).
- Faction names through the faction name layer (API level 5).
- Country flags: a native flag layer that shows any flag from the game's catalogue for a
  country, with a reset (API level 6).
- Leader portraits: a native portrait layer that shows a chosen portrait for a country's
  ruling leader, separate from the game's own data and with a reset (API level 7).
- Text fields with a native editor for script GUIs (the CAPIE grammar editors), with an
  on-screen keyboard fallback when CAPIE is not loaded.
- `CAPIE_*` status keys so mods can show whether CAPIE is loaded and which version.
- The CAPIE launcher for the Steam launch options or a shortcut. It loads CAPIE only when the
  "CAPIE" switch mod is enabled in the playset, and starts the game without CAPIE on an
  unsupported game build.
- The opt-in proxy loader (`version.dll`) as an alternative to the launcher.
- The installer (`install.ps1`): install, update from signed releases, roll back, disable and
  uninstall.
