---
description: The snippet every mod built on CAPIE ships to show when CAPIE is missing, in a campaign and in the main menu.
---

# Indicators

Players need to see when a mod expects CAPIE and CAPIE is not there. A silent failure is never
acceptable. Every mod built on CAPIE therefore ships a small, discreet indicator, and the main
menu shows one line that says whether CAPIE is loaded.

## "This mod requires CAPIE, but it is missing" <span class="st st-progress">In the SDK, needs in-game test</span>

Inside a campaign the indicator sits where the feature that needs CAPIE would be. For Rename
Anything that is the country pencil on the Politics title line: without CAPIE the pencil is
hidden and a 16 px warning glyph in the game's orange takes its place. It has no text on the
panel, just a tooltip:

> Rename Anything needs CAPIE, which is not loaded. Custom country names show as the original.
> Start the game with the CAPIE launcher.

It is never a pop-up and never a blocking dialog.

The snippet uses [signal S1](detection.md#s1-localisation-keys): a text box whose
localisation CAPIE overrides at draw time. No trigger is involved, so it is correct the moment
the panel opens.

The SDK ships the snippet as `snippets/capie_v1_indicator.gui.txt`:

```paradox title="interface/my_mod.gui (inside your own window)"
instantTextboxType = {
	name = "mymod_capie_status"
	position = { x = 0 y = 0 }
	font = "hoi_16mbs"
	text = "MYMOD_CAPIE_LINE"
	maxWidth = 160
	maxHeight = 18
	fixedsize = yes
	pdx_tooltip = "MYMOD_CAPIE_TT"
}
```

```paradox-loc title="localisation/english/mymod_l_english.yml"
l_english:
 MYMOD_CAPIE_LINE:0 "$CAPIE_STATUS$"
 MYMOD_CAPIE_TT:0 "My Mod types names through CAPIE. Without it the editors stay read-only."
```

With CAPIE 1.1 loaded, `CAPIE_STATUS` reads `CAPIE 1.1` in the text colour; without it the
SDK's fallback `CAPIE missing` is drawn in orange. Two details still need a test in the game:
whether an empty or single-space result would draw cleanly (for designs that want nothing shown
while CAPIE is there), and whether a `£` text icon can carry the warning glyph.

## Main menu: "CAPIE loaded" or "CAPIE missing" <span class="st st-planned">Planned</span> { #main-menu }

One line in the bottom-left corner of the main menu, in `Arial12`:

| State | Line |
|---|---|
| CAPIE loaded | `CAPIE 1.1 loaded` (version reported by CAPIE) |
| A mod in the playset needs CAPIE, CAPIE not loaded | `CAPIE missing: a mod in your playset needs it` |
| No enabled mod uses CAPIE | nothing |

The main menu has no script context: no country, no scripted GUI, no triggers. Only GUI files
and localisation apply there. That has consequences:

- The line can only say loaded or missing. Without CAPIE nothing can count mods or compare
  versions in the menu, so version mismatches are shown in the campaign instead, where the API
  level is known.
- Adding a line means overriding the whole vanilla `interface/frontendmainview.gui`. To avoid
  conflicts, only one item ships that override: the CAPIE script SDK. Total conversions that
  already replace the menu paste the same small block into their own file.
- Each game patch that changes the vanilla menu file needs an SDK update.

<span class="proposed-tag">Proposed, subject to change</span>

```paradox title="Block added to mainmenu_panel_bottom (SDK override)"
# PROPOSED. Added to a copy of vanilla interface/frontendmainview.gui.
instantTextBoxType = {
	name = "capie_menu_status"
	position = { x = 12 y = -24 }
	orientation = LOWER_LEFT
	font = "Arial12"
	text = "CAPIE_MENU_STATUS"
	maxWidth = 400
	maxHeight = 14
}
```

```paradox-loc
l_english:
 CAPIE_MENU_STATUS: "§OCAPIE missing: a mod in your playset needs it§!"   # CAPIE would override with "CAPIE 1.1 loaded"
```

Open questions before this can ship: whether main-menu text goes through the lookups CAPIE
hooks (proven only for in-game panels), and whether the menu reads its text once before CAPIE is
loaded. If the menu caches it, the line would say "missing" until the menu is rebuilt. If the
snippet is absent or broken, CAPIE still works, and the in-campaign indicator and the launcher's
own message remain.
