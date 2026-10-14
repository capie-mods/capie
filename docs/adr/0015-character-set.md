# ADR-0015: Character set: extended Latin first, Cyrillic later

- Status: Accepted
- Date: 2026-10-11, refined 2026-10-12 and 2026-10-13
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "Decyzje do architektury
  CAPIE" item 5; "Decyzje roadmapy funkcji CAPIE" item 3; "2026-10-13 — podmioty i stary edytor"
  third bullet

## Context

Names typed by players go through the game's fonts, its script parser and its command transport.
Rename Anything started with ASCII letters, digits, a few punctuation marks and the Polish
letters. Players of other languages want accents, umlauts and Cyrillic.

## Decision

- Extend the allowed characters (Cyrillic, accents, umlauts) as far as HOI4's fonts, parser and
  transport support them. This needs research before each step.
- CAPIE 1.1 adds the extended Latin alphabet. Cyrillic comes later, because it depends on fonts
  and on the game language.
- Rename Anything keeps its own alphabet unchanged. Letters such as `é` are rejected with a
  message.

## Consequences

- The public alphabet (codes 1..264) is in [CONTRACT.md](../../plugin/capie/CONTRACT.md),
  section 7. Rename Anything uses its private 84-code alphabet until it moves onto the public
  contract ([MIGRATION.md](../../sdk/capie-script-sdk/MIGRATION.md)).
- Typing and drawing extended Latin letters still needs an in-game test (contract section 13).
