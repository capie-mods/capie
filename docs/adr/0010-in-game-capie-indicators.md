# ADR-0010: Show in the game whether CAPIE is loaded

- Status: Accepted
- Date: 2026-10-11
- Source: [API decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/API-DECISIONS.md), first list items 1, 3
  and 7; "Decyzje do architektury CAPIE" item 4

## Context

A mod built on CAPIE can be enabled while CAPIE is missing. Without a visible sign the player
sees features silently not working, and a save played without CAPIE shows original names.

## Decision

- Every mod that uses the API ships a small snippet that shows a clear indicator, in the main
  menu among other places, when the mod is loaded but the DLL is missing.
- The main-menu indicator is as unobtrusive as possible. A line saying "CAPIE loaded" or
  "CAPIE missing" is enough.
- The "CAPIE missing" state must also be visible in a campaign, because playing on a save
  without CAPIE is an accepted risk ([ADR-0006](0006-script-only-fallback.md)).

## Consequences

- The look and wording are specified in [DESIGN-SYSTEM.md](https://github.com/capie-mods/capie-design/blob/main/docs/design/DESIGN-SYSTEM.md)
  ("CAPIE missing indicator in a campaign" and "main-menu indicator"). Both are planned.
- The status localisation keys (`CAPIE_STATUS` and related) are part of the script contract.
