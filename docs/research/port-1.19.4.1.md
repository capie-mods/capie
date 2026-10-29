# Port to HOI4 1.19.4.1

Hotfix port of every CAPIE pin from HOI4 1.19.4.0 to 1.19.4.1 (CAPIE 1.5.1 → 1.5.2). Method and
tools as in [port-1.19.4.0.md](port-1.19.4.0.md) and `tools/port/README.md`; the old → new map
by pin id is `tools/port/map_1.19.4.1.json`.

- Old build: 1.19.4.0, `hoi4.exe` SHA-256 `B6650C85…D888` (not on disk during the port; the
  1.19.4.0 pins were the reference).
- New build: 1.19.4.1, `hoi4.exe` SHA-256
  `CF781ED178CD801643B233B7E3FCD1C0EFD19BD01AECD24BF07A35F69F10476F`.
- Addresses are RVAs from image base `0x140000000`.

## Result

No structure layout changed. Every instruction pin (the pins that prove struct offsets) has the
same bytes at its new address inside its own function; the signatures differ only in RIP and
rel32 fields. Code and data moved by small shifts:

| Region | Shift | Examples |
|---|---|---|
| `.text` below `0x12CF000` | 0 | name layer, flag atlas, window manager, caption chain, console actions, `GetSession` |
| `.text` `0x1438000`..`0x167FFFF` | +0x3C0 | portrait resolve/valid, flag candidates, `GetMachineId`, politics observer slots, token parser |
| `.text` around `0x19A6000`..`0x1A42000` | +0x2B0 | `GetApplication` getter, `USER_NAME` tooltip |
| `.text` around `0x1D63000` | +0x310 | lobby `is_server` check |
| `.text` from `0x22A0000` | +0x350 | focus chain, edit box, `FindEdit`/`FindInstantText`, session init, localisation, console |
| `.rdata` | +0x10..+0x30 | vtables +0x10/+0x20, `player_context` and `parent_window_token` DWORDs +0x30, gate message +0x20 |
| `.data` | 0 | gamestate, application, focus manager, sprite database, console slot, GUI allocator |

Triage (`port.py triage` on the 1.5.1 sources): 150 entries, 37 unchanged, 29 moved, 82 short
moved, 1 bytes changed, 1 not found. All 95 anchors of the main checkout's `locate.py` resolved
and agree with the result (35 of the layout RVAs moved).

## Bytes changed: `focus.2379B70` (no layout change)

| | 1.19.4.0 (`2379B70`) | 1.19.4.1 (`2379EC0`) |
|---|---|---|
| `mov rdx,rcx` | `488bd1` | same |
| `mov rcx,[rip+disp]` | disp `0x1187446` | disp `0x11870F6` |
| `jmp manager_release` | `e9f135f4ff` | same |

The displacement shrank by exactly 0x350 because the thunk moved and the focus manager global
did not (`3500FC0` in both builds); the `jmp` is unchanged because the thunk and
`manager_release` (`22BD4C0`) both moved by 0x350. The other signatures whose bytes changed are
the same kind: `editor.set_text` (RIP constant), `caption.12C3B50` (vtable `lea` +0x10),
`caption.BB6085` (call to the politics view ctor `1617CC0`), `caption.F8915` / `caption.F7B47`
(string `lea` and token DWORD +0x30, call `25474F0`).

## Hard pins

| Pin | 1.19.4.0 | 1.19.4.1 | How |
|---|---|---|---|
| `host.get_session` | `17F880` | `17F880` | Triage "not found" only because the RVA is a host constant and the pattern is short. `CGameApplication` (RTTI vtable `2796308`, unchanged) slot `0x88` → `17F880`: `mov rax,[rcx+380h]; ret`; `GetLocalHuman` `1F1A49` loads `app_global` and calls `[rax+88h]`. |
| `human.name` | `1A413EB` | `1A4169B` | `USER_NAME` (`2A59CF8`) is loaded at `1A41720` in function `1A414F0`; the only `lea rdx,[rbx+20h]` in that function is `1A4169B`, followed by `call string_copy` (`11F770`). |
| `lobby.session_is_server` | `1D635BF` | `1D638CF` | 29 global hits. The two old sites (`1D635BF`, `1D641E3`, 0xC24 apart) are `1D638CF` and `1D644F3`: same spacing, same `call [rax+88h]; cmp byte [rax+3Ch],0`. |
| `app.global` | `19A5F73` | `19A6223` | The only `mov rax,[rip→33DDB08]; ret` is the getter `19A6220`. |
| `caption.163B7BD` | `163B7BD` | `163BB7D` | Neighbouring anchors give 0 and +0x3C0; only +0x3C0 matches, once in its function (`163B4F0`). |
| `effect.writes_portrait` target | `143ADA0` | `143B160` | rel32 at `2FDCC0` (code there did not move). |
| `dispatch.gate_message` target | `2C03C50` | `2C03C70` | the string "Console not available in multiplayer or ironman mode." |
| parent window token (test only) | `2B30200` | `2B30230` | `mov edx,[rip]` of `caption.F7B47`; value still `0x3A6E` |
| `CNullEditBox` vtable (test only) | `2BC80B0` | `2BC80D0` | RTTI |

Short instruction pins: each one was placed at its old address plus the shift of its own function
(from `.pdata` boundaries and the anchored signatures) and had to match there; none was taken
from a global unique hit. Where a pattern occurs several times in its function
(`country.variables`/`variables2`, `update_names.tag_argument`,
`scoped_variable.name_capacity`, `ui.out_buffer`, `argv.count`, `result.message`) the pin
keeps its exact offset in the function. All 29 RIP/rel32 pins decode to the expected target.

Vtables (RTTI): `CSession` `2BB4568`, `CProxyServer` `2BCBA28`, `CNetworkServer` `2BCB330`,
`CDummyServer` `2BCB040`, `CEditBox` `2BBF088` (+0x80 `2BBF328`, +0x1A0 unchanged offset),
`CInstantTextBox` `2BBB588`, `CContainerWindow` `2BBA000`, `CInGameIdler` `29E5838`,
`CCharacter` `29FAF40`; `CGameApplication` `2796308` and `CHuman` `279EDB8` unchanged. Slots
checked: politics view `0x28` → `12C4EA0`, observer `0x38`/`0x48` → `161DF00`/`1620C50`, idler
`0xB8`/`0x88` → `E3A930`/`B8E390`, edit box `0xC0`/`0xC8` → `2371560`/`2371D90`. Token values
unchanged (`0x3AB2`, `0x3A70`, `0x3A6E`).

The six signature ids named after a 1.19.4.0 address were renamed to the new one, as in the
previous port: `caption.163BB7D`, `focus.22BD430`, `focus.2371D90`, `focus.2371E10`,
`focus.2379EC0`, `focus.22BD4C0`.

## Open

- In-game test on 1.19.4.1 (text fields, name, flag, portrait, faction, caption refresh, focus
  release); the offline and `*_exe_test` checks pass.
- Release pipeline: reviewed binary hashes, installer pins and Workshop texts are written at
  release time and were not changed here.

## main

`main` (CAPIE 1.5.3, development line) has pins that 1.5.1 did not have. The hotfix port was
taken over unchanged (cherry-pick of the port commit); `tools/port/map_1.19.4.1.json` gained
the entries below, and `port.py --map map_1.19.4.1.json generate` / `apply` on main's 1.19.4.0
sources reproduces the result (only the line wrapping of long signatures differs, not kept).
None of the main-only items moved: they lie in `.text` below `0x12CF000` (shift 0) and in
`.rdata` next to the unchanged `vt_sg_context`. Checked with pefile/capstone on the new
executable (Ghidra holds the 1.19.4.0 file only).

| Item | 1.19.4.1 | How |
|---|---|---|
| `vt_sg_manager` | `29CB368` | RTTI `.?AVCScriptedWindowManager@@`; slot `0x38` → `BDB970` (the per-frame Tick loop, anchor `window_manager_frame`) |
| `window_manager_rebuild` | `BDBAB0` | anchor: the only function referencing "Window %s not found." (`.pdata` `BDBAB0`..`BDC10D`) |
| `window_context_ctor` | `BD7D40` | anchor: the callee of the rebuild that references `vt_sg_context`; also the rel32 target of the call at `BDBEE6` |
| `tick.window_desired` | `BDB7E4` | `mov [rbx+BF9],al`; inside Tick (`BDB500`..`BDB96C`), the only byte store to `+BF9` there |
| `window_ctor.*` (8 stores) | `BDBEAA`..`BDBEEC` | same bytes, in the rebuild after the window vtable stores (`lea` → `29CB328`, `vt_scripted_window`): `+E0`, `+E8`, `+F0`, zero `+F8`/`+100`/`+108`, `lea rcx,[rbx+110]`, `mov word [rbx+BF8],1` |
| `window_ctor.context_ctor_call` / `bind_call` | `BDBEE7` / `BDBF0E` | rel32 → `BD7D40` / `BD8ED0` (`window_bind`) |

The RTTI table (`core/rtti.hpp`) names its vtables through `rva::` constants, so it follows the
header; `rtti_exe_test` checks every class name on the new executable. `vt_sg_model`
(`CScriptedWindowTemplate`) is the hotfix value `2A543B8` (+0x20). The Tick hot-path cache, the
destroyed flag (`flag::destroyed`, bit 2 of `root_flags`) and the scoped frames add no address;
`window_force_refresh` (`+BFA`, stored at `BDBEF5` in the rebuild) is unchanged. All 95 anchors
resolve and `port.py locate --check` passes on 1.19.4.1.

## API level 8

CAPIE 1.6.0 (branch `api8-layers`) adds the country colour layer
(`features/country/colour_layer_pins.hpp`) and the ruling-party ideology name layer
(`features/country/subideology_layer_pins.hpp`), written against 1.19.4.0 and rebased on this
port. Every entry they add was re-checked on the 1.19.4.1 executable with pefile/capstone (Ghidra
holds the 1.19.4.0 file only): each signature is unique in `.text` and starts its `.pdata`
function; each instruction pin was placed at its old address plus the shift of its own function
and matches there, once in that function, at the same offset from the function start; each
RIP/rel32 pin decodes to its target; the two slot pins read the expected function from
`vt_ingame_idler` (`29E5838`, already ported); the RTTI tables name the expected class at offset 0.
`tools/port/map_1.19.4.1.json` lists all of them; the main signature table did not change, so
`accepted-1.19.4.1.json` is unchanged. `port.py locate --check`: 118 entries, 0 problems.

Colour layer: nothing moved. The code lies in `.text` below `0x12CF000` (shift 0), the defines
and globals in `.data` (shift 0), and the three vtables kept their `.rdata` addresses.

| Item | 1.19.4.0 → 1.19.4.1 | Check |
|---|---|---|
| `country_map_colors`, `refresh_country_color` | `734AE0`, `AB7F10` (same) | signatures unique, function starts |
| instruction pins (14) | same RVAs | `getter.block_offset` in `734AE0`, `colors.layout` `759340` (unique), cosmetic guard in `759370`, refresh in `AB7F10`, `mark.*` in `E86C10`, `update.*` in `FBF5A0`, `define.*_target` in `752D00` |
| target pins (11) | same RVAs | `759519` → `AB7E30`, `759525` → `AB7F10`, `AB7F7D` → `734AE0`, `759502` → `33DDB08`, `AB7E33` → `33E7818`, six define loads → `33E5130`..`33E542C` |
| `idler.ingame_slot` | `vt_ingame_idler` + `0x2D8` → `188D90` | slot read |
| `vt_country_colors`, `vt_color`, `vt_country` | `279F558`, `2796208`, `2864710` (same) | RTTI |

Ideology name layer: the panel writer is in the `+0x3C0` region, the formatter in the `+0x350`
region, `CPoliticalParty`'s vtable moved +0x10. The localising helper `66A4E0`, the party and
ideology group constructors (`1230290`, `121D920`) and the idler slot `B8E270` did not move.

| Item | 1.19.4.0 | 1.19.4.1 | Check |
|---|---|---|---|
| `politics_panel` (signature) | `1622B90` | `1622F50` | unique; anchor `politics_panel` |
| `panel.idler_tag` | `1622BBA` | `1622F7A` | `mov rcx,[rcx+5F0]; call [rax+A0]`, once in `1622F50`..`1623325` |
| `panel.ruling_party` | `1622BE0` | `1622FA0` | `mov r14,[rax+D0]`, once in the function |
| `panel.ideology_object` | `1622C65` | `1623025` | `mov rcx,[r14+18]`, once in the function |
| `panel.calls_politics` | `1622BD9` | `1622F99` | → `717370` |
| `panel.calls_localise` | `1622C6A` | `162302A` | → `66A4E0` |
| `panel.calls_first_role` | `1622C7D` | `162303D` | → `1230500` |
| `localise.calls_formatter` target | `22AE6F0` | `22AEA40` | rel32 at `66A501` (unchanged); `22AEA40` calls the formatter `22AE310` |
| `formatter.calls_lookup` | `22AE0FA` | `22AE44A` | in `loc_formatter` (`22AE310`, anchor "Reached max recursive depth") at the same offset `+0x13A`; → `loc_lookup_n` `241F030` |
| `vt_political_party` | `2A1A250` | `2A1A260` | RTTI `.?AVCPoliticalParty@@` |
| `vt_ideology_group` | `29BA258` | `29BA258` | RTTI |
| `ideology.key`, `party.ctor_ideology`, `ideology_group.ctor_key`, `idler.shown_tag` | `66A4E6`, `12302BC`, `121D992`, `B8E373` | same | bytes, once in their functions; `B8E36F` → `gamestate_global` `33DD6B0` |
| `idler.shown_tag_slot` | `vt_ingame_idler` + `0xA0` → `B8E270` | same target | slot read |

The `flip` literals of `colour_layer_pins_exe_test` and `subideology_layer_pins_exe_test` follow
the table, including `vt_ingame_idler` `29E5838`. Both tests, `rtti_exe_test` and every other
`*_exe_test` pass on 1.19.4.1.
