# ADR-0012: Multiplayer level L0: version handshake, feature lock and banner

- Status: Accepted. Supersedes the first multiplayer decision of 2026-10-11 (CAPIE rejects
  players without it).
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), first list item 4;
  "Decyzje do architektury CAPIE" item 2; "Korekty po analizie RE", first bullet

## Context

The first decision was that the DLL checks the other players and rejects those who do not have
it or have an incompatible version. Reverse engineering of the game showed no safe native way to
kick players, and the game does not tell a script effect who sent a command.

## Decision

- Multiplayer starts at level L0: every CAPIE command carries a script-level version handshake
  (`capie_ver`, stored in a country variable). Features are locked and a visible banner or
  warning is shown to everyone until every player has confirmed the same CAPIE build.
- CAPIE does not kick players natively. Rejecting players stays a research topic.

## Consequences

- The handshake is specified in [CONTRACT.md](https://github.com/capie-mods/capie/blob/main/plugin/capie/CONTRACT.md), section 12. It
  runs on synchronised state only, so every client takes the same branch.
- Multiplayer has not been tested. A test with a second player is planned after single player
  is finished ([multiplayer test plan](../contributing/multiplayer-test-plan.md)). Nothing in the
  documentation claims multiplayer compatibility.
