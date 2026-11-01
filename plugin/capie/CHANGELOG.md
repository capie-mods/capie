# CAPIE changelog

Release notes of the CAPIE native bundle (launcher, `capie_core.dll`, `capie_editor.dll`,
injector, proxy `version.dll`, the "CAPIE" switch mod inside the ZIP) published on GitHub
Releases. `tools/release/release.py` copies the section of the released version into the
release notes, so write it for players and mod authors. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the version is `capie_release` in
`src/core/game_build.hpp`.

## [Unreleased]

### Fixed

- The installer no longer stops with "Cannot find drive" when Steam still lists a library on a
  drive that is gone (for example an old USB or broken disk). It skips that library and keeps
  looking for the game (issue #2).

## [1.6.2] - 2026-11-12

Hotfix for Hearts of Iron IV 1.19.5.0 (the update of 12 November). Nothing else changed: the
script API is still level 8, so every mod that works with CAPIE 1.6.1 (and mods built for
CAPIE 1.5) works without changes.

### Changed

- CAPIE now runs on HOI4 1.19.5.0 only (Steam, x64, `hoi4.exe` SHA-256 `709F916A…FCE148`). On
  1.19.4.2 it stays off and the game starts without it, as on any other unsupported build. Steam
  updates the game automatically, so update CAPIE to 1.6.2 (run the installer again).
- Every address, byte signature, structure offset and class name was compared with 1.19.4.2:
  none of them changed, so every CAPIE feature works as in 1.6.1. The details are in
  `docs/research/port-1.19.5.0.md`.
- `CAPIE_BUILD` answers `1.6.2`; the build number sent as `capie_ver` is `10602`.

### Fixed

- CAPIE no longer refuses to start after the 1.19.5.0 game update.
## [1.7.0] - unreleased

Runtime catalogs, first part: mods can read what the running game has instead of shipping
generated lists. Script API level 9 (`capie_cap = 9`); mods built for CAPIE 1.6 keep working
unchanged. Runs on HOI4 1.19.4.2 only.

### Added

- Leader portraits from texture files (CONTRACT section 18, format 2): `capie_pt_ver = 2` stores
  the large and small portrait as a sprite name or a `gfx/...` `.dds`/`.png`/`.tga` path of up to
  126 characters (packing B, CONTRACT 21.3), so the portraits of total conversions that use image
  files (KaiserreduX's runtime leaders) can be chosen. CAPIE builds the entry with the game's own
  loader; a file that does not load shows the game's portrait. Format 1 is unchanged. The console
  command `change_leader_portrait` accepts a path.
- State/VP view (CONTRACT section 21): a scripted GUI named `capie_v1_cat__<mod>__<view>` gets,
  on its `…__vp_click`, the scope state's id and its victory-point provinces (`capie_st`,
  `capie_vp_<k>`, `capie_vpv_<k>`), and the keys `CAPIE_V1_T__<MOD>__<VIEW>__<k>` show each
  province's name as the game shows it (a renamed city's new name within half a second). This
  works for any map, also a total conversion's.
- Every Apply also reports `capie_catalog` (1 when the catalog views work in this game).

### Changed

- Status text "CAPIE 1.7 loaded"; `CAPIE_API` answers `9`, `CAPIE_BUILD` answers `1.7.0`.

### Not yet

- The flag and portrait pickers of runtime catalogs (CONTRACT 21, "pending spikes"): their
  actions are answered with `capie_catalog = 0` in this version.

## [1.6.1] - 2026-11-05

Hotfix for Hearts of Iron IV 1.19.4.2. Nothing else changed: the script API is still level 8, so
every mod that works with CAPIE 1.6.0 (and mods built for CAPIE 1.5) works without changes.

### Changed

- CAPIE now runs on HOI4 1.19.4.2 only (Steam, x64, `hoi4.exe` SHA-256 `BCCC6290…7A8037`). On
  1.19.4.1 it stays off and the game starts without it, as on any other unsupported build. Steam
  updates the game automatically, so update CAPIE to 1.6.1 (run the installer again).
- Every address and byte signature was re-checked for the new build. Code and data moved by a
  few hundred bytes to a few kilobytes; no structure layout changed. The details are in
  `docs/research/port-1.19.4.2.md`.
- `CAPIE_BUILD` answers `1.6.1`; the build number sent as `capie_ver` is `10601`.
- The "CAPIE" switch mod in the ZIP (`mod/capie`, the local copy) is version 1.1.2: no CAPIE mark
  in the main menu any more, and no interface, graphics or localisation file. Its copy of the
  game's main menu file replaced the main menu of total conversions such as Kaiserreich.

### Fixed

- CAPIE no longer refuses to start after the 1.19.4.2 game update.
- Updating no longer fails when an antivirus (Microsoft Defender reports
  `Trojan:Win32/Posilod.CA!cl`, a false positive) quarantines the optional proxy `version.dll`.
  The proxy is now a separate download, `CAPIE-1.6.1-proxy-win64.zip`, listed with its SHA-256 in
  the signed `release-manifest.json`. Only the Proxy method fetches it, so the default Launcher
  install and update never download, unpack or read `version.dll`. If an antivirus blocks a file
  the setup does need, it stops with a clear message: which file, that it is a false positive,
  its SHA-256 and release page, and how to restore or allow that one file in Windows Security >
  Protection history, instead of a raw `Get-FileHash` error.

## [1.6.0] - 2026-11-01

A new thing a mod can change in-game: a country's map colour. Script API level 8
(`capie_cap = 8`); mods built for CAPIE 1.5 keep working unchanged. Runs on HOI4 1.19.4.1 only.

### Added

- Country colour (CONTRACT section 19): a mod can give a country its own map colour with the
  `capie_cc_*` variables. The political map, the claims map mode, borders, the faction colour and
  UI icons follow at once; restoring the variables brings the game's colour back. A cosmetic tag
  set after the choice hides it, as for the flag. A colour change redraws only the map modes that
  show country colours, so it adds nothing to `error.log`.
- Every Apply now also reports `capie_colour` (1 when the layer works in this game), so a mod can
  grey out its colour editor when CAPIE cannot do it.

### Changed

- Status text "CAPIE 1.6 loaded"; `CAPIE_API` answers `8`, `CAPIE_BUILD` answers `1.6.0`.
- CAPIE runs on HOI4 1.19.4.1 only (Steam, x64, `hoi4.exe` SHA-256 `CF781ED1…10476F`), like
  1.5.2. The checks added since 1.5.1 (the scripted-window field stores, the window manager
  vtable, the RTTI class check of every vtable) were re-checked for the new build; none of them
  moved. The details are in `docs/research/port-1.19.4.1.md`.
- Less work every frame: CAPIE recognises the game's scripted windows once, when they are bound,
  instead of re-checking each one every frame.
- A window the game has destroyed is never touched; CAPIE makes no game call for it.
- A C++ exception inside one of CAPIE's game calls is handed to the game's own crash reporter
  instead of ending the game without a report.

## [1.5.2] - 2026-10-29

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

## [1.5.1] - 2026-10-28

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
