# ADR-0008: Install CAPIE with a one-line PowerShell bootstrap

- Status: Accepted. Extended by [ADR-0030](0030-installation-paths-and-playset-switch.md)
  (terminal UI and a GitHub Releases installer).
- Date: 2026-10-11
- Source: [distribution decision log](../evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 8

## Context

The native part has to be downloaded, verified and wired into the game's launch. Doing that by
hand is error-prone and puts players off.

## Decision

A PowerShell bootstrap, started with one line in a terminal, asks interactively:

1. whether to install the launcher and DLL;
2. whether to add the launcher to Steam's launch options. The question states that the game with
   the DLL is not declared multiplayer compatible while the entry is set.

It also has an uninstall mode that reverts what it did.

## Consequences

- The bootstrap lives in `tools/bootstrap/` ([README](../../tools/bootstrap/README.md)). It refuses
  to install until a real release exists, because the release URL, ZIP hash and signing key are
  placeholders.
- Its output follows the CLI pattern of the design system (`OK`, `ASK`, `SKIP`, `NOTE`, `FAIL`).
