---
description: CAPIE API levels, release versions, build indices, deprecation, and how mods degrade gracefully when CAPIE is missing or too old.
---

# Versions and fallback

!!! proposed "Proposed, subject to change"

    This versioning scheme is accepted as a direction. No CAPIE version has been released, so no
    number on this page is real yet.

## Three numbers

| Number | Example | What it identifies | Who compares it |
|---|---|---|---|
| API level | `1` | What the script contract offers | Your mod, through `capie_cap >= N` |
| Release version | `1.0.3` (SemVer) | One set of binaries | Players, the installer, release notes |
| Version number (`capie_ver`) | `103` | One release, as a small integer that fits a script variable | The multiplayer check |

The API level only goes up, and only when the script contract gains something. A release that
adds support for a new game build without changing the contract keeps the same API level, so mods
need no update.

## Pin a minimum API level, not a release

Your mod states the lowest API level it needs and checks it inside the effect:

<span class="proposed-tag">Proposed, subject to change</span>

```paradox
set_temp_variable = { capie_min_api = 1 }
if = {
	limit = { capie_v1_cmd_api_at_least = yes }
	# use the feature
}
else = {
	set_temp_variable = { capie_err = 10 }   # missing or too old: show the status line
}
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

Rename Anything's private protocol stays frozen: `rn_native_cap`, `RN_PLUGIN_STATUS` and the
`RN1|` cosmetic tag prefix. During the binary rename CAPIE would emit both `rn_native_cap` and
`capie_cap`. `RN1|` stays because existing saves contain it.

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

A mod that uses cosmetic tags itself would overwrite a custom country name. The SDK plans a
trigger, `capie_v1_has_custom_country_name`, so that focus trees can check first.
