---
description: How HOI4 mods use CAPIE. Overview of the script contract (API level 4), the SDK, the name layers, the reserved capie_ prefix and what still needs a test in the game.
---

# For modders

!!! proposed "Built, not released"

    CAPIE 1.1 implements the public editor contract (API level 2) and CAPIE 1.2 adds the
    country name layer (API level 4). The SDK's demo mod ran in the game on CAPIE 1.1, and the
    country name layer passed its first in-game checks on a CAPIE 1.2 developer build.
    Nothing is released, so do not publish a mod that depends on CAPIE yet.

CAPIE is a library any HOI4 mod can build on, so that modders get these abilities without
shipping native code: text the player types into a real text field of your own window, names
for countries, states, characters and parties built from that text, and a reliable way to know
whether CAPIE is loaded and which version it is.

## The decided shape

- The contract is variant B: plain HOI4 script, talking to CAPIE through variables and
  scripted effects. A mod that uses it parses and runs the same way with or without
  CAPIE. Only the branch it takes differs. See [Script contract](contract.md).
- Native script keywords (variant A, for example a `capie_set_country_name` effect the game's
  parser would understand) may come later as an extra layer over variant B. They need more
  reverse engineering and would produce parser errors whenever CAPIE is missing.
- The script SDK ships as its own Steam Workshop item with its own repository, so every mod uses
  one copy. Its source is `sdk/capie-script-sdk/` in this repository, with a demo mod.
- CAPIE is one option among native extenders. [Alternatives](../alternatives.md) compares it
  with `hoi4_lua` and proxy-DLL loaders.
- Every mod built on CAPIE shows an indicator when CAPIE is missing, and the main menu shows
  "CAPIE loaded" or "CAPIE missing". See [Indicators](indicators.md).
- Mods pin a minimum API level and degrade gracefully when it is not there. See
  [Versions and fallback](versioning.md).

## The `capie_` prefix is reserved

Everything that starts with `capie_` (or `CAPIE_` in localisation) belongs to CAPIE: scripted
effects and triggers, variables, temporary variables, flags, localisation keys and GUI element
names. Do not define anything with that prefix in your own mod. CAPIE removes script-set copies
of its reserved command records before it adds its own, so a value you set yourself would be
dropped or ignored. The one exception is your editor's GUI names, which follow the
[grammar](contract.md#editor-names) `capie_v1_ed__<mod>__<editor>` so that CAPIE can find them.

The code blocks on these pages colour `capie_` names differently so that you can see at a glance
which names belong to CAPIE:

```paradox
my_mod_effect = {
	set_temp_variable = { capie_min_api = 1 }   # capie_ name: reserved for CAPIE
	set_temp_variable = { my_mod_counter = 1 }  # your own name
}
```

## When you do not need CAPIE

Names you know while writing the mod need nothing from CAPIE. Vanilla script already handles
them:

```paradox
# Vanilla. Works without CAPIE.
set_cosmetic_tag = MYMOD_FREE_POLAND        # with MYMOD_FREE_POLAND, _DEF and _ADJ loc keys
set_party_name = { ideology = neutrality long_name = MYMOD_PARTY_LONG name = MYMOD_PARTY }
```

CAPIE is for text that exists only at run time: what the player typed, or a name your mod builds
from parts while the game runs.

## Pages in this section

| Page | What it covers |
|---|---|
| [Script contract](contract.md) | Editor names, the records CAPIE adds, sentinels, alphabet, the hard rule about where CAPIE data exists |
| [Building an editor](editors.md) | A complete one-field editor from the SDK's demo mod |
| [Text tools](text-tools.md) | Work buffer, trim, validation, text slots, setters, error codes |
| [Name and flag layers](name-layers.md) | Custom country names (API level 4), faction names (API level 5) and country flags (API level 6), separate from cosmetic tags |
| [Detecting CAPIE](detection.md) | The three detection signals and which one to use for what |
| [Indicators](indicators.md) | The "mod requires CAPIE but it is missing" snippet and the main-menu line |
| [Versions and fallback](versioning.md) | API levels, release versions, deprecation, graceful degradation |
