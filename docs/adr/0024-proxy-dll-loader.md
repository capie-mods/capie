# ADR-0024: Proxy DLL in the game folder as a loading method

- Status: Accepted 2026-10-12 as the primary loading method. Superseded in part by
  [ADR-0030](0030-installation-paths-and-playset-switch.md), which makes the launch wrapper with
  the playset switch the way CAPIE is switched on and keeps the proxy as the alternative if
  players find installation hard.
- Date: 2026-10-12
- Source: [distribution decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 10 and its update

## Context

The launcher of [ADR-0004](0004-launcher-through-paradox-launcher.md) needs a launch option or a
shortcut and has to find the right `hoi4.exe`. A small DLL that the game loads by itself from its
own folder asks less of the player.

## Decision

- A small loader with the name of a system library that `hoi4.exe` loads from its folder forwards
  every export to the original in System32. The name was chosen after an analysis of imports and
  conflicts: `version.dll` (three imported functions, 17 exports forwarded to System32).
- It activates CAPIE only on purpose: the `-capie` flag in Steam's launch options or, if the
  Paradox Launcher does not pass it on, a configuration file written by the bootstrap. It first
  verifies the hashes of the game and of the CAPIE package in
  `%LOCALAPPDATA%\RenameAnything\current\`.
- On an unknown game build it loads nothing.
- Uninstall removes the file (through the bootstrap).
- The launcher in wrapper mode stays as the alternative for players who want no file in the game
  folder.
- The goal is the least work for the player.
- For the test, the maintainer agreed to a test file in the game folder for the duration of the
  test, removed and verified afterwards.

## Consequences

- Implementation and test limits: [proxy loader](../contributing/proxy-loader.md).
- A delayed thread does not guarantee that hooks are in place before the game's first call.
- Whether Steam and the Paradox Launcher pass `-capie` on still needs confirmation; the bootstrap
  has an explicit configuration variant.
- No result claims multiplayer compatibility.
- The bootstrap still defaults to `-LoadMethod Proxy`. Aligning it with ADR-0030 is open work.
