# ADR-0011: Versioned API, graceful degradation, minimal footprint

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), first list items 5 and 8

## Context

CAPIE is meant for mods by other authors. Those mods will be built against different CAPIE
versions and may send data CAPIE does not expect.

## Decision

- The API is versioned. Mods by other authors degrade safely when CAPIE is missing or too old.
- CAPIE is as non-invasive as possible.
- CAPIE validates all data that comes from mods by other authors.

## Consequences

- Mods require a minimum API level (`capie_min_api`), never a release. The API level only goes up
  and a level is never reused ([CONTRACT.md](../../plugin/capie/CONTRACT.md), section 1).
- Malformed input is rejected and nothing changes (the SDK error codes in section 10 of the
  contract).
