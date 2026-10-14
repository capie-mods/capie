# ADR-0009: The CAPIE script contract passes data through variables (variant B)

- Status: Accepted
- Date: 2026-10-11 (direction), 2026-10-11 (variant chosen)
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), first list item 1 and
  "Decyzje do architektury CAPIE" item 1
- Background: [architecture research](../evidence/session-20261011/claude-api/)

## Context

Mods could talk to CAPIE in two ways:

- **Variant A:** new native script keywords (for example a `set_custom_country_name` effect).
- **Variant B:** a protocol over plain HOI4 script: temporary variables of one command, scripted
  effects and, at the time, the cosmetic tag.

The first goal was both, with the weight on the integration that is most stable across game
patches and makes updating the DLL after a patch painless. How a mod passes free text through
the API was left as a research question, not a decision.

## Decision

The contract is variant B. Native keywords (variant A) may come later as an addition on top of
it.

## Consequences

- A mod written against the contract parses and runs the same way with or without CAPIE; only
  the branch it takes differs. See [CONTRACT.md](../../plugin/capie/CONTRACT.md).
- Every mod that uses CAPIE ships the indicator snippet of [ADR-0010](0010-in-game-capie-indicators.md).
- Since API level 4 the country name is no longer carried by the cosmetic tag
  ([ADR-0028](0028-custom-name-layer.md)).
