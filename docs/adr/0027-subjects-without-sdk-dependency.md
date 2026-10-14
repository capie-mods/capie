# ADR-0027: Renaming subjects; Rename Anything stays independent of the script SDK

- Status: Accepted
- Date: 2026-10-13
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "2026-10-13 — podmioty i
  stary edytor na CAPIE", first and second bullets

## Context

Overlords can now rename their subjects ([ADR-0013](0013-rename-permissions.md)). The subject
editor uses the CAPIE v1.1 editor grammar, which raises the question whether Rename Anything
should depend on the CAPIE script SDK, and how much of a subject the overlord may rename.

## Decision

- Rename Anything does not depend on the CAPIE script SDK. It does not read the SDK's host rule
  for now. This comes back with the multiplayer test, because checking a game rule that is not
  defined may cause parser errors when the SDK is absent.
- For a subject's party, only the ruling party's name is editable. That is enough.

## Consequences

- Rename Anything defines its own game rules ([ADR-0029](0029-game-rules-event-and-restore.md)).
- The subject editor has three tabs: country (name and adjective), leader, and ruling party.
