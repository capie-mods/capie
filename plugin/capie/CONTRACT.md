# CAPIE script contract, API level 9 (CAPIE 1.7)

This is the versioned contract between CAPIE (the native DLLs in `plugin/capie/`) and HOI4
mods. It covers the names a mod gives its editor GUI, the temporary records CAPIE adds to the
player's Apply command, the localisation keys CAPIE answers, the alphabet, the error codes the
script SDK reports, behaviour without CAPIE, permissions, the multiplayer handshake, the
country name layer (section 14), the faction name layer (section 15), the country flag layer
(section 17), the country leader portrait layer (section 18, with format 2 since API level 9), the
country colour layer (section 19) and the runtime catalogs (section 21). Section 20 (a ruling-party
sub-ideology name layer) is withdrawn, not implemented.

Status, 2026-10-14: API level 2 is implemented in `capie_core.dll` / `capie_editor.dll` 1.1 and
1.2 and in `sdk/capie-script-sdk/`; API level 3 (console) is in `CONTRACT-console.md`; API level
4 (section 14, country name layer) is implemented in the 1.2 developer build
(`plugin/build/capie-v12-dev`), covered by offline tests and accepted in the game on 2026-10-14
(commit cb44cc0: no flag change, console reset, save, restart and load). API level 5 (section 15
faction name layer, `capie_pf1_*` records of section 5.1) is implemented in the 1.3 developer
build (`plugin/build/capie-v13-dev`) and covered by offline tests; it has not run in the game
yet. API level 6 (section 17, country flag layer; record `capie_flag`) is implemented in the 1.4
developer build (`plugin/build/capie-v14-dev`) and covered by offline tests; it has not run in
the game yet. API level 7 (section 18, country leader portrait layer; record `capie_portrait`) is
implemented in the 1.5 developer build (`plugin/build/capie-v15-dev`) and covered by offline
tests; it has not run in the game yet. API level 8 (section 19, country colour layer, record
`capie_colour`) is implemented in CAPIE 1.6 and covered by offline tests; its release candidate
ran in the game on 2026-10-29 (steps and results: `docs/contributing/test-session-1.6.0.md`).
Section 20 (ruling-party sub-ideology name layer, record `capie_subideo`) was withdrawn after that
test and is not implemented. API level 9 (CAPIE 1.7.0, unreleased; branch `api9-design`) adds the
portrait layer format 2 (texture paths, section 18.9), the state/VP catalog view and the
`capie_catalog` record (section 21); it is covered by offline tests and has not run in the game.
The flag and portrait catalogs of section 21 (21.7) are implemented on the assumption that the
in-game spikes S1/S2 pass (`docs/contributing/test-session-api9-spikes.md`); they are **pending
spikes**: if a spike fails, 21.7 changes before release. Rows marked **in-game test** below are the open checks.

The variant is B from `docs/API-DECISIONS.md`: plain HOI4 script, data passed through
temporary variables of one command, no new script keywords. A mod written against this
contract parses and runs the same way with or without CAPIE; only the branch it takes differs.

## 1. Versions

| Name | Value in 1.6 (1.5, 1.4, 1.3, 1.2, 1.1) | Where a mod sees it | Meaning |
|---|---|---|---|
| API level | `9` (`8`, `7`, `6`, `5`, `4`, `2`) | `temp_var:capie_cap`, loc key `CAPIE_API` | What scripts compare. It rises only when the contract gains something, and a level is never reused. Level 1 was CAPIE 1.0, which served only Rename Anything's private names; level 3 added the console commands (`CONTRACT-console.md`); level 4 the country name layer (section 14); level 5 the faction name layer (section 15) and the `capie_pf1_*` records (section 5.1); level 6 the country flag layer (section 17) and the `capie_flag` record; level 7 the country leader portrait layer (section 18) and the `capie_portrait` record; level 8 the country colour layer (section 19) and the `capie_colour` record (the sub-ideology name layer of section 20 and its `capie_subideo` record were withdrawn before release); level 9 the portrait layer format 2 (section 18.9), the catalog views (section 21) and the `capie_catalog` record. Compare with `>=`. |
| Release | `1.7.0` (`1.6.0`, `1.5.0`, `1.4.0`, `1.3.0`, `1.2.0`, `1.1.0`) | loc key `CAPIE_BUILD` | Version of the binaries. |
| Build number | `10700` (`10600`, `10500`, `10400`, `10300`, `10200`, `10100`) | `temp_var:capie_ver` | `major * 10000 + minor * 100 + patch`. Used by the multiplayer handshake, which needs an integer. |
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
| `capie_cap` | API level (`9` in CAPIE 1.7) |
| `capie_ver` | build number (`10700` in CAPIE 1.7) |
| `capie_flag` | API level 6: `1` when the country flag layer of section 17 is active in this game session (its pins matched), `0` when CAPIE runs without it. A flag editor refuses with a message when it is `0` or absent |
| `capie_portrait` | API level 7: `1` when the country leader portrait layer of section 18 is active in this game session (its pins matched), `0` when CAPIE runs without it. A portrait editor refuses with a message when it is `0` or absent |
| `capie_colour` | API level 8: `1` when the country colour layer of section 19 is active in this game session (its pins and RTTI checks matched and no game call has faulted), `0` otherwise. A colour editor refuses with a message when it is `0` or absent |
| `capie_catalog` | API level 9: `1` when the catalog views of section 21 are available in this game session (their pins, RTTI and the core's label export matched, and no game call has faulted), `0` otherwise. Each catalog action also reports whether it was served (21.1) |
| `capie_epoch` | opening epoch read from the carrier, `>= 1` |
| `capie_nf` | number of fields that follow, `0..4` |
| `capie_f<n>_err` | `0` valid, otherwise the field error (5.3) |
| `capie_f<n>_at` | 1-based position of the first rejected character, `0` when none |
| `capie_f<n>_len` | number of characters, `0..32` (`0` when the field is invalid) |
| `capie_f<n>_0` .. `capie_f<n>_31` | character codes (section 7); `0` past the length and for an invalid field |
| `capie_pf1_err`, `capie_pf1_at`, `capie_pf1_len`, `capie_pf1_0` .. `capie_pf1_31` | API level 5: the text prefill carrier 1 (`capie_v1_pf__<mod>__<editor>__1`) showed when this opening began, encoded like a field (same error codes; a character outside the alphabet gives `err = 6`). Sent only when the editor has that carrier, field 1 exists and the carrier text was readable at the opening; absent otherwise |

`n` runs from 1 to `capie_nf`. One field is 35 records; the opening text adds 35 more and
`capie_flag`, `capie_portrait`, `capie_colour` and `capie_catalog` one each, so the largest Apply
adds 8 + 4 × 35 + 35 = 183 records (144 before API level 5, 179 in API level 5, 180 in API level 6,
181 in API level 7, 182 in API level 8). Every name fits the game's
15-byte short-string layout, which CAPIE builds directly (the longest, `capie_portrait`, has 14
bytes).

`capie_pf1_*` is what the player saw before the change: with `[ROOT.GetName]` (or
`[This.GetFactionName]`) in the carrier it is the name on screen at the opening (player-given
name > cosmetic tag > base name), which a mod can use for a "X changes its name to Y" event.
It is captured once per opening, before prefill, and does not follow later changes of the
carrier. Like every record it travels inside the command, so every multiplayer client gets the
clicking player's value.

An invalid field does not stop the command: the other fields still carry their codes, and the
script decides. A multi-field editor that changes nothing unless every field is valid is the SDK
default.

### 5.2 Sentinel Apply

When the editor is visible but CAPIE cannot send a payload, it still forwards the click once,
with only `capie_cap`, `capie_ver` and `capie_epoch = -code`:

| `capie_epoch` | Reason | What the player can do |
|---|---|---|
| `-1` | Opening not ready or changed while capturing (first frame after opening, a rebind, the epoch changed, a field disappeared, prefill still running), or the window failed CAPIE's consistency check | Close and reopen the editor |
| `-2` | Editor registry full (section 3 limits) | Nothing in this session; a mod author issue |
| `-3` | Editor layout invalid: a fifth field (`__5`), or a gap (field `n + 1` without field `n`) | A mod author issue |
| `-4` | CAPIE stopped (CAPIE 1.7): a fault inside a call into the game set the fault latch, and CAPIE calls the game no more until it restarts, so no field can be read | Save, then restart the game; reopening the editor does not help |

Code `-4` is new in CAPIE 1.7 and has no API level of its own: a mod never asks for it, it only
receives it, and its meaning does not depend on anything else in the command. A script written
before it treats `-4` like any value that is not its stored opening (the SDK's error 12, "close
and reopen"), so it keeps working and only shows a less precise message. Check `capie_epoch = -4`
before the generic epoch comparison. CAPIE logs the fault once, with the call, the exception code
and the thread, in the log of the DLL whose call faulted (`capie_core.log` or `capie_editor.log`),
and the first refused call once in each DLL's log.

Rename Anything's private editors (section 11) receive the same codes as `rn_ip_cmd_epoch = -code`.
Their country editor gets `-4` as well when `capie_core.dll` is not loaded or does not report its
name layers (`RNCountryCapabilities`), because a stored name would not be shown.

When the editor window is not visible, CAPIE drops the click and logs it; the game's own
effect does not run. This matches Rename Anything's rule for a click that cannot belong to a
live editor. When the window fails the consistency check (for example its root is its parent,
which the game's window factory returns when it fails), the click is forwarded with
`capie_epoch = -1` and no payload, so the script can tell the player.

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

| Key | With CAPIE 1.7 (1.6, 1.5, 1.4, 1.3, 1.2, 1.1) | SDK fallback |
|---|---|---|
| `CAPIE_STATUS` | `CAPIE 1.7` (`CAPIE 1.6`, `CAPIE 1.5`, `CAPIE 1.4`, `CAPIE 1.3`, `CAPIE 1.2`, `CAPIE 1.1`) | `§OCAPIE missing§!` |
| `CAPIE_API` | `9` (`8`, `7`, `6`, `5`, `4`, `2`) | `0` |
| `CAPIE_BUILD` | `1.7.0` (`1.6.0`, `1.5.1`, `1.4.0`, `1.3.0`, `1.2.0`, `1.1.0`) | `-` |

Since CAPIE 1.7, `CAPIE_STATUS` answers `CAPIE 1.x: editor off` while `capie_editor.dll` is not
running: it has not reported a successful start to `capie_core.dll` (not loaded, or its
initialization failed), or a call into the game faulted and CAPIE stopped calling the game
(section 5.2, sentinel `-4`). Rename Anything's private status keys (`RN_PLUGIN_STATUS`, `_EN`)
then answer `CAPIE 1.x: editor off, restart the game`, `_PL` the Polish text. Every text still
starts with `CAPIE 1.x`; `CAPIE_API` and `CAPIE_BUILD` do not change.

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
| 15 | CAPIE stopped (sentinel `-4`, CAPIE 1.7): save, then restart the game. Reserved for the SDK release after CAPIE 1.7; an older SDK reports 12 | header |
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
serialisation is a hypothesis until the two-machine test (`docs/contributing/multiplayer-test-plan.md`).

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
| Name layer: own country, a subject (overlord), a country with a story cosmetic tag; flag and colour unchanged; reset back to the cosmetic tag; a later cosmetic tag change keeps the name; save, restart, load; load without CAPIE | Section 14; accepted in the game 2026-10-14 (cb44cc0) |
| Faction name layer: faction window, diplomacy, `[X.GetFactionName]`, tooltips; reset; leader change; save, restart, load; load without CAPIE | Section 15; `CFaction::GetName` is pinned statically only, the refresh of open windows is unverified |
| Flag layer: own country and a subject; the exact file (also another ideology's variant); name and colour unchanged; cosmetic tag change (event or console) shows the game's flag at once; reset; save, restart, load (full atlas rebuild on worker threads); load without CAPIE; a missing file | Section 17; the atlas keys and the load path are pinned statically only |
| Portrait layer: politics and diplomacy views, interesting countries, diplomatic request, adviser slot; small size; a game event with `set_portraits` after the choice keeps the choice, Restore shows the event's portrait; a generic leader; a missing sprite shows the game's portrait (not the unknown image); leader change; save, restart, load (before unpausing); load without CAPIE | Section 18; the portrait hook, the leader chain and the character layout are pinned statically only |
| Colour layer: political map repaint after a commit, `colors.txt` equivalence, faction colour, subjects in the overlord's colour, cosmetic tag change, the claims map mode (`colour_map_modes`; no "Getting continent for" lines in error.log), Restore, save and load with and without CAPIE, civil war copy of the variables, multiplayer with a hot-joiner, a broken pin, `tag_color` | Section 19; the getter hook, the refresh call and the map-mode dirty bytes are pinned statically only (T1..T12 of `docs/contributing/country-colour-layer-spec.md`) |
| Portrait layer format 2: a KaiserreduX `.png` leader path and a vanilla `.dds` path on the ruling leader; a missing file; a path large with a sprite small; `change_leader_portrait <TAG> gfx/...png`; save, load; load without CAPIE | Section 18.9; the path builder and its texture load are pinned statically only |
| State/VP view: state 1000 and a state above 1081 on KaiserreduX, a vanilla state; a renamed city shows within a second; a non-state scope; `capie_catalog` on an Apply | Section 21.6; the scope state id, the state and province layout and `ProvinceName` on the UI thread are pinned statically only |
| Sentinel `-4` ("CAPIE stopped") and the "editor off" status keys | Section 5.2 and 8, CAPIE 1.7; steps in `docs/contributing/test-session-1.7.0.md` |
| `capie_pf1_*` in an Apply (179 records with 4 fields) | Section 5.1; larger command than before, multiplayer serialisation unverified |

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

## 15. Faction name layer (API level 5, CAPIE 1.3)

### 15.1 What it is

A faction name a player gives is its own layer on the faction **leader**, separate from the name
the game stores in the faction (`create_faction` / `set_faction_name` store localised text,
not a key, so the game has no cosmetic layer for factions). On screen: the leader's faction
layer, when set and valid; otherwise the stored faction name; otherwise the faction template's
name. The stored name is never changed, so removing the layer shows it again. When the faction
gets another leader, the layer stays with the old leader and the faction shows the game's name.

### 15.2 Variables (on the faction leader)

| Variable | Value |
|---|---|
| `capie_fn_n` | array of the name's character codes, 1..32 elements (section 7) |
| `capie_fn_ver` | `1` while the layer is set; absent = no layer |
| `capie_fn_rev` | set by every change, as the **last** statement; the value is not used |

Write and remove them exactly like the country layer (14.3), with `capie_fn_*` names and only
one array. Use `set_variable = { capie_fn_rev = 1 }` in the scope of the leader.

### 15.3 What CAPIE does

`capie_core.dll` hooks `CFaction::GetName` (D8BB80), the accessor the faction views,
diplomacy and the `GetFactionName` localisation use. When members[0] of the faction (the
leader, `faction+0x58`) has a valid faction layer, CAPIE returns that text through the game's own
string copy; otherwise the game answers. The layer text is cached on the logic thread: when the
game recomputes a country's names (on load, cosmetic tag and ideology changes) and after every
`set_variable` of `capie_fn_rev`, so the accessor never reads variables. An open faction window
may show the new name only when it is redrawn (in-game check). The same validity rules as 14.4
apply; the pins of this layer are part of the name-layer pins (a failed pin turns both layers
off).

### 15.4 Without CAPIE, multiplayer

The variables stay in the save; without CAPIE the faction shows the game's name. The layer is
game state written by effects, so every client holds the same variables; CAPIE only changes
what the accessor returns.

## 16. Host record (not provided)

A `capie_host = 1` record ("the sender is the host of the session") was requested for API level
5. CAPIE 1.3 does not send it. A static candidate exists (a flag reached through the global at
332F698, virtual slot 0x88, byte +0x54, checked by the lobby before "only the server can change
the scenario" and at about 35 other places), but its meaning in a running game, and in single
player, is not established without a runtime test. Following the fail-closed rule, no record is
sent rather than a possibly wrong one. A script must keep treating "host" as unknown in
multiplayer. See [monorepo archive: docs/evidence/session-20261014/capie-v13/report.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261014/capie-v13/report.txt).

## 17. Country flag layer (API level 6, CAPIE 1.4)

Decision record: `docs/adr/0032-custom-flag-layer.md`. Design and review:
[monorepo archive: docs/evidence/session-20261014/flag-layer/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261014/flag-layer) (design.txt, review.txt K1 to K9).

### 17.1 What it is

A flag a player chooses for a country is its own layer, like the country name layer: the
cosmetic tag is not touched, so the name and the map colour stay the game's. On screen:

1. the flag layer, when set, valid, its snapshot of the cosmetic tag still matches (17.4) and its
   file loads;
2. otherwise the game's flag: cosmetic tag with ideology variant, cosmetic tag, tag with
   ideology variant, tag.

The layer names one file of `gfx/flags/` (and of `gfx/flags/medium/` and `gfx/flags/small/`,
which the game loads for the three flag sizes) by its stem, the file name without `.tga`, for
example `POL_KINGDOM_neutrality`. The game's ideology suffix is not added: the chosen file is
shown as it is, whatever the country's ideology. A missing or unreadable file shows the game's
flag.

There is no "overlord's flag, live" mode. A mod that offers the overlord's flag resolves the
overlord's current flag file in script and stores that stem (a snapshot); it does not follow the
overlord later.

### 17.2 Variables (on the country whose flag changes)

| Variable | Value |
|---|---|
| `capie_cf_ver` | `1` while the layer is set (format 1); absent = no layer |
| `capie_cf_len` | number of characters of the stem, `1..63` |
| `capie_cf_s` | array of the stem, three characters per element (17.3), `^num` = ceil(`capie_cf_len` / 3) |
| `capie_cf_cos_len` | cosmetic tag the country had when the flag was chosen: absent = no snapshot (no check), `0` = it had no cosmetic tag, `1..63` = its length |
| `capie_cf_cos` | array of that cosmetic tag, packed like `capie_cf_s` (only when `capie_cf_cos_len` > 0) |
| `capie_cf_rev` | set by every change, as the **last** statement; the value is not used |

They are ordinary country variables: written only by script effects, saved, synchronised in
multiplayer.

### 17.3 Character packing (file-name alphabet)

The stem and the cosmetic snapshot use their own alphabet, not the one of section 7:

| Codes | Characters |
|---|---|
| 1..26 | `a`..`z` |
| 27..52 | `A`..`Z` |
| 53..62 | `0`..`9` |
| 63 | `_` |

Characters `c0 c1 c2` (in reading order) are stored as one element `c0 * 4096 + c1 * 64 + c2`.
The last element is padded with `0`: a stem of 4 characters `POL_` is
`[42*4096 + 41*64 + 38, 63*4096]` = `[174694, 258048]`. Every other value is invalid: a code `0`
before the end, a non-zero code after it, an element outside `0..262143`, a fraction, a length
outside `1..63`, a `^num` that is not ceil(length / 3). Path characters (`/`, `\`, `.`, `:`)
cannot be expressed, so a stem can never leave the flag folders.

### 17.4 Cosmetic tag change resets the flag

A change of the cosmetic tag always ends the player's flag (ADR-0032). CAPIE writes no game
state, so the reset is shared:

- **Display, immediately (CAPIE):** while `capie_cf_cos_len` is present and the country's
  current cosmetic tag differs from the snapshot (or the snapshot is `0` and the country now has
  one), CAPIE treats the layer as absent. The game reloads the flag when its cosmetic tag
  changes, so the game's flag shows in the same frame as in vanilla.
- **State, by script (the mod):** a daily check (`on_daily`, limited to countries with
  `capie_cf_ver`) compares the cosmetic tag with the same snapshot and runs the mod's reset
  effect (17.5). It is identical on every client, so multiplayer stays in sync. A mod that cannot
  name the cosmetic tag (one it does not know) leaves the snapshot absent: then only its own
  daily check can reset.

Limits: a change A → B → A within one day is not reset by the daily check, and the display gate
shows the layer again after the return to A. The country name layer (section 14) does not reset
on a cosmetic tag change (ADR-0028).

### 17.5 Writing and removing

```text
# Scope: the country. Stem "POL_" (4 characters), snapshot: no cosmetic tag.
clear_array = capie_cf_s
add_to_array = { capie_cf_s = 174694 }
add_to_array = { capie_cf_s = 258048 }
set_variable = { capie_cf_len = 4 }
clear_array = capie_cf_cos
set_variable = { capie_cf_cos_len = 0 }
set_variable = { capie_cf_ver = 1 }
set_variable = { capie_cf_rev = 1 }        # last: CAPIE reloads the flag now

# Remove it (the game's flag comes back; nothing else changes):
clear_array = capie_cf_s
clear_array = capie_cf_cos
clear_variable = capie_cf_len
clear_variable = capie_cf_cos_len
clear_variable = capie_cf_ver
set_variable = { capie_cf_rev = 1 }
```

Use `set_variable` (not `add_to_variable`) for `capie_cf_rev`, without a scope prefix, in the
scope of the country. The reset does not change the cosmetic tag or the name layer.

### 17.6 What CAPIE does

HOI4 keeps country flags in three texture atlases (large, medium, small) with one slot per
country, and reloads a slot only when its key (ideology group, flag tag, cosmetic tag) changes.
`capie_core.dll` hooks three functions of that path:

- `LoadCountryFlag` (123F7E0) and `BuildFlagCandidates` (147D990): while a slot of a country with
  an active layer is loaded, the game's candidate list is built from the stem alone (no
  cosmetic tag, no ideology), so the game loads exactly that file with its own loader and
  overlay. If that file does not load, the slot is loaded a second time with the game's own
  candidates (fail-closed). The candidate hook changes arguments only when the call comes from
  this load for the same country (the cosmetic argument is that country's cosmetic tag field).
- `UpdateFlagAtlas` (123FE20), the game's per-frame atlas update: when a layer was set, changed
  or removed, CAPIE marks that country's slot key as changed (an impossible cosmetic id), so the
  game reloads the slot through its own incremental path.

The layer is read on the logic thread (when the game recomputes a country's names, which it does
on load and on every cosmetic tag change, and after `set_variable` of `capie_cf_rev`) into a
cache. The atlas hooks only read that cache: the full atlas rebuild loads flags in parallel on
worker threads, so nothing on that path reads variables or takes an exclusive lock.

Not covered: the flag on the save-game list and the main menu (built from save metadata), images
with a flag drawn into the texture, and the tooltip of the `set_cosmetic_tag` effect.

### 17.7 Activation, multiplayer, without CAPIE

The flag layer has its own pins (signatures of the three functions and the atlas key layout). If
one does not match, the flag layer is off and the name layers keep working; Apply commands then
carry `capie_flag = 0`. The flag is a local texture: CAPIE changes no game state, every client
holds the same variables, a client without CAPIE (or a save loaded without it) shows the game's
flags.

### 17.8 Console (developer build)

`change_nation_flag <TAG> <stem>` writes the layer (with the current cosmetic tag as the
snapshot) and `reset_nation_flag <TAG>` removes it, through `eval_effect`, in single player only
(`CONTRACT-console.md`).

## 18. Country leader portrait layer (API level 7, CAPIE 1.5)

Decision record: `docs/adr/0036-leader-portrait-and-feature-switches.md` (with its amendment:
U1, U2). Design and review: [monorepo archive: docs/evidence/session-20261014/portrait-layer/](https://github.com/SirCypkowskyy/hoi4-rename-anything/tree/monorepo-before-split-20261014/docs/evidence/session-20261014/portrait-layer) (design.txt,
review.txt).

### 18.1 What it is

A portrait a player chooses for a country leader is its own layer, like the flag layer: the
game's portrait data of the character is not touched (no `set_portraits`), so the choice can be
removed exactly. On screen, wherever the game draws the **civilian** portrait of the character
that is **currently the ruling country leader** of a country (politics view, diplomacy view,
interesting countries, diplomatic request window, the adviser slot when the same person sits
there):

1. the layer, when set, valid and its sprite exists in the game's sprite database;
2. otherwise the game's portrait (the character's own, one set by a game event with
   `set_portraits`, or the generic one the game picks).

Army and navy portraits (the same person as a general or admiral) are never changed. A
character who stops being the ruling leader shows the game's portrait again (the variables stay
on the character; when the person rules again, the layer shows again).

The layer names sprites by their game names (`GFX_portrait_...`), the large one and the small one
(the small one is drawn where the game uses the small size, for example in adviser slots). A
sprite of a mod works the same as a vanilla one, as long as its name fits the alphabet of 17.3.
Names of sprites a mod generates per version (a catalog index) must not be used: a save would
show another portrait after an update.

### 18.2 Variables (on the character, the ruling leader whose portrait changes)

| Variable | Value |
|---|---|
| `capie_pt_ver` | `1` while the layer is set (format 1); absent = no layer |
| `capie_pt_len` | number of characters of the large sprite name, `1..63` |
| `capie_pt_s` | array of the large sprite name, packed as in 17.3, `^num` = ceil(`capie_pt_len` / 3) |
| `capie_pt_slen` | number of characters of the small sprite name, `1..63`; absent or `0` = the game's small portrait stays (only as a last resort: a mod should always write the small one, 18.3) |
| `capie_pt_ss` | array of the small sprite name, packed as in 17.3 (only when `capie_pt_slen` > 0) |

The commit variable is on the **country**: `capie_pt_rev`, set by every change as the **last**
statement (value not used). They are ordinary variables: written only by script effects, saved
with the character and the country, synchronised in multiplayer. The character variables are
saved with the character (CCharacter::Write, token `variables`) and read back on load.

### 18.3 Which small portrait

Always write both sizes. For a character from `common/characters`, the small one is the
`small` of the same block when it has one; otherwise the large name with `_small` appended, when
that sprite exists (the game's own convention); otherwise the large one again (a large sprite in a
small frame is scaled by the game). Leave `capie_pt_slen` out only when none of these exists.

### 18.4 Writing and removing

```text
# Scope: the country. Sprite GFX_portrait_POL_ignacy_moscicki (32 characters) and its small one.
random_character = {
    limit = { is_country_leader = yes }
    clear_array = capie_pt_s
    add_to_array = { capie_pt_s = <element 1> }   # 11 elements, packed as in 17.3
    ...
    set_variable = { capie_pt_len = 32 }
    clear_array = capie_pt_ss
    add_to_array = { capie_pt_ss = <element 1> }  # GFX_portrait_POL_ignacy_moscicki_small, 38 characters, 13 elements
    ...
    set_variable = { capie_pt_slen = 38 }
    set_variable = { capie_pt_ver = 1 }
}
set_variable = { capie_pt_rev = 1 }        # last, in the country scope: CAPIE re-reads every leader now

# Remove it (the game's portrait comes back; nothing else changes):
random_character = {
    limit = { is_country_leader = yes }
    clear_array = capie_pt_s
    clear_array = capie_pt_ss
    clear_variable = capie_pt_len
    clear_variable = capie_pt_slen
    clear_variable = capie_pt_ver
}
set_variable = { capie_pt_rev = 1 }
```

`capie_pt_rev` uses `set_variable` (not `add_to_variable`) without a scope prefix, in a
country scope (any country: CAPIE re-reads the ruling leaders of all countries). The character
scope of the writes is the mod's choice; the example uses vanilla's idiom for "the leader".
CAPIE shows the layer only on the character that is the ruling leader per the game's own chain
(18.6), so writing to a non-ruling leader has no visible effect until that person rules.

Daily refresh (required): once a day (`on_daily` of the country, limited to a country whose
ruling leader has `capie_pt_ver`), set `capie_pt_rev = 1` again. CAPIE reads every ruling leader
when the game recomputes country names, which happens on load and on government changes; the
daily commit covers leader changes that do not recompute names. It is identical on every client.

### 18.5 Validity

The sprite names use the alphabet and packing of 17.3 (a path, `/`, `.` or `-` cannot be
expressed). A layer is invalid, and the game's portrait shows, when: `capie_pt_ver` is not `1`, a
length is outside `1..63`, a `^num` is not ceil(length / 3), an element is outside `0..262143` or a
fraction, a code `0` is followed by a non-zero code, the variables are unreadable, or the sprite
does not exist (CAPIE checks the substituted entry with the game's own validity check before
using it; on failure the game's own entry is kept, never the "unknown leader" image).

Sprites of a DLC: a layer may name a sprite that only a DLC defines. A client without that DLC
has no such sprite, so CAPIE shows the game's portrait there (the same check; the log notes only
the first eight misses per session). The variables stay the same on every client. Mods must not
read `has_dlc` (or anything else that differs between clients) in effects that write or remove
the layer: the stored state would then differ between clients and the game would go out of sync.
`has_dlc` belongs only in the GUI (for example which sprite collections a player can browse).

### 18.6 What CAPIE does

`capie_core.dll`:

- reads the layer on the logic thread into a cache: the ruling leader of a country through the
  game's chain (country → politics → ruling party → first leader → character, the path of the
  `set_country_leader_portrait` effect), when the game recomputes that country's names (on load,
  on government and name changes) and for all countries after `set_variable` of `capie_pt_rev`;
  the cache keeps, per country, the character, its id and the two names;
- hooks the function that resolves a character's portrait for the GUI (139DF20, the only one the
  portrait icons of the politics and diplomacy views use): after the game's own result, only for
  the civilian portrait of a cached character (same object, same id), it builds the entry for
  the chosen sprite, checks it with the game's validity function (139D1E0, sprite exists) and
  only then copies the name into the caller's buffer. Game data are never written.

### 18.7 Activation, multiplayer, without CAPIE

The portrait layer has its own pins (the hooked function and its only caller, the argument
layout, the validity check, the character layout as written by CCharacter::Write, the leader
chain). If one does not match, the portrait layer is off and the other layers keep working;
Apply commands then carry `capie_portrait = 0`. The portrait is local display: CAPIE changes no
game state, every client holds the same variables, a client without CAPIE (or a save loaded
without it) shows the game's portraits.

### 18.8 Console (developer build)

`change_leader_portrait <TAG> <sprite>` writes the layer on the ruling leader of `TAG` (the small
sprite is `<sprite>_small` when it exists, otherwise the large one) and `reset_leader_portrait
<TAG>` removes it, through `eval_effect`, in single player only (`CONTRACT-console.md`). Both
sprites are checked in the game's sprite database before the command runs.

### 18.9 Format 2: texture paths (API level 9, CAPIE 1.7)

Format 2 lets the layer name a texture **file** as well as a sprite, so the portraits of mods that
create leaders from image files (KaiserreduX's `create_country_leader` with
`picture = "gfx/leaders/…png"`) can be chosen. Format 1 (18.2 to 18.5) is unchanged and still read.

| Variable | Format 1 | Format 2 |
|---|---|---|
| `capie_pt_ver` | `1` | `2` |
| `capie_pt_len`, `capie_pt_s` | large sprite name, 17.3 packing, `1..63` | large sprite name **or path**, packing B (21.3), `1..126` |
| `capie_pt_slen`, `capie_pt_ss` | small sprite name, 17.3 packing | small sprite name or path, packing B; absent or `0` = the game's small portrait |

- A name that contains `/` is a path; any other name is a sprite name. Large and small may mix (a
  path large, a sprite small).
- A path must pass the path rules of 21.3 (`gfx/` prefix; `.dds`, `.png` or `.tga` in any case; no
  `..`, no `//`, no leading `/`; the packing B characters only).
- **Validity** adds: format 2 with a malformed packing B text or a path that fails the rules is
  `invalid`, and the game's portrait shows.
- **What CAPIE does with a path:** on the logic thread, when it reads the layer (18.6), it builds
  the portrait entry once per path with the game's own builder, which loads the texture through the
  game's texture manager, exactly as `create_country_leader` does, and keeps it for the game
  session (the game keeps such textures loaded too). In the portrait hook it checks the entry with
  the game's validity function (a path entry is valid only when its texture loaded) and then copies
  it into the caller's buffer the way the game's own copy does. **An entry whose texture did not
  load is rejected and the game's portrait shows** (a missing or unreadable file, or a file only
  another client has). A path is tried once per session; at most 1024 distinct paths are built
  (past that, path layers show the game's portrait and the log says so once).
- Path support has its own pins inside the portrait layer (the builder, its texture load and the
  validity check of a path entry). If they fail, format 2 paths are `invalid` and sprite names
  (both formats) keep working; `capie_portrait` still reports the layer.
- Multiplayer: the variables are synchronised game state as before; each client loads the file
  locally and shows the game's portrait when it cannot.
- Console (developer build): `change_leader_portrait <TAG> <path>` writes format 2 (the path as
  large and small) without the sprite check; the texture check happens when the portrait is drawn.

```text
# Scope: the ruling leader. Path gfx/a.png (9 characters, 3 elements of packing B).
clear_array = capie_pt_s
add_to_array = { capie_pt_s = 1700728 }   # g f x
add_to_array = { capie_pt_s = 782510 }    # / a .
add_to_array = { capie_pt_s = 1849191 }   # p n g
set_variable = { capie_pt_len = 9 }
# ... capie_pt_ss / capie_pt_slen likewise ...
set_variable = { capie_pt_ver = 2 }
# then, in a country scope, as for format 1:
set_variable = { capie_pt_rev = 1 }
```

## 19. Country colour layer (API level 8, CAPIE 1.6)

Decision record: `docs/adr/0038-country-colour-layer.md`. Design: `docs/contributing/country-colour-layer-spec.md`
and `docs/research/country-colour-2026-10-28.md`.

### 19.1 What it is

A colour a player picks for a country is its own layer, like the flag layer: the cosmetic tag,
the flag and the name are not touched. Wherever the game asks for a country's map colours
(the political map fill and borders, the faction colour of a faction it leads without a custom
colour, the UI colour swatches), the colour comes from:

1. the colour layer, when it is set, valid, and its cosmetic-tag snapshot still matches (19.4);
2. otherwise the game's colour (cosmetic tag colour, else the `colors.txt` colour).

A subject drawn in its overlord's colour (the game's own rule) shows the overlord's layer.

### 19.2 Variables (on the country whose colour changes)

| Variable | Value |
|---|---|
| `capie_cc_ver` | `1` while the layer is set (format 1); absent = no layer |
| `capie_cc_r`, `capie_cc_g`, `capie_cc_b` | the picked colour, whole numbers `0..255`, as written in `colors.txt` `color = rgb { R G B }`. CAPIE applies the `NGraphics` country colour defines to it, exactly as the game does when it reads `colors.txt` (19.3) |
| `capie_cc_cos_len` | cosmetic tag the country had when the colour was picked: absent = no snapshot (no check), `0` = it had no cosmetic tag, `1..63` = its length |
| `capie_cc_cos` | array of that cosmetic tag, packed as in 17.3 (only when `capie_cc_cos_len` > 0) |
| `capie_cc_rev` | set by every change, as the **last** statement; the value is not used |

They are ordinary country variables: written only by script effects, saved, synchronised in
multiplayer.

### 19.3 Colour maths

From `R G B`, CAPIE builds the two colours the game keeps per country, the way the game parses
`colors.txt`: `color` with the defines `COUNTRY_COLOR_HUE_MODIFIER`,
`COUNTRY_COLOR_SATURATION_MODIFIER` and `COUNTRY_COLOR_BRIGHTNESS_MODIFIER`, and `color_ui` with
`COUNTRY_UI_COLOR_HUE_MODIFIER`, `COUNTRY_UI_COLOR_SATURATION_MODIFIER` and
`COUNTRY_UI_COLOR_BRIGHTNESS_MODIFIER`. In HSV with the hue in `[0,1)`: `H += hue`,
`S *= saturation`, `V *= brightness`, no clamp, alpha 1. The defines are read from the running
game each time, so modded defines apply. With the vanilla defines (map S x0.6, V x0.8; UI
unchanged), a country given another country's `colors.txt` RGB looks exactly like it.

### 19.4 Validity and the cosmetic tag

| Status | Condition | Shown |
|---|---|---|
| `absent` | `capie_cc_ver` missing (or the country has no variables) | game colour |
| `invalid` | `capie_cc_ver` is not `1`; a channel missing, a fraction, or outside `0..255`; a malformed snapshot (as in 17.3) | game colour |
| `unreadable` | the variables could not be read | game colour |
| `hidden` | valid, but `capie_cc_cos_len` is present and the country's current cosmetic tag differs from the snapshot (or the snapshot is `0` and the country now has one) | game colour |
| `valid` | otherwise | the layer |

A cosmetic tag change therefore hides the layer at once, like the flag layer (17.4). The state
is the mod's: a daily check (`on_daily`, limited to countries with `capie_cc_ver`) compares the
cosmetic tag with the snapshot and removes the layer (19.5), identically on every client.

| Case | Behaviour |
|---|---|
| Cosmetic tag change | Display: hidden at once (snapshot mismatch). State: the mod's daily check removes it. |
| Civil war or a new dynamic tag | If the game copies the variables to the breakaway (in-game test), the mod removes the layer there. |
| Annexation | The layer stays on the dead country and returns on release. Removing it is the mod's choice. |
| Restore | Remove the layer (19.5); the cosmetic or base colour returns. |
| Save and load | The variables are saved; CAPIE rebuilds its state when the game recomputes names on load. |

Two countries may share a colour; CAPIE does not check it.

### 19.5 Writing and removing

```text
# Scope: the country. Colour 30 90 160, no cosmetic tag at the time.
set_variable = { capie_cc_r = 30 }
set_variable = { capie_cc_g = 90 }
set_variable = { capie_cc_b = 160 }
clear_array = capie_cc_cos
set_variable = { capie_cc_cos_len = 0 }
set_variable = { capie_cc_ver = 1 }
set_variable = { capie_cc_rev = 1 }        # last: CAPIE repaints now

# Remove it (the game's colour comes back):
clear_variable = capie_cc_r
clear_variable = capie_cc_g
clear_variable = capie_cc_b
clear_array = capie_cc_cos
clear_variable = capie_cc_cos_len
clear_variable = capie_cc_ver
set_variable = { capie_cc_rev = 1 }
```

Use `set_variable` (not `add_to_variable`) for `capie_cc_rev`, without a scope prefix, in the
scope of the country. With a cosmetic tag, write its packed characters to `capie_cc_cos` and its
length to `capie_cc_cos_len`, as for `capie_cf_cos` (17.5).

### 19.6 What CAPIE does

`capie_core.dll`:

- hooks `GetMapColors` (734AE0), the getter that the 16 users of a country's colours call (map
  modes, map renderer, faction colour, UI). After the game's own lookup, which already walks to
  the overlord, it returns CAPIE's own colour block for that resolved country when its layer is
  shown. The getter makes no game call, takes no lock and reads no variables;
- re-evaluates every country after `set_variable` of `capie_cc_rev`, and one country when the
  game recomputes its names (on load, cosmetic tag, ideology and subject changes);
- for every country whose shown colour changed: publishes or removes its block, calls the
  game's own refresh `RefreshCountryColor` (AB7F10, the call the game makes after a cosmetic
  tag change; it refreshes the political map and the faction colour), and marks dirty the map
  modes whose builders read the colour (the political and the claims mode), so that the game's
  next map update rebuilds them. It never marks the other modes: one of them (the continent
  mode) writes a line per province to `error.log` when it is rebuilt. Both happen only while the game is
  in a campaign (the in-game idler), on the logic thread, at the effect step that commits the
  variables, on every client.

CAPIE writes no variable, no cosmetic tag and no `CCountry` field. The faction colour the game
copies from the leader is game state: it is identical on every client that runs CAPIE, and it
is saved.

### 19.7 Activation, multiplayer, without CAPIE

- **Pins fail:** the colour layer has its own pins (the getter, the refresh function, the
  colour block layout, the map-mode dirty bytes and the modes it marks, the idler check, the six
  define globals) and
  RTTI checks (`CCountryColors`, `CColor`, `CCountry`, `CInGameIdler`). If one does not match,
  the layer is off, the getter is not hooked, colours stay vanilla, the other layers keep
  working, and Apply commands carry `capie_colour = 0`. A fault in a game call at run time
  also turns the layer off (vanilla colours from then on).
- **Multiplayer:** the layer is a pure function of synchronised variables, updated at the
  effect step on every client. Every player needs the same CAPIE build (section 12).
- **A client without CAPIE:** sees vanilla colours. A save made with the layer keeps the
  overridden faction colour of a faction the country leads (cosmetic only).
- **Console:** none.

## 20. Ruling-party sub-ideology name layer (withdrawn, not implemented)

Withdrawn on 2026-10-29, after the in-game test of the 1.6.0 release candidate: the project
decided the layer is not needed. CAPIE 1.6 has no such layer. It hooks nothing in the politics
panel, reads no `capie_sn_*` variable, adds no `capie_subideo` record to an Apply, and
`RNCountryCapabilities` defines no bit for it (v4 bit `0x8` stays unused). A mod sets
`capie_sn_*` variables without effect; it must not wait for `capie_subideo`, which is always
absent. API level 8 is the country colour layer (section 19) and the `capie_colour` record only.

The section number is not reused. The design is kept, marked "dropped, not implemented", in
`docs/research/subideology-rename-design.md`.

## 21. Runtime catalogs (API level 9, CAPIE 1.7)

Design: `docs/contributing/api9-runtime-catalogs-spec.md`; evidence:
`docs/research/runtime-catalogs-2026-11.md`. A catalog view lets a mod show what the **running**
game has (a state's victory points today; flags and portraits once their spikes pass), with no list
generated per mod. Catalogs live only in CAPIE's memory on the clicking machine; what a mod keeps
travels as records inside the click's command (section 5), so every client stores the same values.

Status in CAPIE 1.7.0:

| Part | Status |
|---|---|
| Names, records, sentinels, keys (21.2 to 21.5) | implemented |
| `vp` action, the state/VP view (21.6) | implemented |
| Packing B (21.3) | implemented (used by the portrait layer format 2, 18.9) |
| `flag`, `flagown`, `portrait`, `portraitown`, `first`, `prev`, `next`, `last`, `pick`, slot sprites (21.7) | implemented, **pending spikes** S1/S2 (may change if a spike fails); with their own pins failed the action carries `capie_catalog = 0` |

### 21.1 Versions and availability

- API level `9`, build `10700`. Every catalog action carries `capie_cap` and `capie_ver`.
- `capie_catalog` on an editor Apply (5.1) says whether the catalog views are available at all.
  On a catalog action it says whether **that** action was served: `1` served, `0` not available
  (feature off, or that catalog's own pins failed), negative = a sentinel (21.5).
- The feature has its own pins and RTTI checks; if they fail, every catalog action carries
  `capie_catalog = 0` and the layers and editors keep working.

### 21.2 Names

All names are under the reserved prefixes of section 2. Ids are `[a-z0-9]{1,12}`; the whole name
must match (no case folding, no substring search).

```text
scripted GUI   capie_v1_cat__<mod>__<view>                        (the scripted_gui key, the "model")
action         capie_v1_cat__<mod>__<view>__<action>_click          (a button capie_v1_cat__<mod>__<view>__<action>)
actions        vp                                                  (implemented)
               flag | flagown | portrait | portraitown              (pending spikes, 21.7)
               first | prev | next | last | pick                    (pending spikes, 21.7)
slot sprites   GFX_capie_v1_s__<mod>__<view>__<set>_<slot>          set 0 or 1, slot 0..N-1 (21.7)
               GFX_capie_v1_s__<mod>__<view>__p<set>                the preview of each set
text keys      CAPIE_V1_T__<MOD>__<VIEW>__<k>     row/slot k: 0..31 for vp, 0..N-1 for a picker (decimal)
               CAPIE_V1_T__<MOD>__<VIEW>__COUNT   items in the view, decimal
               CAPIE_V1_T__<MOD>__<VIEW>__PAGE    "1 / 1", or "0 / 0" when empty
               CAPIE_V1_T__<MOD>__<VIEW>__SEL     the picked item (empty for vp)
```

- `<MOD>` and `<VIEW>` in a key are the ids in upper case.
- The longest model is 40 bytes, the longest selector 59 (`…__portraitown_click`).
- Limits per game session: at most **16** views and **4** per mod id, first come first served. A
  view registers when its window is bound (or on its first action). A click of a view that does not
  fit gets sentinel `-2`.
- Other buttons of the window are not catalog actions and run as plain script.

### 21.3 Packing B (names with `/ . - '`)

The 17.3 alphabet (63 codes) cannot express paths. Packing B stores the **ASCII value** of each
character:

- Characters: `A-Z a-z 0-9 _ - . / '` (codes 39..122).
- Three characters per element: `c0 * 16384 + c1 * 128 + c2` (at most 2 097 151, below the script
  limit of 2 147 483). The last element is padded with `0`. Length `1..126`, `^num` = ceil(length / 3),
  at most 42 elements.
- Invalid: a code outside the set, a `0` before the end or a non-zero code after it, an element
  outside `0..2097151`, a fraction, a length outside `1..126`, a `^num` that is not ceil(length / 3).
- Path rules (a name with `/`): starts with `gfx/`; ends with `.dds`, `.png` or `.tga` (any case); no
  `..`, no `//`, no leading `/`. Non-ASCII paths cannot be expressed and are not offered.

Example: `gfx/a.png` = `[1700728, 782510, 1849191]`, length 9.

### 21.4 Records

CAPIE removes every incoming `capie_*` record of the click (including a dynamic list's
`capie_v1_slot`) and adds:

| Record | Value |
|---|---|
| `capie_cap`, `capie_ver` | `9`, `10700` |
| `capie_catalog` | `1` = served; `0` = not available; `-code` = sentinel (21.5). With anything but `1`, no record below is sent |
| `capie_cat` | the catalog shown: `1` flag, `2` portrait, `3` vp |
| `capie_cn` | items in the view (vp: VP provinces, `0..32`; pickers: up to 65 535) |
| `capie_pg`, `capie_pgs` | page shown (1-based) and number of pages; `0` and `0` when the view is empty (vp: `1` and `1` otherwise) |
| `capie_pn` | items on the page shown, `0..N` (vp: `= capie_cn`) |
| `capie_par`, `capie_ppar` | slot set and preview set to show, `0` or `1` (`0` for vp) |
| `capie_st` | vp: the scope state's numeric id; `0` when the scope holds no state |
| `capie_vp_<k>`, `capie_vpv_<k>` | vp: province id and VP value of row `k`, `k = 0 .. capie_cn - 1` |

A `vp` action adds at most 10 + 1 + 64 = 75 records. Every name fits the 15-byte short-string
layout (the longest, `capie_catalog`, has 13 bytes). The values are the clicking machine's
reading of synchronised game state (state and province ids, VP values), so they are the same
everywhere; mods must still not branch saved state on catalog counts beyond storing them.

### 21.5 Sentinels

The click is forwarded once with only `capie_cap`, `capie_ver` and `capie_catalog = -code`:

| Code | Reason |
|---|---|
| `-1` | the window is not visible, or it failed CAPIE's consistency check |
| `-2` | the view registry is full (21.2 limits) |
| `-3` | the view is invalid: a flag or portrait action on a view whose slot sets are missing, of different sizes, larger than 48, or without both previews |
| `-4` | `pick` without a valid `capie_v1_slot` (missing, not whole, negative, or not a slot of the page shown) |
| `-5` | the game forbids gamestate access on the clicking thread at that moment (`ThreadForbidCount != 0`) |

### 21.6 The state/VP view (`vp`)

Use a scripted GUI with a **state scope**: `context_type = selected_state_context`, or a window whose
scope is a state. On `…__vp_click` CAPIE, on the clicking machine and before the command is sent:

1. reads the scope's state id (`0` when the scope is not a state: an empty view, `capie_st = 0`);
2. reads the state's provinces and keeps those with victory points `> 0`, sorted by VP value,
   highest first, then by province id; at most 32 (the 32 highest);
3. reads each province's **shown name** through the game's own function: the player-given name
   (`set_province_name`), else the country-specific VP name (`<TAG>_VICTORY_POINTS_<id>`), else the
   VP name (`VICTORY_POINTS_<id>`), else what the game shows otherwise;
4. publishes the names as `CAPIE_V1_T__<MOD>__<VIEW>__<k>` (empty for `k >= capie_cn`), `__COUNT`
   and `__PAGE`, and sends the records of 21.4.

While the window is visible, CAPIE reads the names of the same provinces again at most every 500 ms,
so a rename shows within about half a second without a click. The rows and records change only on
the next `vp` click.

- Keys answer only after their view registered; before that the game's lookup runs and the
  mod's own localisation (the fallback) shows. Answers are text only; a label never contains `$`,
  `[`, `]` or a control character (they are shown as a space) and is cut at 255 bytes.
- Use the keys through a scripted-localisation wrapper (`text = "[RnVpRow3]"`), as Rename Anything's
  `RN_VPROW_*` do; whether a plain key in `text` is re-evaluated after a change is an open in-game
  check (spec S3).
- Example records for KaiserreduX state 1000 with VPs 13671 (value 25) and 10472 (value 10):
  `capie_catalog = 1, capie_cat = 3, capie_cn = 2, capie_pg = 1, capie_pgs = 1, capie_pn = 2,
  capie_par = 0, capie_ppar = 0, capie_st = 1000, capie_vp_0 = 13671, capie_vpv_0 = 25,
  capie_vp_1 = 10472, capie_vpv_1 = 10`.

```text
scripted_gui = {
    capie_v1_cat__mymod__vp = {
        context_type = selected_state_context
        window_name = "capie_v1_cat__mymod__vp"
        effects = {
            capie_v1_cat__mymod__vp__vp_click = {
                if = {
                    limit = { check_variable = { capie_catalog = 1 } }
                    ROOT = {
                        set_variable = { mymod_state = capie_st }
                        set_variable = { mymod_vp_count = capie_cn }
                        set_variable = { mymod_vp_0 = capie_vp_0 }   # ... one per row the mod shows
                    }
                }
                # else: the mod's static data or a "not available" message
            }
        }
    }
}
```

### 21.7 Flag and portrait catalogs (pending spikes)

Implemented on the assumption that the in-game spikes S1/S2 pass; **if a spike fails, this
subsection changes before release.** The view needs its own pins (slot sprites, the game's file
listing, the sprite map); without them its actions carry `capie_catalog = 0`.

**Slot sprites (the mod's `.gfx`).** Per view, two sets of `N` sprites (`N` = 1..48) and one preview
per set; every `texturefile` is a real file of the mod (the placeholder, shown without CAPIE):

```text
spriteType = { name = "GFX_capie_v1_s__rn__fp__0_0"  texturefile = "gfx/interface/rn/slot_blank.dds" }   # set 0, slots 0..N-1
spriteType = { name = "GFX_capie_v1_s__rn__fp__1_0"  texturefile = "gfx/interface/rn/slot_blank.dds" }   # set 1, slots 0..N-1
spriteType = { name = "GFX_capie_v1_s__rn__fp__p0"   texturefile = "gfx/interface/rn/slot_blank.dds" }   # preview, set 0
spriteType = { name = "GFX_capie_v1_s__rn__fp__p1"   texturefile = "gfx/interface/rn/slot_blank.dds" }   # preview, set 1
```

`N` is the number of consecutive slots of set 0 from 0; set 1 must have exactly `N`, and both
previews must exist, else the view is invalid (sentinel `-3`). The grid's `image` is
`GFX_capie_v1_s__<mod>__<view>__[?ROOT.<par>]_[?<slot>]` with `<par>` stored from `capie_par`; the
preview is `…__p[?ROOT.<ppar>]` with `<ppar>` stored from `capie_ppar`.

**Catalogs** (built on the clicking machine, from the running game, once per session):

| Catalog | Items | Slot shows | Label (`…__<k>`, `…__SEL`) |
|---|---|---|---|
| flag (`capie_cat = 1`) | stems in all of `gfx/flags`, `gfx/flags/medium` and `gfx/flags/small` (`.tga`), 1..63 characters of 17.3 | `gfx/flags/medium/<stem>.tga` | the stem |
| portrait (`capie_cat = 2`) | sprites named `GFX_portrait*` or `GFX_leader*` (any case, not ending in `_small`), plus `.dds`/`.png`/`.tga` files under `gfx/leaders/` that pass the path rules and are not the file of a listed sprite | the sprite's own file, or the path | the sprite name, or the file name without folders |

Both are sorted by byte order without duplicates (at most 65 535 items); positions mean nothing
outside the session. The portrait catalog is rebuilt when the sprite database changes (a GUI reload).

**Actions:**

| Action | Effect |
|---|---|
| `flag`, `portrait` | the whole catalog, page 1 |
| `flagown` | stems equal to the scope country's tag `T` or starting with `T_`, and those equal to or starting with `<cosmetic>_` of its current cosmetic tag; empty when the scope is not a country |
| `portraitown` | sprite names containing `_T_`, paths containing `/T/` |
| `first`, `prev`, `next`, `last` | page 1, one back (not below 1), one on (not past the last), the last page of the current view; `capie_catalog = 0` before any catalog action of the view |
| `pick` | the item in the clicked slot of the page shown (`capie_v1_slot`, a dynamic list entry's `value`) |

On a page action CAPIE loads the page into the **hidden** set and the records carry the set to show
in `capie_par`; on `pick` it loads the item into the hidden preview and flips `capie_ppar`. Labels of
slots past the end of a page are empty, and those slots show their placeholder.

**Records of a page action:** the header of 21.4 (`capie_catalog = 1`, `capie_cat`, `capie_cn`,
`capie_pg`, `capie_pgs`, `capie_pn`, `capie_par`, `capie_ppar`). **`pick` adds:**

| Record | Value |
|---|---|
| `capie_sel_fmt` | `1` = 17.3 packing (a flag stem), `2` = packing B (a sprite name or a path) |
| `capie_sel_len`, `capie_sel_0` .. `capie_sel_41` | the item's name: 1..63 characters in format 1 (21 elements), 1..126 in format 2 (42 elements) |
| `capie_sml_len`, `capie_sml_0` .. `capie_sml_41` | portrait only, packing B: `<name>_small` when that sprite exists, else the name; for a path the same path |
| `capie_cos_len`, `capie_cos_0` .. `capie_cos_20` | flag only: the scope country's current cosmetic tag in 17.3 packing; `capie_cos_len = 0` when it has none, `-1` (and no elements) when it cannot be expressed in 17.3 |

The largest `pick` adds 10 + 1 + 43 + 43 = 97 records. Store the name, never a position: the flag
picker writes `capie_cf_*` (section 17) from `capie_sel_*` and, as the snapshot, `capie_cos_*`;
the portrait picker writes the format 2 variables of 18.9 from `capie_sel_*` and `capie_sml_*`.

**Display rules (spike-dependent):** CAPIE re-points only the hidden set and the hidden preview. It
releases a replaced texture at least 2 GUI updates later on the UI thread, and never releases a
portrait-path texture of 18.9. Flipping pages therefore keeps a bounded number of textures loaded
(about `2 × N + 2` per view).
### 21.8 Without CAPIE, multiplayer

- **Without CAPIE:** the action runs the mod's effect with no `capie_*` record; the keys show the
  mod's fallback text. The mod uses its static data or shows a message.
- **Multiplayer:** only the effect writes game state, from records that travel in the command, so
  every client stores the same values (the same open item as every editor: records surviving
  network serialisation, section 12). Labels and views are local and never written to game state.
  A client whose catalog differs only displays differently.