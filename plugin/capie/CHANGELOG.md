# CAPIE changelog

Release notes of the CAPIE native bundle (launcher, `capie_core.dll`, `capie_editor.dll`,
injector, proxy `version.dll`, the "CAPIE" switch mod inside the ZIP) published on GitHub
Releases. `tools/release/release.py` copies the section of the released version into the
release notes, so write it for players and mod authors. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the version is `capie_release` in
`src/core/game_build.hpp`.

## [Unreleased]

## [1.6.0] - unreleased

Two new things a mod can change in-game: a country's map colour and the party-ideology line of the
politics panel. Script API level 8 (`capie_cap = 8`); mods built for CAPIE 1.5 keep working
unchanged. Runs on HOI4 1.19.4.1 only.

### Added

- Country colour (CONTRACT section 19): a mod can give a country its own map colour with the
  `capie_cc_*` variables. The political map, borders, the faction colour and UI icons follow at
  once; restoring the variables brings the game's colour back. A cosmetic tag set after the choice
  hides it, as for the flag.
- Ruling-party ideology name (CONTRACT section 20): a mod can rename the ideology line of the
  ruling-party box in the politics panel (for example "Non-Aligned") with the `capie_sn_*`
  variables. The name stays only while the same party and leader rule; an election, coup or new
  leader brings back the game's name. It shows the next time the politics tab is opened.
- Every Apply now also reports `capie_colour` and `capie_subideo` (1 when the layer works in this
  game), so a mod can grey out these editors when CAPIE cannot do it.

### Changed

- Status text "CAPIE 1.6 loaded"; `CAPIE_API` answers `8`, `CAPIE_BUILD` answers `1.6.0`.
- CAPIE runs on HOI4 1.19.4.1 only (Steam, x64, `hoi4.exe` SHA-256 `CF781ED1…10476F`), like
  1.5.2. The checks added since 1.5.1 (the scripted-window field stores, the window manager
  vtable, the RTTI class check of every vtable) were re-checked for the new build; none of them
  moved. The details are in `docs/research/port-1.19.4.1.md`.

## [1.5.2] - unreleased

Hotfix for Hearts of Iron IV 1.19.4.1. The script API is unchanged (level 7), so mods built for
CAPIE 1.5.0 and 1.5.1 work without changes.

### Changed

- CAPIE now runs on HOI4 1.19.4.1 only (Steam, x64, `hoi4.exe` SHA-256 `CF781ED1…10476F`). On
  1.19.4.0 it stays off and the game starts without it, as on any other unsupported build.
- Every address and byte signature was re-checked for the new build. Code and data moved by a
  few hundred bytes; no structure layout changed. The details are in
  `docs/research/port-1.19.4.1.md`.

### Fixed

- CAPIE no longer refuses to start after the 1.19.4.1 game update.

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
- The launcher always starts the game. If Steam's command names a program the launcher does not
  know (in the game folder), the program needs administrator rights, or the launcher hits an
  internal error before the start, the game now starts unchanged without CAPIE and a notice says
  why. Only a missing program is still refused.
- Steam can update `hoi4.exe` while the launcher waits for the game; with CAPIE off the launcher
  holds no file open at all.
- Launcher notices now appear on top of the Paradox Launcher and the game. The "unsupported game
  version" notice says that CAPIE needs an update and where to get it. The outcome of the last
  run is written to `%LOCALAPPDATA%\RenameAnything\last-status.txt`.
- Crash-loop breaker: if a start ended while CAPIE was loading (launcher or proxy), the next start
  runs without CAPIE once, says so, and the start after it tries again.
- The proxy `version.dll` pins the same CAPIE DLLs as the launcher (one list,
  `deploy/bundle-inputs.json`); before, it could pin an older build and refuse the shipped DLLs.

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
