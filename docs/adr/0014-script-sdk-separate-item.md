# ADR-0014: The CAPIE script SDK is its own Workshop item and repository

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "Decyzje do architektury CAPIE" item 3

## Context

Mods that use CAPIE need the same script helpers: reading fields, trimming and validating text,
setters, the multiplayer handshake. Copying them into every mod would fork the contract.

## Decision

The CAPIE script SDK ships as a separate Steam Workshop item ("CAPIE Script SDK") that mods list
as a dependency, and lives in its own repository (`capie-script-sdk`) in the GitHub organisation.

## Consequences

- Until the repository split it lives in `sdk/capie-script-sdk/`
  ([README](../../sdk/capie-script-sdk/README.md)).
- Rename Anything does not depend on the SDK for now ([ADR-0027](0027-subjects-without-sdk-dependency.md)).
