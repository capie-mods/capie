# ADR-0025: Console commands ship together, after review and an in-game test

- Status: Accepted. The developer build passed a joint single-player test on 2026-10-13; the
  commands are not in release builds yet.
- Date: 2026-10-13
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "Komendy konsoli CAPIE (2026-10-13)"

## Context

CAPIE can register commands in the game's own console: `change_nation_name` and
`reset_nation_name` for players and testers, and `capie_cmd` for modders (API level 3).

## Decision

`capie_cmd` is enabled together with `change_nation_name` and `reset_nation_name`, and only after
an independent review of the candidate and a single-player test in the game covering save and
load, refusal in multiplayer and ironman, Polish letters, and `help`. The API level 3 contract is
added to `plugin/capie/CONTRACT.md`.

## Consequences

- The contract lives for now in [CONTRACT-console.md](../../plugin/capie/CONTRACT-console.md) and
  will be merged into `CONTRACT.md`.
- Since API level 4 the two name commands write the country name layer
  ([ADR-0028](0028-custom-name-layer.md)) instead of a cosmetic tag.
- The commands are compiled only into the developer build (`build.ps1 -Console`).
