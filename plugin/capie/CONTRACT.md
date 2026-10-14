# CAPIE script contract, API level 4 (CAPIE 1.2)

This is the versioned contract between CAPIE (the native DLLs in `plugin/capie/`) and HOI4
mods. It covers the names a mod gives its editor GUI, the temporary records CAPIE adds to the
player's Apply command, the localisation keys CAPIE answers, the alphabet, the error codes the
script SDK reports, behaviour without CAPIE, permissions, the multiplayer handshake and the
country name layer (section 14).

Status, 2026-10-14: API level 2 is implemented in `capie_core.dll` / `capie_editor.dll` 1.1 and
1.2 and in `sdk/capie-script-sdk/`; API level 3 (console) is in `CONTRACT-console.md`; API level
4 (section 14, country name layer) is implemented in the 1.2 developer build
(`plugin/build/capie-v12-dev`) and covered by offline tests. Nothing of level 4 has run in the
game yet. Rows marked **in-game test** below are the open checks.

The variant is B from `docs/API-DECISIONS.md`: plain HOI4 script, data passed through
temporary variables of one command, no new script keywords. A mod written against this
contract parses and runs the same way with or without CAPIE; only the branch it takes differs.

## 1. Versions

| Name | Value in 1.2 (1.1) | Where a mod sees it | Meaning |
|---|---|---|---|
| API level | `4` (`2`) | `temp_var:capie_cap`, loc key `CAPIE_API` | What scripts compare. It rises only when the contract gains something, and a level is never reused. Level 1 was CAPIE 1.0, which served only Rename Anything's private names; level 3 added the console commands (`CONTRACT-console.md`); level 4 adds the country name layer (section 14). Compare with `>=`. |
| Release | `1.2.0` (`1.1.0`) | loc key `CAPIE_BUILD` | Version of the binaries. |
| Build number | `10200` (`10100`) | `temp_var:capie_ver` | `major * 10000 + minor * 100 + patch`. Used by the multiplayer handshake, which needs an integer. |
| SDK major | `v1` | every SDK path and name (`capie_sdk_v1_*`, `capie_v1_*`) | A breaking change goes into `v2` names; two majors can coexist. |

Mods require a minimum API level (`capie_min_api`), never a release.

## 2. Reserved names

Every name that starts with `capie_` or `CAPIE_` belongs to CAPIE and its SDK: scripted effects,
triggers, variables, temporary variables, flags, arrays, localisation keys, game rules and GUI
element names. The one exception is the editor grammar in section 3: a mod names its own editor
GUI `capie_v1_...__<modid>__...`, and that is how CAPIE recognises it.

The mod id `capie` is reserved for the SDK's own windows.

## 3. Editor name grammar

CAPIE recognises a mod's editor only by exact names. It parses them; it never searches for
substrings.

```text
id                 [a-z0-9]{1,12}                         (mod id and editor id)
scripted GUI       capie_v1_ed__<mod>__<editor>           the scripted_gui key (the "model")
Apply effect       capie_v1_ed__<mod>__<editor>_apply_click
                   (the button is capie_v1_ed__<mod>__<editor>_apply)
edit box n         capie_v1_f__<mod>__<editor>__<n>       n = 1..4, consecutive from 1
prefill carrier n  capie_v1_pf__<mod>__<editor>__<n>      instantTextBox, optional
epoch carrier      capie_v1_ep__<mod>__<editor>           instantTextBox, required
```

- The grammar applies to the scripted GUI key, which is what the game passes to the hooks.
  `window_name` may be anything; the SDK examples use the same string.
- An editor has 0 to 4 fields. CAPIE finds field `n` with the game's own edit-box lookup in the
  window; the field count is the number of consecutive fields found from 1. The lookup
  searches edit boxes only and answers a miss with the game's null edit box, so a field exists
  exactly when the lookup returns an edit box whose definition has that name. An editor with
  no field is allowed: its Apply carries only the header records (used by the SDK's CAPIE check
  button).
- The longest model name is 13 + 12 + 2 + 12 = 39 bytes, the longest selector 51 bytes. Both
  stay below the 128-byte name limit of CAPIE's bounded string reader.
- Other `Open`, `Cancel` or custom buttons of the window are not annotated. They run as
  vanilla script.

Limits per game session (process): at most **32** registered editors and at most **8** per
mod id, first come first served. A window that matches the grammar but finds the registry full
still gets a sentinel Apply (code 2 below), so the mod can tell the player.

Rename Anything's own names (`rn_ip_*`, `rn_native*`, `rn_ip_cmd_epoch`) are a frozen legacy
alias set with their own records; see section 11.

## 4. The opening epoch

The epoch carrier is an `instantTextBox` whose text is a positive decimal integer the mod
stores (for example `[?ROOT.mymod_ed_epoch|0]`). The mod raises it when the editor opens and
after every successful Apply or Cancel. CAPIE:

1. reads the carrier when the window is visible; a new value starts a new opening;
2. prefills the fields once per opening (section 6);
3. on Apply, reads the carrier again and sends its value as `capie_epoch`.

The SDK accepts an Apply only when `capie_epoch` equals the stored value, then raises it.
A second Apply from the same opening (double click, Enter plus click, a command that arrived
twice) therefore fails with code 12 and changes nothing. The epoch is deduplication, not
authority.

Accepted range: 1 to 92 233 720 368 547 (the largest value that survives the 100 000
fixed-point scale of HOI4 variables). Zero, a sign, any non-digit or more than 15 digits
is rejected, and the opening is not ready.

## 5. Command records

CAPIE adds records only to the Apply command of a registered, visible editor, on the machine
of the player who clicked. Records are temporary variables of that one command; they exist only
while its effect runs.

Before adding anything, CAPIE removes every incoming record whose name starts with `capie_`.
A script, a save or a stale value cannot supply them. That stops spoofing from the window
context; it does not stop a mod from setting `capie_cap` inside its own effect, so `capie_cap` is
a presence signal, not a permission (section 9).

### 5.1 Payload Apply

| Record | Value |
|---|---|
| `capie_cap` | API level, `2` |
| `capie_ver` | build number, `10100` |
| `capie_epoch` | opening epoch read from the carrier, `>= 1` |
| `capie_nf` | number of fields that follow, `0..4` |
| `capie_f<n>_err` | `0` valid, otherwise the field error (5.3) |
| `capie_f<n>_at` | 1-based position of the first rejected character, `0` when none |
| `capie_f<n>_len` | number of characters, `0..32` (`0` when the field is invalid) |
| `capie_f<n>_0` .. `capie_f<n>_31` | character codes (section 7); `0` past the length and for an invalid field |

`n` runs from 1 to `capie_nf`. One field is 35 records; the largest Apply adds 4 + 4 × 35 =
144 records. Every name fits the game's 15-byte short-string layout, which CAPIE builds
directly.

An invalid field does not stop the command: the other fields still carry their codes, and the
script decides. A multi-field editor that changes nothing unless every field is valid is the SDK
default.

### 5.2 Sentinel Apply

When the editor is visible but CAPIE cannot send a payload, it still forwards the click once,
with only `capie_cap`, `capie_ver` and `capie_epoch = -code`:

| `capie_epoch` | Reason | What the player can do |
|---|---|---|
| `-1` | Opening not ready or changed while capturing (first frame after opening, a rebind, the epoch changed, a field disappeared, prefill still running) | Close and reopen the editor |
| `-2` | Editor registry full (section 3 limits) | Nothing in this session; a mod author issue |
| `-3` | Editor layout invalid: a fifth field (`__5`), or a gap (field `n + 1` without field `n`) | A mod author issue |

When the editor window is not visible or not consistent, CAPIE drops the click and logs it;
the game's own effect does not run. This matches Rename Anything's rule for a click that
cannot belong to a live editor.

### 5.3 Field errors (`capie_f<n>_err`)

The values are the native `InputError` codes, shared with Rename Anything:

| Code | Meaning |
|---|---|
| 0 | valid |
| 1 | empty |
| 2 | more than 32 characters |
| 3 | invalid Unicode scalar (surrogate or above U+10FFFF) |
| 4 | control character |
| 5 | markup or delimiter character: `" [ ] \| $ § £ \ { } # @` |
| 6 | character outside the alphabet |

Input is never trimmed, normalised or truncated by CAPIE.

## 6. Prefill (C8)

When an opening starts, CAPIE copies the text of `capie_v1_pf__<mod>__<editor>__<n>` into field
`n`, once, then reads it back. A field without a carrier is left as it is. CAPIE never writes
into a field that has keyboard focus, never writes twice in one opening, and never writes a
carrier text that holds a control or markup character. A failed or skipped prefill does not
block Apply; the transport reads the field fresh on every Apply.

Not in 1.1 (OPEN, need in-game evidence first): focus on open, select-all, a length cap while
typing, Escape handling, and releasing focus when a grammar editor hides. Rename Anything's
focus release covers only its politics-panel editors.

## 7. Alphabet (C3)

A character is sent as an integer code. Codes 1..84 are Rename Anything's table, unchanged:

| Codes | Characters |
|---|---|
| 1..26 | `a`..`z` |
| 27..52 | `A`..`Z` |
| 53..62 | `0`..`9` |
| 63..66 | space, `-`, `.`, `'` |
| 67..75 | `ą ć ę ł ń ó ś ź ż` |
| 76..84 | `Ą Ć Ę Ł Ń Ó Ś Ź Ż` |

API level 2 adds, for grammar editors only:

| Codes | Characters |
|---|---|
| 85..92 | `,` `!` `?` `(` `)` `&` `:` `/` |
| 93..152 | Latin-1 Supplement letters U+00C0..U+00FF in code-point order, without `×` (U+00D7), `÷` (U+00F7) and the two Polish letters already above (`Ó`, `ó`) |
| 153..264 | Latin Extended-A U+0100..U+017F in code-point order, without the 16 Polish letters already above |

The table lives once, in `sdk/capie-script-sdk/tools/capie_charset.py`; a check
(`plugin/capie/tests/check_alphabet.py`) proves that the native table in
`src/features/editor/input_codes.hpp` is the same list.

Fonts: `hoi_16mbs`, `hoi_18mbs`, `hoi_18`, `hoi_20b`, `hoi_20bs`, `hoi_26mbs`,
`hoi_16typewriter`, `hoi_22typewriter`, `Arial12`, `hoi_16tooltip3`, `hoi_33`, `cg_16b` and `cg_18b`
draw every character of the table (counted from `gfx/fonts/*.fnt` of 1.19.3.0). `garamond_*`,
`hoi_18b`, `hoi_30header`, `vic_*` and the typewriter fonts without the `hoi_` prefix lack Latin
Extended-A (which includes the Polish letters); `hoi_20`, `hoi_30`, `hoi_24header` and
`hoi_36header` lack part of it. Use a font from the first list for edit boxes and for text that
shows typed names.

Cyrillic is OPEN: the main fonts have no Cyrillic glyphs, and the game picks the `_cryllic`
fonts only for the Russian language.

Rename Anything's legacy fields keep the 84-code table; its decoder knows no other code.

## 8. Localisation keys (C4)

`capie_core.dll` answers these keys whenever the game draws them. The SDK localisation holds the
fallback, which is what players see without CAPIE.

| Key | With CAPIE 1.2 (1.1) | SDK fallback |
|---|---|---|
| `CAPIE_STATUS` | `CAPIE 1.2` (`CAPIE 1.1`) | `§OCAPIE missing§!` |
| `CAPIE_API` | `4` (`2`) | `0` |
| `CAPIE_BUILD` | `1.2.0` (`1.1.0`) | `-` |

They are text only. A trigger cannot read them, so they decide what is shown, never what runs.
Nest them in your own keys (`MYMOD_CAPIE_LINE: "$CAPIE_STATUS$"`). Whether the main menu draws
text through the hooked lookups is OPEN (architecture note Q10).

## 9. Permissions

The game runs a scripted GUI effect without repeating the button's `visible` or `enabled`
triggers, and it does not tell the effect who sent the command. CAPIE does not check permissions
and is not a trusted source of who clicked. Every effect checks authority itself. The SDK
provides the rule from `docs/API-DECISIONS.md` (decisions 6 and 7):

- `capie_v1_actor_ok` (trigger, ROOT = the acting country): `is_ai = no`.
- `capie_v1_may_edit_country` (trigger, scope = target country, ROOT = actor): the target is ROOT,
  or the target is a subject of ROOT (overlord rights, no veto), or the game rule
  `capie_v1_edit_rights` is `CAPIE_V1_OPEN`. The rule is set by the host in the lobby; its
  default `CAPIE_V1_OWN` allows only the first two.

A failed check is error 20 and changes nothing.

## 10. SDK effects and error codes (`temp_var:capie_err`)

The SDK (`sdk/capie-script-sdk/`, Workshop item "CAPIE Script SDK") is plain script. Inputs and
outputs are temporary variables. Trim, validation, storing and the setters do nothing while
`capie_err` is not 0, so an Apply effect can call them in a row and check once at the end.

| Effect | Inputs | Outputs |
|---|---|---|
| `capie_v1_begin` | `capie_open_epoch` (the value the epoch carrier shows), `capie_min_api` (default 2) | `capie_err` |
| `capie_v1_field_load` | `capie_field` (1..4) | work buffer `capie_c0..capie_c31`, `capie_len` |
| `capie_v1_text_trim` | work buffer | work buffer without leading and trailing spaces |
| `capie_v1_text_validate` | `capie_min_len` (default 1), `capie_max_len` (default 32) | `capie_err` 31..34 |
| `capie_v1_text_store` / `_load` / `_clear` | `capie_slot` (1..4) | array `capie_v1_txt<slot>` and `capie_v1_txt<slot>_len` on ROOT |
| `capie_v1_text_equals_slot` | `capie_slot` | `capie_eq` (0 or 1) |
| `capie_v1_set_country_name` | slots 1 (name) and 2 (adjective) of ROOT; called in the scope of the country | the country name layer (section 14) |
| `capie_v1_clear_country_name` | called in the scope of the country | the layer removed (section 14) |
| `capie_v1_set_state_name`, `_character_name`, `_party_name_<group>` | slots 1 and 2 of ROOT; called in the scope to rename | the vanilla name |
| `capie_v1_report` | `capie_err`, `capie_err_field`, `capie_err_at` | ROOT variables read by `[ROOT.CAPIE_V1_LAST_ERROR]` |

The localisation key `CAPIE_V1_TXT<slot>` draws a slot of ROOT. The four slots are per country
and shared by every mod that uses the SDK on that country: the setters use slots 1 and 2 as
scratch space, slots 3 and 4 are for text a mod keeps.

Set by `capie_v1_begin` and the text utilities. `capie_err_field` names the field, `capie_err_at`
the 1-based position when one applies.

| Code | Meaning | Source |
|---|---|---|
| 0 | ok | |
| 10 | CAPIE missing: no `capie_cap` in this command | header |
| 11 | CAPIE older than `capie_min_api` | header |
| 12 | stale opening: sentinel `-1`, or `capie_epoch` is not the stored opening | header |
| 13 | editor registry full (sentinel `-2`) | header |
| 14 | editor layout invalid (sentinel `-3`) | header |
| 16 | multiplayer lock: not every human player confirmed the same CAPIE build | handshake |
| 20 | not allowed by the permission rule | caller's check |
| 21..26 | field transport error 1..6 (section 5.3) | payload |
| 31 | shorter than `capie_min_len` | validation |
| 32 | longer than `capie_max_len` | validation |
| 33 | leading or trailing space | validation |
| 34 | two spaces in a row | validation |
| 35 | malformed payload: length or code out of range, a fraction, or a non-zero code past the length | payload |

## 11. Behaviour without CAPIE, and legacy aliases

Without CAPIE (not installed, unknown game build, failed initialization), the edit box still
takes typing, because it is a native widget. Apply runs the mod's effect with no `capie_*`
records; `capie_v1_begin` sets `capie_err = 10` and the SDK changes nothing. `CAPIE_STATUS`
shows the fallback. Saved data (SDK text slots, names already set through vanilla setters)
stays in the save. A country name layer (section 14) stays in the save as variables; without
`capie_core.dll` the game shows the country's cosmetic tag or base name, with the right flag.

Legacy country names (API level 2, CAPIE 1.0/1.1). The SDK setter of 1.1 stored
`RN1|Name|Adjective|` as the country's cosmetic tag, the protocol Rename Anything introduced;
`capie_core.dll` answers the three keys the game derives from it (the tag, `_DEF`, `_ADJ`). It
also replaced the flag and colour with the cosmetic tag's, which is why level 4 moved the name
into its own layer. The `RN1|` format stays frozen and readable, because saves contain it; since
level 4 nothing writes it. Rename Anything and the console drop an `RN1|` cosmetic tag once, on
the first Apply or reset of that country (section 14.5).

Rename Anything keeps its private names in 1.1: models `rn_ip_leader_gui`,
`rn_ip_party_neutrality_gui`, `rn_ip_country_gui`, records `rn_native(b)_*`, `rn_ip_cmd_epoch`,
`rn_native_cap`, status keys `RN_PLUGIN_STATUS*` and the 84-code alphabet. These are a frozen
alias set: CAPIE recognises them exactly as in 1.0 and does not add `capie_*` records to them.
Moving Rename Anything onto the grammar is planned for the next release that is tested in the
game, because its editors depend on behaviour the public contract does not offer (caption
refresh after Apply, the country capability gate, revision counters and focus release on the
politics panel), and because its 1.0 behaviour is accepted in the game and pinned by tests and
79 mutants. Migration steps are in `sdk/capie-script-sdk/MIGRATION.md`.

## 12. Multiplayer handshake (level L0)

Every grammar Apply carries `capie_ver`. The SDK keeps, per human country, the last build
it reported:

- `capie_v1_begin` stores `capie_v1_ver` on ROOT when `capie_cap` is present and clears it when it
  is absent, then recomputes the global flag `capie_v1_mp_ok`: set when every country with
  `is_ai = no` reports the same `capie_v1_ver` as ROOT.
- In multiplayer (more than one human country) an SDK Apply without `capie_v1_mp_ok` fails with
  error 16. In single player the check passes once ROOT reports.
- The SDK's CAPIE check window (`capie_v1_ed__capie__hello`, no fields) appears in multiplayer while
  the flag is unset. Each player presses it once per session.

The handshake runs on synchronised state only, so every client takes the same branch. Its
limits: a value saved by a player who later joins without CAPIE stays until that player presses
the check button or any SDK Apply; nobody is kicked; temporary records surviving network
serialisation is a hypothesis until the two-machine test (`docs/mp-test-plan.md`).

## 13. What needs an in-game test

| Item | Why |
|---|---|
| Grammar editor end to end: prefill, Apply, sentinel, registry limit message | new native path; only fake-memory tests so far |
| Records with 39..51-byte model and selector names reach the effect | longer names than Rename Anything's; bounded reads are tested offline |
| `capie_nf` and 4-field editors (144 records) | larger command than Rename Anything's 70 records |
| Typing and drawing the Latin Extended letters and the 8 punctuation marks | keyboard layouts, dead keys, `set_cosmetic_tag` / `set_state_name` with those characters |
| `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD` drawn in a campaign | new keys on an accepted mechanism |
| SDK text slots, trim and validation in the game's script engine | plain script, never parsed by the game yet; reading an array element into a temporary variable (`set_temp_variable = { capie_c0 = capie_v1_txt1^0 }`) and comparing it with one is not used by Rename Anything |
| `$CAPIE_V1_TXT3$` nested in another key, `[ROOT.CAPIE_V1_LAST_ERROR]` in a GUI | nesting is proven for status keys only |
| The demo mod (`sdk/capie-script-sdk/examples/capie_demo`) end to end | first consumer of the SDK |
| Game rule `capie_v1_edit_rights` in the lobby | new file |
| Multiplayer handshake | needs two machines |
| Name layer: own country, a subject (overlord), a country with a story cosmetic tag; flag and colour unchanged; reset back to the cosmetic tag; a later cosmetic tag change keeps the name; save, restart, load; load without CAPIE | Section 14; the variable read and the refresh trigger are pinned statically only |

## 14. Country name layer (API level 4, CAPIE 1.2)

### 14.1 What it is

A name a player (or a mod) gives a country is its own layer, separate from the cosmetic tag
(decision of 2026-10-13, `docs/API-DECISIONS.md`). On screen the order is:

1. the name layer, when set and valid;
2. otherwise the country's current cosmetic tag;
3. otherwise the base name of the tag.

The layer never changes the cosmetic tag, so flag and colour stay the game's. When the game or
another mod changes the cosmetic tag later, the layer keeps showing; removing the layer shows the
current cosmetic tag again, not the base name.

### 14.2 Variables (on the country that has the name)

| Variable | Value |
|---|---|
| `capie_cn_n` | array of the name's character codes, 1..32 elements, codes of section 7 (1..264) |
| `capie_cn_a` | array of the adjective's character codes, 1..32 elements |
| `capie_cn_ver` | `1` while the layer is set (format 1); absent = no layer |
| `capie_cn_rev` | set by every change, as the **last** statement; the value is not used |

They are ordinary country variables: written only by script effects, saved, and synchronised
in multiplayer like any other variable. The definite form (`GetNameDef`) is the name.

### 14.3 Writing and removing

```text
# Scope: the country. Set the layer (codes as in section 7):
clear_array = capie_cn_n
clear_array = capie_cn_a
add_to_array = { capie_cn_n = 40 }   # ... one per character of the name
add_to_array = { capie_cn_a = 14 }   # ... one per character of the adjective
set_variable = { capie_cn_ver = 1 }
set_variable = { capie_cn_rev = 1 }  # last: CAPIE refreshes the names now

# Remove it:
clear_array = capie_cn_n
clear_array = capie_cn_a
clear_variable = capie_cn_ver
set_variable = { capie_cn_rev = 1 }
```

The SDK effects `capie_v1_set_country_name` (from text slots 1 and 2 of ROOT) and
`capie_v1_clear_country_name` do exactly this. Use `set_variable` (not `add_to_variable`) for
`capie_cn_rev`, without a scope prefix, in the scope of the country.

### 14.4 What CAPIE does

`capie_core.dll` hooks `CCountry_UpdateNames` and `CCountry_BuildNameKey`: while the game
recomputes a country's cached name, adjective and definite name, a valid layer replaces the tag
the game builds the localisation keys from (`RN1|<name>|<adjective>|`, answered by the
localisation hooks). It hooks `CSetVariableEffect::Execute`: after a `set_variable` of
`capie_cn_rev` it recomputes the names of every country whose layer differs from what it shows.
The game also recomputes names on load and whenever the cosmetic tag, the ideology, the
popularities or a subject relation change; the layer is applied there too.

The layer is invalid, and treated as absent, when `capie_cn_ver` is not exactly 1, when a count
is outside 1..32 or not whole, when an element is missing, not whole or outside 1..264, or when
the text fails the RN1 guard. CAPIE writes no variable, no cosmetic tag and no country field;
it only changes what the game's name caches hold. Its pins are checked separately from the
localisation hooks: if they do not match, the layer is off (logged in `capie_core.log`) and
`RNCountryCapabilities` reports v1 instead of v2, so Rename Anything refuses country renames.

Not covered (they use the base tag, as without CAPIE): `GetNonIdeologyName*`, the
`NONIDEOLOGY*` parameters of subject name formats, the tooltip of `set_cosmetic_tag`, and
localisation that references a tag key such as `$CAN$` directly.

### 14.5 Legacy RN1 names

A country renamed by CAPIE 1.0/1.1 carries an `RN1|...` cosmetic tag. It keeps showing its name
(section 11). Rename Anything marks such countries with the flag `rn_custom_name` and, on the
first Apply or reset, drops that cosmetic tag once and sets `rn_cn_migrated`. The console
commands drop it only when the current cosmetic tag starts with `RN1|`. The cosmetic tag that
`RN1|` replaced was never stored, so it cannot be restored.

### 14.6 Multiplayer and without CAPIE

The layer is game state written by effects, so every client holds the same variables. A client
without CAPIE shows the cosmetic tag or base name with the correct flag; nothing goes out of
sync, because CAPIE only changes display caches. The handshake of section 12 is unchanged.
