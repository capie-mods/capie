---
description: The three signals a mod can use to tell whether CAPIE is loaded, how fresh each one is, and what each is for.
---

# Detecting CAPIE

!!! proposed "Proposed, subject to change"

    The signals below generalise mechanisms that work in Rename Anything today
    (`RN_PLUGIN_STATUS`, `rn_native_cap`). The `CAPIE_*` and `capie_*` names are proposals.

A mod asks "is CAPIE here?" for three different reasons: to draw an indicator, to decide whether
an effect may run, and to choose which editor to show. One signal cannot answer all three,
because HOI4 keeps command data, saved data and drawn text apart. Mixing the signals up is the
most likely source of bugs.

| Signal | Always fresh? | Where you can read it | Use it for |
|---|---|---|---|
| S1. Status localisation key `CAPIE_STATUS` | Yes, evaluated whenever the text is drawn | Any text: GUI, tooltips, possibly the main menu | Indicators |
| S2. Command record `capie_cap` | Yes, per command; a save cannot fake it | Only inside the effect of a command CAPIE annotated | The only hard check before an effect that needs CAPIE |
| S3. Hint flag `capie_v1_seen` on the country | No: stale after loading a save without CAPIE, until the next command | `visible` and `enabled` triggers, AI, other mods | Cosmetic choices only, such as which editor to show |

## S1: status localisation key

CAPIE overrides a few reserved localisation keys when the game draws them. Your localisation
file holds the fallback text, which is what players see when CAPIE is missing:

<span class="proposed-tag">Proposed, subject to change</span>

```paradox-loc title="localisation/english/capie_sdk_v1_l_english.yml (UTF-8 with BOM)"
l_english:
 CAPIE_STATUS: "§OCAPIE missing§!"            # fallback; CAPIE replaces it when loaded
 CAPIE_STATUS_TT: "Rename Anything needs CAPIE, which is not loaded. Start the game with the CAPIE launcher."
 MYMOD_CAPIE_LINE: "$CAPIE_STATUS$"           # nest it in your own keys
```

The mechanism works today: Rename Anything's `RN_PLUGIN_STATUS` key reads
`DLL: INACTIVE (RN1)` from the mod and `DLL: ACTIVE (RN1)` once CAPIE overrides it, and nesting
through `$...$` works. Because the text is resolved when it is drawn, S1 is correct right after
loading a save without CAPIE.

S1 is text. A trigger cannot read it, so it cannot decide what a script does.

## S2: the command record

When CAPIE is loaded, it adds `capie_cap` (the API level) to every command it annotates and
strips any copy a script set before. Inside the effect of that command:

<span class="proposed-tag">Proposed, subject to change</span>

```paradox
# PROPOSED. Valid only inside the effect of an annotated command.
if = {
	limit = { check_variable = { temp_var:capie_cap > 0 } }
	# CAPIE is loaded on the machine that sent this command
}
else = {
	set_temp_variable = { capie_err = 10 }   # CAPIE missing; change nothing
}
```

Rename Anything uses this today with the private record `rn_native_cap` and error code
`rn_pair_err = 10`.

S2 tells you CAPIE is present. It is not permission to act; check
[authority](../permissions.md) separately.

## S3: the hint flag

GUI triggers run outside any command, so they cannot see S2. For layout decisions the SDK keeps a
hint on the country:

<span class="proposed-tag">Proposed, subject to change</span>

```paradox
# PROPOSED. Every annotated command runs this first.
capie_v1_note_presence = {
	if = {
		limit = { check_variable = { temp_var:capie_cap > 0 } }
		set_country_flag = capie_v1_seen
		set_variable = { capie_v1_api = temp_var:capie_cap }
	}
	else = {
		clr_country_flag = capie_v1_seen
	}
}
```

The flag is saved with the game, so it can be wrong right after loading a save on a machine
without CAPIE. That is acceptable only because a wrong hint can at worst show an editor whose
Apply then fails cleanly with "CAPIE missing. Nothing was changed." It must never lead to a wrong
rename.

## Recipe

1. Every annotated command (Open and Apply of your editor) runs `capie_v1_note_presence` first.
2. GUI `visible` triggers use S3 only to choose a layout.
3. Every effect that needs CAPIE checks S2 and reports `capie_err = 10` otherwise.
4. Indicators use S1.

Whether `on_startup` also fires when a save is loaded is not known. The recipe does not rely on
it.
