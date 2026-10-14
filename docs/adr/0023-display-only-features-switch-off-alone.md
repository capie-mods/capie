# ADR-0023: Display-only features may switch off on their own

- Status: Accepted. Amends [ADR-0016](0016-game-updates-signature-scan.md).
- Date: 2026-10-12
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "Decyzje roadmapy funkcji CAPIE" item 2

## Context

[ADR-0016](0016-game-updates-signature-scan.md) switches CAPIE off as a whole when any pin does
not match. For features that only change what is drawn, losing all of CAPIE because one of them
broke is a poor trade.

## Decision

Features that only display things (C2 view refresh, C6 portraits, C7 player name) may switch off
separately when their pins stop matching after a patch. The all-or-nothing rule still applies to
features that change game state and to the text transport.

## Consequences

- The country name layer of CAPIE 1.2 uses this exception: its pins are checked separately from
  the localisation hooks, and when they fail the layer is off and Rename Anything refuses country
  renames ([CONTRACT.md](../../plugin/capie/CONTRACT.md), section 14.4).
- Release notes must say which feature was switched off.
