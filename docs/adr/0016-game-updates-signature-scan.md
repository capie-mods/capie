# ADR-0016: After a game patch, an opt-in signature scan, all or nothing

- Status: Accepted. Amended by [ADR-0023](0023-display-only-features-switch-off-alone.md) for
  features that only change what is displayed.
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Decyzje do architektury CAPIE" item 10

## Context

CAPIE is pinned to one `hoi4.exe` by SHA-256. Every patch would switch it off until a new release
([ADR-0005](0005-unknown-game-build.md)). The goal is that after a patch all features work, not a
subset, and that no new CAPIE release is needed when a patch does not touch the places CAPIE
uses.

## Decision

Option (a) of the research:

- On an unknown game SHA, CAPIE runs a signature scanner at runtime.
- It enables its features only when every pattern (functions and structure offsets in code) is
  found exactly once and every hooked code region is byte-identical to the tested one.
- Otherwise CAPIE does not start at all (all or nothing). The game runs without it and shows a
  message.
- On an unverified game build a discreet marker is visible.
- The mode is off by default and the player switches it on. That default was recommended by the
  development lead and can be changed at the maintainer's request.

## Consequences

- With the mode on, the rule of [ADR-0005](0005-unknown-game-build.md) no longer applies to
  builds that pass the scan. With the mode off, nothing changes.
- The known builds and their status are listed in `versions.json` ([ADR-0017](0017-versions-json.md)).
- The build check in `plugin/capie/src/core/game_build.hpp` refers to this decision as
  "decision 10(a)".
