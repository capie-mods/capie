# ADR-0004: Start CAPIE with a launcher that always goes through the Paradox Launcher

- Status: Accepted. The loading method was changed by [ADR-0024](0024-proxy-dll-loader.md) and
  again by [ADR-0030](0030-installation-paths-and-playset-switch.md); the requirements below
  still apply to every launch mode.
- Date: 2026-10-11
- Source: [distribution decision log](../evidence/decision-log/DISTRIBUTION-DECISIONS.md), decisions 2 and 9

## Context

CAPIE must be loaded into `hoi4.exe`. Players pick their mods in playsets in the Paradox
Launcher, and Rename Anything should work next to other mods.

## Decision

- The preferred way to switch CAPIE on is a launcher entry in Steam's launch options:
  `"...\RenameAnythingLauncher.exe" %command%`. Adding it is a deliberate opt-in; removing it gives
  plain HOI4 without the DLL.
- A desktop shortcut to the launcher with the same parameters is an equal second mode. The
  launcher may offer a "Create shortcut" button.
- Every mode (Steam, shortcut, double click) runs the chain launcher, then Paradox Launcher, then
  `hoi4.exe`, so the player can choose a playset and play with other mods.
- The launcher injects CAPIE only into a `hoi4.exe` that is a descendant of the process it
  started itself, never into an arbitrary running process.

## Consequences

- `%command%` points at the Paradox Launcher (`dowser.exe`), so the launcher must recognise the
  `hoi4.exe` that this Paradox Launcher creates. This needs a runtime test.
- Compatibility with other mods that override the politics window (`countrypoliticsview.gui`) or
  the same scripted GUIs needs a separate assessment.
