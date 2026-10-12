---
description: Build a CAPIE text editor in your own mod. Window, fields, carriers, the Apply effect, permissions and the status line, taken from the SDK's demo mod.
---

# Building an editor

!!! proposed "Built, not yet tested in the game"

    The code below is the SDK's demo mod (`sdk/capie-script-sdk/examples/capie_demo`). It passes
    the SDK's offline checks; the game has not parsed it yet.

This page builds a one-field editor for a national motto. The demo mod also contains a two-field
editor for the country name and adjective. The mod id is `capiedemo`, the editor id `motto`.

## 1. Depend on the SDK

```text title="descriptor.mod"
dependencies={
	"CAPIE Script SDK"
}
```

The SDK ships the `capie_v1_*` effects and triggers, the `CAPIE_*` localisation fallbacks, the
lobby rule and the multiplayer check window. Do not copy its files into your mod.

## 2. The window

The names follow the [grammar](contract.md#editor-names). Hide the carriers; CAPIE finds them by
name, and the player never needs to see them.

```paradox title="interface/mydemo.gui (shortened)"
containerWindowType = {
	name = "capie_v1_ed__capiedemo__motto"
	position = { x = -210 y = -90 }
	orientation = center
	size = { width = 420 height = 150 }
	background = { name = "Background" quadTextureSprite = "GFX_tiled_window_2b_border" }

	# Prefill of field 1: what the field shows when the editor opens.
	instantTextboxType = { name = "capie_v1_pf__capiedemo__motto__1" text = "CAPIE_V1_TXT3" font = "hoi_16mbs" maxWidth = 300 maxHeight = 16 }
	# The opening epoch your mod stores.
	instantTextboxType = { name = "capie_v1_ep__capiedemo__motto" text = "[?ROOT.mydemo_motto_epoch|0]" font = "hoi_16mbs" maxWidth = 60 maxHeight = 16 }

	editBoxType = {
		name = "capie_v1_f__capiedemo__motto__1"
		position = { x = 16 y = 65 }
		size = { x = 356 y = 20 }
		font = "hoi_18"
		text = ""
		ignore_enter = yes          # Enter goes to the Apply button's shortcut
	}
	buttonType = {
		name = "capie_v1_ed__capiedemo__motto_apply"
		position = { x = 382 y = 62 }
		quadTextureSprite = "GFX_preset_button_save"
		shortcut = "RETURN"
	}
	instantTextboxType = { name = "mydemo_motto_error" text = "[ROOT.CAPIE_V1_LAST_ERROR]" font = "hoi_16mbs" maxWidth = 388 maxHeight = 36 }
}
```

A 356-pixel field in `hoi_18` holds 32 of the widest characters. Keep the field at least that
wide, or players cannot see the end of a long name.

## 3. The scripted GUI

```paradox title="common/scripted_guis/mydemo.txt"
scripted_gui = {
	capie_v1_ed__capiedemo__motto = {
		context_type = player_context
		window_name = "capie_v1_ed__capiedemo__motto"
		visible = { is_ai = no has_country_flag = mydemo_motto_open }
		ai_enabled = { always = no }
		effects = {
			capie_v1_ed__capiedemo__motto_apply_click = { mydemo_apply_motto = yes }
			mydemo_motto_close_click = { mydemo_close_motto = yes }
		}
		triggers = {
			capie_v1_pf__capiedemo__motto__1_visible = { always = no }
			capie_v1_ep__capiedemo__motto_visible = { always = no }
		}
	}
}
```

Whatever opens the editor (a decision in the demo) raises the epoch and sets the flag:

```paradox
complete_effect = {
	add_to_variable = { mydemo_motto_epoch = 1 }
	set_country_flag = mydemo_motto_open
}
```

## 4. The Apply effect

Each SDK step after `capie_v1_begin` does nothing while `capie_err` is not 0, so the effect is a
straight list.

```paradox title="common/scripted_effects/mydemo_effects.txt"
mydemo_apply_motto = {
	set_temp_variable = { capie_open_epoch = mydemo_motto_epoch }
	capie_v1_begin = yes                         # header, CAPIE presence, epoch: capie_err 10..16
	if = {
		limit = { capie_v1_ok = yes NOT = { capie_v1_actor_ok = yes } }
		set_temp_variable = { capie_err = 20 }   # permission is checked here, in the effect
	}
	set_temp_variable = { capie_field = 1 }
	capie_v1_field_load = yes                    # field 1 into the work buffer: 21..26, 35
	capie_v1_text_trim = yes
	set_temp_variable = { capie_min_len = 2 }
	set_temp_variable = { capie_max_len = 32 }
	capie_v1_text_validate = yes                 # 31..34
	set_temp_variable = { capie_slot = 3 }
	capie_v1_text_store = yes                    # keep it in text slot 3 of ROOT
	capie_v1_report = yes                        # for [ROOT.CAPIE_V1_LAST_ERROR]
	if = {
		limit = { capie_v1_ok = yes }
		mydemo_close_motto = yes
	}
}

mydemo_close_motto = {
	clr_country_flag = mydemo_motto_open
	add_to_variable = { mydemo_motto_epoch = 1 }   # the next Apply of this opening is stale
}
```

To rename something instead of storing text, put the fields in slots 1 and 2 and call a setter in
the scope of the object, for example `capie_v1_set_country_name = yes` for the country (name in
slot 1, adjective in slot 2). See [Text tools](text-tools.md).

## 5. Localisation

Show the CAPIE status in your window with your own key that nests the SDK's:

```paradox-loc title="localisation/english/mydemo_l_english.yml"
l_english:
 MYDEMO_STATUS:0 "$CAPIE_STATUS$"
 MYDEMO_MOTTO_CURRENT:0 "Current motto: $CAPIE_V1_TXT3$"
```

## What the player sees

| Situation | Result |
|---|---|
| CAPIE loaded, valid text | The motto is stored; the window closes |
| Text too short, double space, unsupported character | The window stays open with a message naming the field and position |
| CAPIE not loaded | "CAPIE is not loaded. Nothing was changed." The field still takes typing, because it is the game's own widget |
| Apply twice in one opening | The second one reports a stale editor and changes nothing |
| Multiplayer, a player without the same CAPIE build | "Not every player runs the same CAPIE." until every player presses CAPIE check |

## What is not there yet

CAPIE prefills the fields once per opening, never over a field that has keyboard focus. It does
not focus the first field on open, select its text, stop typing at 32 characters, handle Escape
or release focus when your window hides. Those need evidence from the game first.

<span class="st st-progress">needs in-game test</span> for the whole page: the demo has not run
in the game.
