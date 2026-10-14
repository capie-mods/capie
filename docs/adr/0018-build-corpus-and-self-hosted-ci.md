# ADR-0018: Keep a private corpus of game builds and run CI on a machine with the game

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "Decyzje do architektury CAPIE" items 8 and 9

## Context

Signature scanning ([ADR-0016](0016-game-updates-signature-scan.md)) can only be tested against
real game builds, old and new. The offline tests that check pins need the game's executable,
which cannot be redistributed or put on a hosted CI runner.

## Decision

- Build a corpus of older game builds. From now on every new `hoi4.exe` is archived by default,
  privately and never redistributed.
- Tools that need Steam login data are run only by the maintainer, never by automation or by
  other contributors on the maintainer's behalf.
- CI runs on the maintainer's computer (a self-hosted runner with the game installed).

## Consequences

- Contributors without the game can run the tests that do not need `hoi4.exe`; pin checks run on
  the self-hosted runner.
- The corpus never leaves the maintainer's machine.
