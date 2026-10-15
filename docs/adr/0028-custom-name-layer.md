# ADR-0028: A player-given country name is its own layer, not a cosmetic tag

- Status: Accepted, implemented as API level 4 (CAPIE 1.2 developer build)
- Date: 2026-10-13
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "2026-10-13 — nazwa
  nadana przez gracza to osobny byt"
- Design: [custom name layer report](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261013/custom-name-layer/report.txt)

## Context

Up to CAPIE 1.1 a custom country name was stored as a cosmetic tag (`RN1|Name|Adjective|`). In
the game test of subject renaming on 2026-10-13, renaming Canada also changed its flag, because
a cosmetic tag carries a flag and a colour as well as a name. The maintainer's ruling was
explicit: a custom name must not be treated like a cosmetic tag; it has to be a separate thing.

## Decision

- Renaming a country (one's own or a subject) never changes its flag or colour. The cosmetic tag
  does not carry the Rename Anything name.
- Display order: the player-given name, otherwise the current cosmetic tag, otherwise the base
  name.
- Reset removes only the player-given name. The country goes back to its current cosmetic tag
  (or its base name), not to the base name past the cosmetic tag.
- When the game or an event later changes the cosmetic tag, the player-given name stays.
- The entry point for renaming a subject sits with the diplomatic action buttons (at the top or
  bottom of the list), preferably with an icon.

## Consequences

- CAPIE 1.2 implements the layer as API level 4: country variables `capie_cn_n`, `capie_cn_a`,
  `capie_cn_ver`, `capie_cn_rev` ([CONTRACT.md](https://github.com/capie-mods/capie/blob/main/plugin/capie/CONTRACT.md), section 14).
- Countries renamed with CAPIE 1.0 or 1.1 keep their `RN1|` cosmetic tag until their first Apply
  or reset; the cosmetic tag that `RN1|` replaced was never stored and cannot be restored.
- The subject entry is a scripted diplomatic action with an icon.
- In the game test of 2026-10-14 the name changed with flag and colour unchanged, the console
  reset worked, and the names survived save, restart and load.

## Open question

Whether a player-given name should be reset when the cosmetic tag changes was reopened on
2026-10-14 ("2026-10-14 — flagi" in the decision log). For now it is not reset, which is how the
name layer behaves today. The maintainer has not settled it; when it is decided, the decision and
the contract ([CONTRACT.md](https://github.com/capie-mods/capie/blob/main/plugin/capie/CONTRACT.md), section 14.1) and the player
documentation must say so. The flag layer resets on a cosmetic tag change
([ADR-0032](0032-custom-flag-layer.md)).
