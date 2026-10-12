---
description: The three signals a mod can use to tell whether CAPIE is loaded, how fresh each one is, and what each is for.
---

# Detecting CAPIE

!!! proposed "Built, not yet tested in the game"

    The three signals below exist in CAPIE 1.1 and the script SDK. They generalise mechanisms
    that work in Rename Anything today (`RN_PLUGIN_STATUS`, `rn_native_cap`), but the `CAPIE_*`
    keys and `capie_*` records have not been seen in the game yet.

A mod asks "is CAPIE here?" for three different reasons: to draw an indicator, to decide whether
an effect may run, and to choose which editor to show. One signal cannot answer all three,
because HOI4 keeps command data, saved data and drawn text apart. Mixing the signals up is the
most likely source of bugs.

| Signal | Always fresh? | Where you can read it | Use it for |
|---|---|---|---|
| S1. Localisation keys `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD` | Yes, evaluated whenever the text is drawn | Any text: GUI, tooltips, possibly the main menu | Indicators |
| S2. Command record `capie_cap` | Yes, per command; a save cannot fake it | Only inside the effect of a command CAPIE annotated | The only hard check before an effect that needs CAPIE |
| S3. Flag `capie_v1_seen` and variable `capie_v1_ver` on the country | No: stale after loading a save without CAPIE, until the next command | `visible` and `enabled` triggers, AI, other mods | Cosmetic choices only, such as which editor to show |

## S1: localisation keys

`capie_core.dll` answers three keys whenever the game draws them. The SDK's localisation holds
the fallbacks, which players see when CAPIE is missing:

| Key | With CAPIE 1.1 | Without CAPIE |
|---|---|---|
| `CAPIE_STATUS` | `CAPIE 1.1` | `CAPIE missing` (orange) |
| `CAPIE_API` | `2` | `0` |
| `CAPIE_BUILD` | `1.1.0` | `-` |

Nest them in your own keys:

```paradox-loc title="localisation/english/my_mod_l_english.yml"
l_english:
 MY_MOD_CAPIE_LINE:0 "$CAPIE_STATUS$"
```

The mechanism works today: Rename Anything's `RN_PLUGIN_STATUS` key reads "CAPIE 1.0 loaded"
with CAPIE and the mod's fallback without it, and nesting through `$...$` works. Because the
text is resolved when it is drawn, S1 is right immediately after loading a save without CAPIE.

S1 is text. A trigger cannot read it, so it cannot decide what a script does.

## S2: the command record

When CAPIE is loaded, it adds `capie_cap` (the API level) to the Apply command of every grammar
editor and removes any copy that came from elsewhere. `capie_v1_begin` reads it for you:

```paradox
set_temp_variable = { capie_open_epoch = my_mod_epoch }
capie_v1_begin = yes
# capie_err = 10: CAPIE missing; 11: older than capie_min_api; 0: go on
```

Rename Anything uses the same pattern with its private record `rn_native_cap` and error code
`rn_pair_err = 10`.

S2 tells you CAPIE is present on the machine that sent the command. It is not permission to act;
check [authority](../permissions.md) separately.

## S3: the hint flag

GUI triggers run outside any command, so they cannot see S2. `capie_v1_begin` therefore notes
the result on the acting country, through `capie_v1_note_presence`:

```paradox title="capie_sdk_v1_effects.txt (generated)"
capie_v1_note_presence = {
	if = {
		limit = { check_variable = { temp_var:capie_cap > 0 } }
		ROOT = {
			set_variable = { capie_v1_ver = temp_var:capie_ver }
			set_country_flag = capie_v1_seen
		}
	}
	else = {
		ROOT = { clear_variable = capie_v1_ver clr_country_flag = capie_v1_seen }
	}
	capie_v1_mp_recompute = yes
}
```

The flag is saved with the game, so it can be wrong right after loading a save on a machine
without CAPIE. That is acceptable only because a wrong hint can at worst show an editor whose
Apply then fails cleanly with "CAPIE is not loaded. Nothing was changed." It must never lead to a
wrong rename. The same values feed the [multiplayer check](../multiplayer.md#version-check).

## Recipe

1. Every Apply effect starts with `capie_v1_begin`.
2. GUI `visible` triggers use S3 only to choose a layout.
3. Every effect that needs CAPIE stops when `capie_err` is not 0.
4. Indicators use S1.

Whether `on_startup` also fires when a save is loaded is not known. The recipe does not rely on
it.
