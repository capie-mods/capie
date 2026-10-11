---
description: The proposed CAPIE script contract (variant B). Inputs and outputs as variables, an Apply button example, and where CAPIE data exists.
---

# Script contract (variant B)

!!! proposed "Proposed, subject to change"

    Every `capie_*` name on this page is a proposal. The mechanism works today inside Rename
    Anything under private names (`rn_native_cap`, `rn_pair_err`); the generic SDK is not
    written yet.

## How it works

Variant B uses nothing the game's parser does not already understand. A mod sets temporary
variables, calls scripted effects from the CAPIE script SDK, and reads results back from
temporary variables. CAPIE's native side adds data to the player's own game command; the SDK's
scripts read that data, check it, and call the game's own setters.

<figure class="diagram" markdown="span">
  ![The player clicks Apply. CAPIE adds capie_cap and the typed text to the player's own command. The effect runs on every client with the same input. The SDK checks capie_cap and calls the game's setter. Without CAPIE the effect sets capie_err to 10 and changes nothing.](../assets/shared/diagrams/variant-b-flow.svg)
  <figcaption>The same script runs with or without CAPIE. Only the branch differs.</figcaption>
</figure>

Why this shape:

- A mod that uses variant B loads without errors when CAPIE is missing, after a game patch, or on
  a machine that never had CAPIE.
- In multiplayer, the branch input (`capie_cap`) travels inside the command, so every client
  should take the same branch. That is a design goal; it has not been tested with two players.
- The contract does not depend on the game build. After a patch, mods need no update while CAPIE
  catches up; they show "CAPIE missing" in the meantime.

## SDK files

The SDK is plain script with the major version in every path and name, so two mods pinned to
different major versions can coexist:

```text
common/scripted_triggers/capie_sdk_v1_triggers.txt
common/scripted_effects/capie_sdk_v1_effects.txt
localisation/english/capie_sdk_v1_l_english.yml
```

The SDK ships as its own Steam Workshop item; your mod lists it as a dependency. Do not edit SDK
files. A changed copy is a bug, not a customisation point.

## Inputs and outputs

Vanilla scripted effects take no parameters, so the SDK works with temporary variables:

| Direction | Name | Meaning |
|---|---|---|
| In | `capie_min_api` | Lowest API level your effect needs |
| In (from CAPIE) | `capie_cap` | API level CAPIE wrote into this command; absent without CAPIE |
| Out | `capie_err` | `0` success, `10` CAPIE missing, other values name the problem |
| Out | `capie_err_field` | Which field failed validation, for multi-field editors |

## Example: an Apply button

<span class="proposed-tag">Proposed, subject to change</span>

```paradox title="common/scripted_guis/my_mod_rename.txt"
# PROPOSED SYNTAX. Nothing below exists yet.
my_mod_rename_window = {
	context_type = player_context
	window_name = "capie_v1_ed__mymod__country"   # name grammar, see below
	effects = {
		capie_v1_ed__mymod__country_apply_click = {
			set_temp_variable = { capie_min_api = 1 }
			if = {
				limit = { capie_v1_cmd_api_at_least = yes }
				# Check authority again here: the effect does not repeat visible/enabled triggers.
				capie_v1_require_own_country = yes            # sets capie_err if ROOT is not the actor
				if = {
					limit = { check_variable = { temp_var:capie_err = 0 } }
					capie_v1_set_country_name_from_fields = yes   # decode, validate, set cosmetic tag
				}
			}
			else = {
				set_temp_variable = { capie_err = 10 }       # CAPIE missing: show the status line
			}
		}
	}
}
```

The trigger the example calls would be a one-liner in the SDK:

<span class="proposed-tag">Proposed, subject to change</span>

```paradox title="common/scripted_triggers/capie_sdk_v1_triggers.txt"
# PROPOSED SYNTAX.
# True when the current command carries capie_cap >= temp_var:capie_min_api.
capie_v1_cmd_api_at_least = {
	check_variable = { temp_var:capie_cap >= temp_var:capie_min_api }
}
```

`capie_v1_set_country_name_from_fields` would wrap what Rename Anything does today: decode the
transported character codes, validate them, and build the cosmetic tag with `meta_effect`. The
body Rename Anything runs now looks like this (private names, works today):

```paradox title="What the SDK would wrap (Rename Anything today, shortened)"
rn_apply_country_name = {
	if = {
		limit = {
			rn_country_rename_enabled = yes
			check_variable = { rn_cname_len > 0 }
			check_variable = { rn_cadj_len > 0 }
		}
		meta_effect = {
			text = { set_cosmetic_tag = "RN1|[N]|[J]|" }
			N = "[ROOT.RN_N_0][ROOT.RN_N_1] ... [ROOT.RN_N_31]"   # one scripted loc per character
			J = "[ROOT.RN_J_0][ROOT.RN_J_1] ... [ROOT.RN_J_31]"
		}
		set_country_flag = rn_custom_name
	}
}
```

Leader, party, state, city and character variants would wrap the vanilla setters the same way.

## The hard rule: where CAPIE data exists

!!! warning "Command data exists only inside the command"

    `capie_cap` and the transported text are temporary records of one game command. They exist
    only while that command's effect runs. In `visible` and `enabled` triggers, in on-actions
    and in any other effect they are absent, so a `capie_v1_cmd_*` trigger there is always false.

    Use [the other detection signals](detection.md) to decide what to show. Use `capie_cap`
    to decide what to do.

## Rules every effect must follow

1. Check authority inside the effect. When the player clicks, the game runs the effect without
   repeating the button's `visible` or `enabled` triggers. See [Permissions](../permissions.md).
2. Gate on `capie_cap` inside the effect, never on a flag or a variable stored in the save.
3. On failure, set `capie_err`, show a status line, and change nothing. Never partially apply
   a multi-field edit.
4. Never use `career_profile_has_player_flag` or any other local-player state inside an effect.
   Branching on local state in a synchronised effect risks an out-of-sync game.

## Text limits

The limits come from the transport that works in Rename Anything today:

- At most 32 characters per field. Longer input is rejected, never cut.
- Letters `a-z`, `A-Z`, digits, space, `-`, `.`, `'`, and the 18 Polish letters. Other
  characters are rejected with an error. Wider alphabets (Cyrillic, accents, umlauts) are planned,
  as far as the game's fonts, parser and transport handle them; that still needs research.
- The country name path also rejects `| " \ [ ] $ { } #` and `@`, control characters, and invalid
  UTF-8.

## Registering your own editor

Today CAPIE recognises only Rename Anything's own window and button names. For other mods it
would recognise a strict name grammar instead of reading registration files:

<span class="proposed-tag">Proposed, subject to change</span>

```text
window (model):   capie_v1_ed__<modid>__<editor>
field (editBox):  capie_v1_f__<modid>__<editor>__<n>      n = 1..4
Open selector:    capie_v1_ed__<modid>__<editor>_open_click
Apply selector:   capie_v1_ed__<modid>__<editor>_apply_click
modid, editor:    [a-z0-9]{1,12}
```

CAPIE would parse these names exactly, not by substring search, and cap the number of editors
per mod and per session.

## Highlighting in these docs

HOI4 script has no stock lexer in Pygments, the highlighter Zensical uses. The code on this site
is coloured by a small local lexer (`pygments-paradox-script` in the repository's
`sites/shared/`) that knows Clausewitz blocks, operators, dates, `yes`/`no`, scope prefixes such
as `temp_var:` and the reserved `capie_` prefix. Use the fence names `paradox` for script and
`paradox-loc` for localisation if you copy the setup.
