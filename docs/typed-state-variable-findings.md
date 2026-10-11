# Typed state variables and the Cities lookup

Date: 2026-10-11. Read-only static investigation, stopped at the user's request to save progress. No game/UI action, injection, Frida attachment, rebuild, or game-file change was performed. Proposed script changes have not been verified at runtime by this investigation.

Target: Windows x64 HOI4 1.19.3.0, EXE SHA256 `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`, base `0x140000000`. All addresses below are RVAs. Ghidra used the existing exact-hash project with `-noanalysis -readOnly`; function aliases in the output are analyst labels.

## Confirmed native semantics

A variable holding a state scope is not the ordinary numeric ID of that state. Native scope resolver `0x542500` outputs the selected state's ID through its state output argument, taking it from event-scope `+0xA8`. Scope-literal evaluator `0x543EE0` then stores:

```text
tagged_state_value = state_id | 0xFFFFFFFFC0000000
```

This is a negative signed 64-bit value. In contrast, numeric literal parsing in `0x546530` multiplies an integer by `100000` to produce the ordinary fixed-point number.

`CCheckVariable`'s evaluation function `0x3D0BB0` obtains both raw 64-bit values through scoped-variable evaluator `0x544C90` and compares them directly, using signed inequalities or equality. It does not strip the state type tag or convert a state reference into a numeric ID. Raw Capstone confirms the signed comparisons.

For Danzig (state 85), the static consequences are:

| Value or comparison | Native representation / result |
|---|---|
| State reference 85 | `0xFFFFFFFFC0000055`, signed `-1073741739` |
| Numeric 85 | `8500000` |
| Typed state reference `> 0` | false |
| Typed state reference `< 100` | true, regardless of normal state-ID bucket |
| Typed state reference `= 85` | false |

Thus the pre-fix use of `rn_sel_state` both as a state reference and as the numeric operand in the generated VP lookup is inconsistent with native semantics. It explains the blank selected-state label/zero VP lookup path without requiring an input or rendering failure. This is a static explanation of the reported symptom, not a completed runtime regression test.

`CHasVariable` evaluator `0x3D9A30` performs existence lookup instead of a numeric comparison. It is appropriate for a deliberately present/absent state-reference variable, provided every deselection path uses `clear_variable`. Assigning numeric zero leaves a variable present, so replacing `> 0` with `has_variable` also requires auditing reset paths and validating the selected state before use.

## Converter status: unresolved, do not treat `THIS.id` as numeric

The proposed split is sound: preserve typed `rn_sel_state` for `var:` scope access and state arrays, and use a separate **ordinary numeric** `rn_sel_state_id` for ID comparisons, bucket dispatch, and ID-based localization.

The proposed assignment `ROOT.rn_sel_state_id = THIS.id` is **not confirmed to produce an ordinary numeric ID and should not be accepted as the converter**. Parser `0x546530` explicitly treats the component `id` as a special name (literal RVA `0x2716334`; parser branches around `0x54816B` and `0x548421`), skipping it rather than setting an evident numeric-conversion flag. `0x545C60` also classifies `id`, `this`, `from`, `root`, and `prev` as special names. The literal evaluation path still emits the tagged state value. This is strong static evidence that `.id` retains the scope-reference semantics; no isolated live evaluation was performed.

The local vanilla example `common/scripted_effects/SOV_scripted_effects.txt:8302` assigns `ROOT.temp_target_state = THIS.id` and then passes it to `build_railway.target_state`. That consumer accepts a state reference, so the example does not establish suitability for mathematical ID comparison.

A concrete fallback for the next session is to generate an ID lookup while inside `var:rn_sel_state`, assigning a numeric literal for the matching state:

```text
if = {
    limit = { state = 85 }
    ROOT = { set_variable = { rn_sel_state_id = 85 } }
}
```

Generate this from the existing state-ID inventory, clear/reset the numeric ID before selection, and preserve the typed state separately. A per-state numeric variable initialized from the same inventory is another implementation choice, with added saved state. Neither fallback was implemented or tested in this investigation. The generated dispatch should cover valid states without VPs too, so their empty Cities list can be distinguished from failed selection.

## Evidence and next validation

- [Targeted decompilation](evidence/session-20261011/typed-state-variable-decompile.txt): state resolver, literal encoding, variable evaluation, comparison, existence, assignment, and expression parser.
- [Raw Capstone](evidence/session-20261011/typed-state-variable-capstone.txt): tagged state encoding, signed comparisons, special-name handling, and aligned parser excerpts.
- [Function list](evidence/session-20261011/typed-state-variable-functions.txt) and [read-only Ghidra logs](evidence/session-20261011/typed-state-variable-ghidra.log).

Next session: select Danzig through the States list and the state-panel shortcut, verify the selected state's name and VP list, select a state from another numeric bucket, then a state without VPs. Confirm deselection clears both reference and numeric lookup state. Do not claim the candidate Cities fix passed until these paths are observed in the game.

## Follow-up, 2026-10-11 resumed session

Commit `cc9791d` implemented the literal dispatch above for all 1081 vanilla states,
including 126 without VPs, while retaining the typed state for ownership and scope
access. [Static mappings and negative fixtures](evidence/session-20261011/city-state-literal-mapping-checks.json)
passed; the old `THIS.id` converter was removed.

Independent fresh POL1936/PL QA then selected Gdańsk85, renamed VP362 to
“Nowy Gdańsk”, and observed the actual map label and native province panel. The
map helper also selected Gdynia807 and showed its three VPs, exercising another
numeric bucket. [City evidence](evidence/qa-20261011-day2/05-city-apply-map-nowy-gdansk.jpg),
[helper list](evidence/qa-20261011-day2/07-map-helper-cities-gdynia.jpg).
Evidence commit: `4f4c5ad`. This verifies the repaired selection and Apply paths;
zero-VP UI, complete guard coverage and final save/load remain separate checks.
