# CAPIE console contract, API level 3 and 4 (candidate)

This is the console part of the CAPIE script contract. It lives in its own file while CAPIE 1.1
(API level 2, `CONTRACT.md`) is being revised; the two will be merged later. Everything not
mentioned here (reserved names, the alphabet, field errors, the SDK effects) is as in
`CONTRACT.md`.

Status, 2026-10-14: implemented in `src/features/console/` and compiled only into the developer
build (`build.ps1 -Console`, output `plugin/build/capie-console-dev`, for CAPIE 1.2
`plugin/build/capie-v12-dev`). Since CAPIE 1.2 (API level 4) the two name commands write the
country name layer of `CONTRACT.md` section 14 instead of an RN1 cosmetic tag. Release builds do not
contain it. Nothing here has run in the game yet; section 8 lists the open checks. The
evidence is in `docs/evidence/session-20261011/claude-console/` (`re-evidence.md`,
`design.md`).

## 1. Versions

| Name | Value | Where a mod sees it |
|---|---|---|
| API level | `4` in CAPIE 1.2 (`3` in the 1.1 candidate) | `temp_var:capie_cap` of a console command |
| Source | `2` | `temp_var:capie_src`: 2 = console command (editor Apply commands do not set it) |
| Build number | as `CONTRACT.md` | `temp_var:capie_ver` |

API level 3 added the console commands; API level 4 adds the country name layer
(`CONTRACT.md` section 14). Since CAPIE 1.2 console and editor commands send the same level
(`capie_cap = 4`). Compare with `>=`, never with `=`.

## 2. Commands CAPIE adds to the game console

| Command | Arguments | What runs |
|---|---|---|
| `change_nation_name` | `<TAG> "<name>" ["<adjective>"]` | The game's `eval_effect "<TAG> = { ... }"` that writes the name layer (codes into `capie_cn_n` / `capie_cn_a`, `capie_cn_ver = 1`, then `capie_cn_rev = 1`); the adjective defaults to the name. Flag and colour stay |
| `reset_nation_name` | `<TAG>` | The game's `eval_effect "<TAG> = { ... }"` that clears the layer (`clear_array` both, `clear_variable = capie_cn_ver`, then `capie_cn_rev = 1`). The country shows its cosmetic tag or base name again |

Both name commands first read the country's current cosmetic tag (read only). When it is a
legacy `RN1|...` tag of CAPIE 1.0/1.1, the effect also contains `drop_cosmetic_tag = yes`, which
migrates the country; a cosmetic tag of the game or another mod is never dropped. An unknown tag
prints the game's "Invalid tag for the command".
| `capie_cmd` | `<mod> <command> ["<arg>" ...]` | The game's `eval_effect` with the inline effect of section 4, which calls the mod's `capie_v1_cmd__<mod>__<command>` |

`help` lists the three commands with their arguments (in-game check pending).

Arguments are bare words or quoted strings. Inside quotes, `\"` is a quote and `\\` a
backslash. CAPIE rebuilds the line itself, because the game splits it at every space.

## 3. Declaring a mod command

A command is a scripted effect with this exact name. There is nothing else to register.

```text
capie_v1_cmd__<mod>__<command>          <mod>, <command>: [a-z0-9]{1,12}
```

- The mod id `capie` is reserved for the SDK.
- `<mod>` and `<command>` must be typed without quotes.
- The longest effect name is 39 bytes.
- Two mods cannot collide, because the mod id is part of the name. Use the same mod id as for
  your editors.

## 4. What the effect receives

CAPIE validates every argument and then runs this inline effect through the game's
`eval_effect`:

```text
FROM = {
    set_temp_variable = { capie_cap = 4 }      # 3 in the 1.1 candidate
    set_temp_variable = { capie_ver = <build> }
    set_temp_variable = { capie_src = 2 }
    set_temp_variable = { capie_nf = <n> }               # 0..4 arguments
    set_temp_variable = { capie_f<i>_err = 0 }           # for i = 1..n
    set_temp_variable = { capie_f<i>_at = 0 }
    set_temp_variable = { capie_f<i>_len = <length> }    # 0..32 characters
    set_temp_variable = { capie_f<i>_<k> = <code> }      # k = 0..length-1, alphabet codes
    capie_v1_cmd__<mod>__<command> = yes
}
```

| Item | Rule |
|---|---|
| Argument type | Text only, as character codes of the CAPIE alphabet (`CONTRACT.md` section 7, codes 1..264). Numbers are text too (`"12"` arrives as codes 54, 55) |
| Argument count | 0 to 4 |
| Argument length | 0 to 32 characters. `""` is a valid empty argument (`len = 0`) |
| Rejected before anything runs | Characters outside the alphabet, controls, markup (`" [ ] \| $ § £ \ { } # @`), leading or trailing spaces, two spaces in a row, invalid UTF-8, more than 4 arguments, a bad or quoted id |
| `capie_f<i>_err`, `_at` | Always 0: invalid text never reaches the effect |
| Codes past the length | Not set; HOI4 reads an unset temporary variable as 0 |
| THIS | The player's country |
| FROM | The player's country |
| ROOT | Whatever the player has selected in the game (a country, a state or a character), or nothing. Do not use it |

### Scope and permissions

The game's `eval_effect` sets ROOT to the selected object and FROM to the player's country
(`re-evidence.md` section 10). CAPIE cannot change ROOT, so it switches to FROM, and the
effect runs with THIS = the player's country. Therefore:

- Use THIS as the acting country.
- Do not call SDK effects or triggers that read ROOT: `capie_v1_begin`, `capie_v1_actor_ok`,
  `capie_v1_may_edit_country`, the `capie_v1_text_*` slot effects and `capie_v1_report`. They
  would act on the selected object.
- `capie_v1_field_load` and `capie_v1_text_trim` / `capie_v1_text_validate` work on temporary
  variables only and are safe.
- Check `capie_src = 2` and `capie_cap >= 3` first. Any script can call your effect without
  CAPIE, and the console is a cheat channel, so authority checks beyond "the console is open"
  are the mod's own choice.

## 5. Behaviour without CAPIE

The three commands do not exist, so the game answers `Unknown command`. A mod's
`capie_v1_cmd__*` effects are ordinary scripted effects. They parse without CAPIE and run only
if some script calls them.

## 6. Multiplayer and ironman

The game refuses its console in multiplayer and in ironman before it looks up any command:
"Console not available in multiplayer or ironman mode." CAPIE's commands are entries in the
same table, so the same check stops them. CAPIE has no other path to run them, and it never
reads, calls or bypasses that check.

A console command runs on one machine only. That is safe only because the console exists only
in single player.

## 7. Safety rules CAPIE follows

- CAPIE writes no game state itself. `change_nation_name` and `reset_nation_name` hand the work
  to the game's own `eval_effect`, so the change is an ordinary script effect. Both read the
  current cosmetic tag first, with the game's tag and country lookups (read only).
- CAPIE adds its commands once, from the game thread that runs the scripted-window Tick. It
  never replaces an existing command: if any name or alias already exists, it registers
  nothing.
- An action that arrives on a different thread from the one that registered the commands is
  refused and logged.
- If any of the console signatures or pins does not match the game build, the console commands
  stay off and the rest of CAPIE keeps working.

## 8. What needs an in-game test

| Item | Why |
|---|---|
| `change_nation_name` with Polish and Latin Extended letters, double spaces; save and reload; map and diplomacy names refresh; flag unchanged | Encoding of console text, the tokenizer's handling of empty tokens, refresh through `capie_cn_rev` |
| `reset_nation_name` on a layer name, on a legacy RN1 tag, on a country with a story cosmetic tag | Section 2 rule (the story tag and its flag stay) |
| The refusal in multiplayer and in ironman | Section 6 |
| `capie_cmd condemo echo` with nothing selected, with another country selected, with a state selected | ROOT, THIS and FROM as in section 4 (open item O5) |
| The `help` listing | Section 2 |
| Thread ids in `capie_editor.log` (`CAPIE_CONSOLE registration=... tick_thread=...`, `first action on thread ...`) | Tick and console must share one thread |
