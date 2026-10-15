# ADR-0006: Without CAPIE the mod falls back to scripts and says so

- Status: Accepted
- Date: 2026-10-11
- Source: [distribution decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md), decision 5;
  [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), first list, item 3

## Context

CAPIE can be missing: not installed, an unknown game build, a failed start, or a save opened
after CAPIE was uninstalled. The mod still has to load and behave sensibly.

## Decision

- Scripts detect whether the native capabilities are present.
- Without them the in-place text fields are hidden. States, cities and characters still work
  through the scripted name editor. The country name and the text field are unavailable, with a
  message that says why.
- A save played on after CAPIE was removed is an acceptable risk, but the "CAPIE missing" state
  must be visible in the game ([ADR-0010](0010-in-game-capie-indicators.md)).

## Consequences

- Every feature is labelled by whether it needs CAPIE, in game and in the documentation.
- Saved names stay in the save; a country with a custom name shows its game name until CAPIE is
  loaded again.
