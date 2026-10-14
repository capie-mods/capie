# ADR-0013: Who may rename what, and where it is checked

- Status: Accepted. The host exception was corrected on 2026-10-11 after reverse engineering;
  [ADR-0029](0029-game-rules-event-and-restore.md) turns the rule into three lobby options.
- Date: 2026-10-11
- Source: [API decision log](../evidence/decision-log/API-DECISIONS.md), first list item 6;
  "Decyzje do architektury CAPIE" items 6 and 7; "Korekty po analizie RE", second and third bullets

## Context

In a shared campaign one player renaming another player's country would be a griefing tool. Some
exceptions are natural: the host of a game, and an overlord whose subject is another player.

## Decision

- A player renames only their own country's things. Exceptions: the host of the game, and an
  overlord whose subject is another player.
- The overlord exception uses the simplest form: no veto for the subject and no extra UI.
- The game has no trigger for "this player is the host". The host exception is therefore
  implemented only through a game rule that the host sets in the lobby.
- Permissions are checked inside the script effect itself. The game's command `Execute` does not
  verify the sender, and it does not re-run the button's `visible` or `enabled` triggers.

## Consequences

- CAPIE is not a trusted source of who clicked. Every effect checks authority, and a failed check
  changes nothing (error 20 in [CONTRACT.md](../../plugin/capie/CONTRACT.md), section 9).
- The SDK has its own rule `capie_v1_edit_rights`. Rename Anything defines its own rule
  `rn_edit_rights` and does not read the SDK's ([ADR-0027](0027-subjects-without-sdk-dependency.md)).
