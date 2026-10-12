---
description: CAPIE API levels, release versions, build indices, deprecation, and how mods degrade gracefully when CAPIE is missing or too old.
---

# Versions and fallback

!!! proposed "Built, not yet released"

    CAPIE 1.1 (API level 2) carries these numbers in its binaries and in the script SDK. It has
    not been released or tested in the game yet.

## Three numbers

| Number | Example | What it identifies | Who compares it |
|---|---|---|---|
| API level | `2` (`CAPIE_API`) | What the script contract offers | Your mod, through `capie_min_api` |
| Release version | `1.1.0` (SemVer, `CAPIE_BUILD`) | One set of binaries | Players, the installer, release notes |
| Build number (`capie_ver`) | `10100` (major × 10000 + minor × 100 + patch) | One release, as an integer that fits a script variable | The multiplayer check |

API level 1 was CAPIE 1.0, which served only Rename Anything's private names. Level 2 adds the
editor grammar, the `capie_*` records, the wider alphabet and the `CAPIE_*` keys.

The API level only goes up, and only when the script contract gains something. A release that
adds support for a new game build without changing the contract keeps the same API level, so mods
need no update.

## Pin a minimum API level, not a release

Your mod states the lowest API level it needs and checks it inside the effect:

```paradox
set_temp_variable = { capie_min_api = 2 }
set_temp_variable = { capie_open_epoch = my_mod_epoch }
capie_v1_begin = yes
# capie_err = 10: CAPIE missing; 11: CAPIE older than capie_min_api
if = {
	limit = { capie_v1_ok = yes }
	# use the feature
}
capie_v1_report = yes                        # show the status line either way
```

Rename Anything is the exception: it ships with CAPIE and pins a CAPIE release.

## SDK major versions

The SDK carries its major version in file paths and names (`capie_sdk_v1_*`, `capie_v1_*`).
Minor additions get new names in the same files; an existing name never changes meaning. A new
major version goes into `capie_sdk_v2_*` with `capie_v2_*` names, and both can be loaded at the
same time.

## Deprecation

1. A name or record is marked deprecated in the SDK.
2. CAPIE keeps accepting or emitting it for at least one more API level and one release cycle.
3. It is removed only with a new major version.

CAPIE writes use of deprecated names to its log. It does not show them to players.

Rename Anything's private protocol stays frozen in 1.1: `rn_native_cap`, `RN_PLUGIN_STATUS`, its
editor names and the 84-code alphabet. CAPIE recognises it exactly as before. The `RN1|` cosmetic
tag prefix is also the public country-name protocol of the SDK and stays because existing saves
contain it.

## Graceful degradation

| Situation | What players see | What your script does |
|---|---|---|
| CAPIE loaded, API level high enough | Feature works | Normal branch |
| CAPIE not loaded | Indicator "CAPIE missing"; the editor that needs CAPIE is hidden or its Apply fails with a status line | Fallback branch, `capie_err = 10`, nothing changed |
| CAPIE too old for your mod | Same as missing, plus a version line in the campaign | Fallback branch |
| Game patched, no CAPIE release yet | Launcher message; indicators say "missing" | Fallback branch; no parse errors, no save damage |
| One CAPIE module ships disabled after a patch | That feature falls back; release notes say which | Reported API level drops to what still works |

What stays in the save without CAPIE:

| Object | Stored as | Without CAPIE |
|---|---|---|
| State, city | Vanilla `set_state_name` / `set_province_name` text | Unchanged; never needed CAPIE |
| Leader, party, character | Vanilla setter text | Unchanged; CAPIE was needed only to type it |
| Country name and adjective | `cosmetic_tag = "RN1\|...\|"` | Expected to show the original name. Seen with the mod enabled and CAPIE not loaded; not yet tested with the mod disabled |

A mod that uses cosmetic tags itself would overwrite a custom country name, and the other way
round. A trigger that lets focus trees check for a custom name first is not part of API level 2.
