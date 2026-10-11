---
description: How HOI4 mods will use CAPIE. Overview of the planned script contract, the reserved capie_ prefix and what already works.
---

# For modders

!!! proposed "Proposed, subject to change"

    The public CAPIE API does not exist yet. The mechanism underneath it runs Rename Anything
    today, in single player, under Rename Anything's own private names (`rn_*`). The `capie_*`
    names, file names and code on these pages are a proposal. They will change before the first
    release, and nothing here should go into a published mod yet.

CAPIE is meant to become a library any HOI4 mod can build on, so that modders get these
abilities without shipping native code themselves. The first goal is to open up what Rename
Anything already does: free text names for countries, leaders and parties, text typed into a
native field, and a reliable way to know whether CAPIE is loaded and which version it is.

## The decided shape

- The contract is variant B: plain HOI4 script, talking to CAPIE through variables, scripted
  effects and the cosmetic tag. A mod that uses it parses and runs the same way with or without
  CAPIE. Only the branch it takes differs. See [Script contract](contract.md).
- Native script keywords (variant A, for example a `capie_set_country_name` effect the game's
  parser would understand) may come later as an extra layer over variant B. They need more
  reverse engineering and would produce parser errors whenever CAPIE is missing.
- The script SDK ships as its own Steam Workshop item with its own repository, so every mod uses
  one copy.
- Every mod built on CAPIE shows an indicator when CAPIE is missing, and the main menu shows
  "CAPIE loaded" or "CAPIE missing". See [Indicators](indicators.md).
- Mods pin a minimum API level and degrade gracefully when it is not there. See
  [Versions and fallback](versioning.md).

## The `capie_` prefix is reserved

Everything that starts with `capie_` (or `CAPIE_` in localisation) belongs to CAPIE: scripted
effects and triggers, variables, temporary variables, flags, localisation keys and GUI element
names. Do not define anything with that prefix in your own mod. CAPIE removes script-set copies
of its reserved command records before it adds its own, so a value you set yourself would be
dropped or ignored.

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
| [Script contract](contract.md) | Variant B: inputs, outputs, an Apply button, the hard rule about where CAPIE data exists |
| [Detecting CAPIE](detection.md) | The three detection signals and which one to use for what |
| [Indicators](indicators.md) | The "mod requires CAPIE but it is missing" snippet and the main-menu line |
| [Versions and fallback](versioning.md) | API levels, release versions, deprecation, graceful degradation |
