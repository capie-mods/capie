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
| Host, through a game rule | A mod game rule set in the lobby, read with `has_game_rule` | <span class="st st-planned">Planned</span> |
| Overlord over a subject | Target scope `is_subject_of = ROOT` | <span class="st st-planned">Planned</span> |

## Your own country

The pencils and editors appear only for objects of your own country. That is a convenience, not a
guard: when a player clicks, the game runs the button's effect without evaluating its `visible`
and `enabled` triggers again. Every effect therefore checks authority again inside its own body.
Rename Anything does this today, and the CAPIE SDK would provide the same check as a scripted
trigger (proposed name `capie_v1_require_own_country`).

In multiplayer the acting country inside the effect is expected to be the country of the player
who issued the command. That is a design assumption until the two-machine test.

## The host <span class="st st-planned">Planned</span>

HOI4 has no script trigger that says "this is the host", and reading the game's code found no
such flag either: a command carries the country it acts for, not a verified player or machine.
The documented triggers mention a host only for governments in exile. So the host's exception
works through the one thing only the host controls, the game rules chosen in the lobby. A mod
game rule decides what players may rename beyond their own country, for example:

| Option | Meaning |
|---|---|
| Own country only | Players rename only their own country's things |
| Overlords too | Overlords may also rename their subjects' things, including a player subject |

Setting the rule in the lobby is the host's consent, and the script reads it with
has_game_rule. There is no option that gives the host's own country extra powers during the
game, because the script cannot tell which country belongs to the host. CAPIE is not anti-cheat:
a modified client can send arbitrary commands in vanilla multiplayer too.

<span class="proposed-tag">Proposed, subject to change</span>

```paradox title="common/game_rules/capie_sdk_v1_rules.txt"
# PROPOSED. Names and options will change.
capie_v1_rename_others = {
	name = "CAPIE_RULE_RENAME_OTHERS"
	group = "RULE_GROUP_GENERAL"
	default = { name = OWN_ONLY text = "CAPIE_RULE_OWN_ONLY" }
	option = { name = OVERLORDS text = "CAPIE_RULE_OVERLORDS" }
}
```

## The overlord of a player subject <span class="st st-planned">Planned</span>

An overlord may rename the things of a subject, including a subject that another player controls.
The decision is the simplest variant: no veto for the subject and no extra screens. The check is
the documented trigger `is_subject_of = ROOT` on the target country.

<span class="proposed-tag">Proposed, subject to change</span>

```paradox
# PROPOSED. Inside the effect, with the target country in scope.
if = {
	limit = {
		OR = {
			tag = ROOT                      # own country
			is_subject_of = ROOT            # overlord over this subject, AI or player
		}
	}
	# allowed
}
else = {
	set_temp_variable = { capie_err = 20 }  # not your object; nothing changed (code not yet assigned)
}
```

In Rename Anything the slot for renaming a subject's names exists but is hidden while it is in
progress.
