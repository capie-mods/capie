---
description: Console commands for CAPIE mods. Declare a scripted effect and players can run it from the in-game console with text arguments, in single player.
---

# Console commands

!!! proposed "Developer build only, not yet tested in the game"

    Console commands are API level 3, a candidate. They are compiled only into the CAPIE
    developer build. The full rules are in `plugin/capie/CONTRACT-console.md`.

With CAPIE, the in-game console gains three commands:

```text
change_nation_name GER "Neues Reich" "neureichisch"
reset_nation_name GER
capie_cmd <mod> <command> ["<argument>" ...]
```

The first two rename a country the way Rename Anything does, and undo that rename. The third
runs a scripted effect from your mod.

## Declare a command

Add a scripted effect named after your mod id and a command id, both 1 to 12 characters from
`a-z` and `0-9`:

```text title="common/scripted_effects/mymod_console.txt"
capie_v1_cmd__mymod__hello = {
	if = {
		limit = {
			check_variable = { temp_var:capie_src = 2 }
			check_variable = { temp_var:capie_cap > 2 }
		}
		log = "mymod hello from [THIS.GetTag], [?capie_nf] arguments"
	}
}
```

The player types `capie_cmd mymod hello "first" "second"`.

## What your effect gets

- THIS is the player's country. CAPIE runs your effect inside `FROM = { ... }` because the
  game's `eval_effect` sets FROM to the player.
- ROOT is whatever the player has selected on the map, or nothing. Do not use it, and do
  not call SDK effects that read ROOT (`capie_v1_begin`, `capie_v1_actor_ok`,
  `capie_v1_may_edit_country`, the text slot effects).
- Arguments arrive like editor fields: `capie_nf` (0 to 4), then for each argument
  `capie_f<n>_len` (0 to 32) and the character codes `capie_f<n>_0` and up. Load one with
  `capie_v1_field_load`, then trim and validate it with the SDK text tools if you need to.
- CAPIE rejects bad text before your effect runs: characters outside the alphabet, markup
  characters, leading, trailing or double spaces, more than 4 arguments.

## Without CAPIE, in multiplayer and in ironman

Without CAPIE the game answers `Unknown command`, and your effect is never called by the
console.

The game refuses its console in multiplayer and in ironman before it looks at any command, so
CAPIE's commands are refused there too. A console command runs on one machine; that is why it
is single player only.

## Example mod

`sdk/capie-script-sdk/examples/capie_console_demo` has two commands: `capie_cmd condemo echo`,
which logs which country is ROOT, THIS and FROM, and `capie_cmd condemo mark`, which sets a
flag on the player's country.
