# ADR-0026: Character portraits in the Characters list (backlog)

- Status: Proposed (backlog)
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Backlog funkcji CAPIE"

## Context

The name editor's Characters list shows names and role icons. Players asked for portraits there
so they can tell characters apart. HOI4 script has no getter for a character's portrait (see the
game's `loc_objects_documentation.md`).

## Decision

When it is built, the feature uses a native CAPIE localisation function on the character, for
example `GetPortraitSmall`, returning the sprite name of the small portrait with
`GFX_portrait_unknown` as the fallback. The mod shows a 32 to 40 px portrait before the role
icon (the role tooltip already exists), and hides it without CAPIE.

It comes after stage 2, with its own reverse engineering, review and in-game test.

## Consequences

- It is a display-only feature, so it falls under [ADR-0023](0023-display-only-features-switch-off-alone.md).
- It is item C6 of the roadmap in [ADR-0022](0022-capie-feature-roadmap.md).
