# ADR-0022: CAPIE feature roadmap for versions 1.1 and 1.2

- Status: Accepted. v1.1 is implemented; the portrait work planned for v1.2 has not started, and
  v1.2 shipped the country name layer first ([ADR-0028](0028-custom-name-layer.md)).
- Date: 2026-10-12
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Decyzje roadmapy funkcji
  CAPIE (2026-10-12, po api-gap-analysis.md)" items 1, 4 and 5
- Background: API gap analysis in [monorepo archive: docs/evidence/session-20261011/claude-api-gaps/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261011/claude-api-gaps)

## Context

The API gap analysis listed what modders ask for and what CAPIE could offer (items C1 to C11).
Some need no new reverse engineering, others do.

## Decision

1. Order, decided by the development lead: v1.1 brings C1, public text fields for other mods,
   together with C8 (prefill and focus), C4 (status keys), C5 (SDK text tools) and C3 (Latin
   alphabet). These have the highest demand and need no new reverse engineering. C6, character
   portraits, goes into v1.2 after a separate reverse engineering session (research questions RQ1
   to RQ3; see [ADR-0026](0026-character-portraits-backlog.md)).
2. The CAPIE documentation names the alternatives openly: `hoi4_lua` and an injector based on a
   proxy DLL.
3. The scripted GUI profiler (C11) is public, and exists only in the debug variant. It answers
   performance problems modders report and has no effect on release builds. Decided by the
   development lead.

Display-only features got their own rule on the same day: [ADR-0023](0023-display-only-features-switch-off-alone.md).

## Consequences

- The alternatives page of the CAPIE site exists ([alternatives](https://github.com/capie-mods/capie/blob/main/sites/capie/docs/alternatives.md)).
