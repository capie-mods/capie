---
description: Who may rename what. Your own country, the host through a lobby game rule, and an overlord over a player subject.
---

# Permissions

The rule is short: a player renames only their own country's things. There are two exceptions:
the host, through a game rule set in the lobby, and an overlord whose subject is another player. This page explains how each rule is
checked and how far each one is built.

| Rule | How it is checked | State |
|---|---|---|
| Own country | Scripted GUI with `context_type = player_context`; the effect checks again that the target belongs to the acting country and that `is_ai = no` | <span class="st st-today">Works today</span> in single player |
| Check again inside the effect | Required, because the game runs the effect without repeating `visible`/`enabled` triggers | <span class="st st-today">Works today</span> |
| Host, through a game rule | SDK game rule `capie_v1_edit_rights`, set in the lobby, read with `has_game_rule` | <span class="st st-progress">In the SDK, needs in-game test</span> |
| Overlord over a subject | SDK trigger `capie_v1_may_edit_country`: target scope `is_subject_of = ROOT` | <span class="st st-progress">In the SDK, needs in-game test</span> |

## Your own country

The pencils and editors appear only for objects of your own country. That is a convenience, not a
guard: when a player clicks, the game runs the button's effect without evaluating its `visible`
and `enabled` triggers again. Every effect therefore checks authority again inside its own body.
Rename Anything does this today. The CAPIE SDK provides two scripted triggers for it:
`capie_v1_actor_ok` (the acting country, ROOT, is played by a human) and
`capie_v1_may_edit_country` (in the scope of the target country, see below).

In multiplayer the acting country inside the effect is expected to be the country of the player
who issued the command. That is a design assumption until the two-machine test.

## The host <span class="st st-progress">In the SDK</span>

HOI4 has no script trigger that says "this is the host", and reading the game's code found no
such flag either: a command carries the country it acts for, not a verified player or machine.
The documented triggers mention a host only for governments in exile. So the host's exception
works through the one thing only the host controls, the game rules chosen in the lobby. A mod
game rule decides what players may rename beyond their own country and its subjects:

| Option | Meaning |
|---|---|
| Own country and subjects (default) | Players rename their own country's things and those of their subjects |
| Any country | Every human player may rename any country's things |

Setting the rule in the lobby is the host's consent, and the script reads it with
has_game_rule. There is no option that gives the host's own country extra powers during the
game, because the script cannot tell which country belongs to the host. CAPIE is not anti-cheat:
a modified client can send arbitrary commands in vanilla multiplayer too.

```paradox title="common/game_rules/capie_sdk_v1_rules.txt (SDK)"
capie_v1_edit_rights = {
	name = "CAPIE_V1_RULE_EDIT_RIGHTS"
	group = "RULE_GROUP_GAMEPLAY"
	default = { name = "CAPIE_V1_OWN" text = "CAPIE_V1_RULE_OWN" desc = "CAPIE_V1_RULE_OWN_DESC" }
	option = { name = "CAPIE_V1_OPEN" text = "CAPIE_V1_RULE_OPEN" desc = "CAPIE_V1_RULE_OPEN_DESC" allow_achievements = yes }
}
```

The rule applies to every mod built on the SDK, so players set it once per game.

## The overlord of a player subject <span class="st st-progress">In the SDK</span>

An overlord may rename the things of a subject, including a subject that another player controls.
The decision is the simplest variant: no veto for the subject and no extra screens. The check is
the documented trigger `is_subject_of = ROOT` on the target country.

```paradox title="capie_sdk_v1_triggers.txt (generated)"
# In the scope of the target country; ROOT is the acting country.
capie_v1_may_edit_country = {
	OR = {
		tag = ROOT                      # own country
		is_subject_of = ROOT            # overlord over this subject, AI or player
		has_game_rule = { rule = capie_v1_edit_rights option = CAPIE_V1_OPEN }
	}
}
```

Inside your Apply effect, a failed check sets `capie_err = 20` ("You may not change this.") and
changes nothing:

```paradox
if = {
	limit = {
		capie_v1_ok = yes
		NOT = { AND = { capie_v1_actor_ok = yes capie_v1_may_edit_country = yes } }
	}
	set_temp_variable = { capie_err = 20 }
}
```

In Rename Anything the slot for renaming a subject's names exists but is hidden while it is in
progress.
