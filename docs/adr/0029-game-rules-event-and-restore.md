# ADR-0029: Lobby rules for rename rights and a rename news event; restoring a name uses the cooldown

- Status: Accepted. Script implemented; not yet tested in the game.
- Date: 2026-10-14
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), "2026-10-14 —
  uprawnienia, frakcje, eventy, flagi", first three bullets

## Context

With subjects and factions renameable, one fixed permission rule does not fit every campaign. A
renamed country should also be noticed by other players if they want that.

## Decision

- A game rule "who may rename" with three options:
  1. own country and own subjects (default);
  2. the host renames every country;
  3. every player renames only their own country.

  Faction names get the same options.
- "Restore name" uses the same cooldown as a rename. This replaced an earlier decision of the
  same morning, under which restoring did not use the cooldown.
- A game rule "rename news event", off by default. When on, a generic event "X changed its name
  to Y" fires, where X is the last name actually shown (player-given name, otherwise cosmetic
  tag, otherwise base name). Its description uses the adjective among other things. It covers
  one's own country and subjects.

## Consequences

- Rename Anything defines `rn_edit_rights` and `rn_rename_event` in
  `mod/common/game_rules/rn_rules.txt`. The event is a major news event, `rn_rename.1`.
- The game has no host trigger. In single player the player is the host; in multiplayer the host
  needs a record from CAPIE (`capie_host`). It was requested for API level 5, and CAPIE 1.3 does
  not send it, because what marks the host could not be established without a runtime test
  ([CONTRACT.md](../../plugin/capie/CONTRACT.md), section 16). How the host will be recognised is
  decided in [ADR-0033](0033-host-recognition.md). Until then option 2 works in single player only.
- The old name in the event title needs a CAPIE record of the displayed name (`capie_pf1_*`).
  Without it the title falls back to the previous player-given name or to a title without the old
  name. CAPIE 1.3 (API level 5, developer build, not yet run in the game) sends this record.
- Option 3 for factions: the faction leader still renames the faction, so options 1 and 3 behave
  the same for factions.
- Faction names need a CAPIE faction name layer. The script is ready; with CAPIE 1.2 or older the
  faction editor changes nothing and says a newer CAPIE is needed. CAPIE 1.3 implements the layer
  (contract section 15); it has not run in the game yet.
