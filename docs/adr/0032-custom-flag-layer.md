# ADR-0032: A player-chosen flag as its own attribute

- Status: Accepted. The design and its independent review exist; implementation was approved on
  2026-10-14 and has not landed yet.
- Date: 2026-10-14
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "2026-10-14 —
  uprawnienia, frakcje, eventy, flagi", fourth bullet; "2026-10-14 — flagi"; "2026-10-14 —
  rozpoznawanie hosta", second bullet
- Design: [design.txt](../evidence/session-20261014/flag-layer/design.txt), independent review:
  [review.txt](../evidence/session-20261014/flag-layer/review.txt)

## Context

Since [ADR-0028](0028-custom-name-layer.md) a custom name no longer changes the flag. Some
players still want to pick a flag for their country.

## Decision

- A player's choice of flag is a separate attribute, like the name layer and independent of the
  cosmetic tag. It gets a good-looking UI. The design comes first, then an independent review of
  whether this is the best way to do it.
- The pool of flags depends on a game rule. Default: every flag in the game. Other options: the
  country's own pool (its tag, its ideology variants and matching cosmetic tags), or the
  country's own pool plus the overlord's flag.
- Becoming a subject does not change the flag. This is the game's behaviour: the flag comes from
  the cosmetic tag or the tag.
- A flag the player chose (the overlord's flag included) is kept for good, also after the country
  becomes independent, until the cosmetic tag changes. A cosmetic tag change always resets the
  player's flag.
- The flag has its own cooldown, of the same length as the name's. This default recommendation
  was accepted.
- After the design and the review, the decision was to implement it as designed, with the review's
  corrections K1 to K9 and the rules above.

## Consequences

- Flag and name behave differently on a cosmetic tag change: the flag resets, the name layer
  stays ([ADR-0028](0028-custom-name-layer.md)). Whether the name should also reset is undecided;
  see the open question in ADR-0028.
- The review found the native approach of the design sound (replacing the flag candidates when
  an atlas slot loads) and asked for corrections, among them a reset on cosmetic tag change that
  the first design did not have.
- Nothing is implemented yet.
