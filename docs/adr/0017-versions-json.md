# ADR-0017: A signed versions.json in the repository and in every release

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), "Decyzje do architektury CAPIE" item 11

## Context

CAPIE has to know which game builds it supports and in which state (verified, pending, refused).
That list changes more often than the code.

## Decision

`versions.json` is kept in the repository, signed, and shipped with every release.

## Consequences

- The format is described on the CAPIE site ([game updates](https://github.com/capie-mods/capie/blob/main/sites/capie/docs/game-updates.md)).
- The signature uses the same project key as the release manifest
  ([ADR-0031](0031-trust-without-paid-code-signing.md)).
