# ADR-0033: Recognise the host by comparing user names, fail closed

- Status: Accepted. Not implemented; needs a multiplayer test with a second client.
- Date: 2026-10-14
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "2026-10-14 —
  rozpoznawanie hosta", first bullet

## Context

The "host renames every country" option of the lobby rule
([ADR-0029](0029-game-rules-event-and-restore.md)) needs to know who the host is. The game has
no script trigger for it ([ADR-0013](0013-rename-permissions.md)), and CAPIE 1.3 sends no
`capie_host` record because the static candidate for a host flag could not be confirmed without
a runtime test ([CONTRACT.md](https://github.com/capie-mods/capie/blob/main/plugin/capie/CONTRACT.md), section 16). Workarounds based on
trust, such as a vote or a "take the host role" button, were on the table.

## Decision

- No trust-based solutions: no vote and no "take the host role".
- CAPIE exposes `get_my_username` and `get_host_username` in its API. A player is the host when
  the two values are equal.
- The result travels inside the command as the `capie_host` record, so every client takes the
  same branch in multiplayer.
- When either value cannot be read with certainty, no record is sent (fail closed).

## Consequences

- Until this exists, option 2 of the rights rule works in single player only.
- The implementation needs reverse engineering of where the game keeps the two user names, and a
  test with a second client before it can be relied on.
