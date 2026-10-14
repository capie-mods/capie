# ADR-0036: Leader portrait choice and a host switch for every feature

- Status: Accepted. Not implemented.
- Date: 2026-10-14

## Context

Players can rename a leader but not change the leader's picture. The maintainer also wants the
host to be able to switch off any single feature of the mod for a game.

## Decision

- **Leader portrait.** A player can pick a different portrait for a country leader. The pool is a
  game rule:
  - any country leader from any country;
  - only leaders of the player's own country;
  - own country's leaders plus generals;
  - every leader and every general.
- **Portraits of subjects** are a separate game rule:
  - default: the overlord and the subject can both change the subject leader's portrait;
  - only the overlord can;
  - the overlord cannot.
- **Feature switches.** Every feature gets an on/off game rule (country name, leader name, party
  names, flag, portrait, states and cities, other characters, factions, subjects, and any later
  feature). A disabled feature keeps its button visible but greyed out, with a tooltip saying the
  host turned the option off for this game.
- The portrait choice follows the same principles as the name and flag layers: separate from the
  game's own data, reversible, deterministic in multiplayer, and fail-closed when CAPIE cannot
  apply it.

## Consequences

- The game has no script effect for an arbitrary portrait, so the portrait most likely needs a
  CAPIE layer like the flag; the design is reviewed before implementation.
- More game rules make the rules screen longer; they stay grouped and prefixed
  "Rename Anything:".

## Amendment 2026-10-14

- Pool rule "own country plus generals" means the own country's generals (and admirals).
- A chosen portrait replaces only the country-leader portrait; army and navy portraits of the same
  character stay as the game shows them.
- The game does have script effects for portraits (`set_portraits`, `set_country_leader_portrait`,
  `set_leader_portrait`); the native-layer versus script-only choice is decided after the
  independent review of the design.
