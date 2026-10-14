# ADR-0005: On an unknown game build, start the game without CAPIE

- Status: Accepted. [ADR-0016](0016-game-updates-signature-scan.md) adds an opt-in mode that can
  still enable CAPIE on an unknown build.
- Date: 2026-10-11
- Source: [distribution decision log](../evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 4

## Context

CAPIE hooks code at fixed places in one exact `hoi4.exe` (1.19.3.0, SHA-256
`7DC947BE...A031D5`). After a game patch those places can move.

## Decision

When the game build is not one CAPIE knows, the launcher starts the game without the DLL and
shows a clear message.

## Consequences

- A patch never blocks the game. Script-only features keep working; features that need CAPIE
  fall back as described in [ADR-0006](0006-script-only-fallback.md).
- Custom country names show as the original until a CAPIE release supports the new build.
