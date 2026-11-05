# Port to HOI4 1.19.4.2

Hotfix port of every CAPIE pin from HOI4 1.19.4.1 to 1.19.4.2 (CAPIE 1.6.0 → 1.6.1, branch
`hotfix-1.6.1` from tag `v1.6.0`). Method and tools as in [port-1.19.4.1.md](port-1.19.4.1.md)
and `tools/port/README.md`; the old → new map by pin id is `tools/port/map_1.19.4.2.json`.

- Old build: 1.19.4.1, `hoi4.exe` SHA-256 `CF781ED1…10476F`. Not on disk during the port (Steam
  had already replaced it); the 1.6.0 pins were the reference.
- New build: 1.19.4.2 (launcher "Operation Postern v1.19.4.2.2da5 (0bdb)"), `hoi4.exe` SHA-256
  `BCCC62909B6A1EC2C1D5A46EF18608E4D0C7CDC89AD341B7402A23C40F7A8037`. A private copy is kept
  outside the repository for the next port.
- Addresses are RVAs from image base `0x140000000`.

## Result

No structure layout changed: every instruction pin (the pins that prove struct offsets) has the
same bytes at its new address, at the same offset in its own function, and `off` of the map is
empty. Only RIP/rel32 fields differ. Code and data moved by uniform shifts:

| Region | Shift | Examples |
|---|---|---|
| `.text` below `0x1F2000` | 0 | `GetSession` `17F880`, `GetLocalHuman` `1F1A40`, `string_copy` `11F770`, caption `F7B47`/`F8915`, idler in-game getters |
| `.text` `0x26A000`..`0x23A0000` | +0x70 | console actions, name/flag/portrait/colour layers, window manager, caption chain, edit box, focus chain, session init |
| `.text` `0x241D000`..`0x2548000` | +0x140 | localisation lookups, console singleton/dispatch/lookup/add-or-replace, the token registrar `2547630` |
| `.rdata` | 0..+0xE80, growing with the address | `CGameApplication`, `CHuman`, `CColor` 0; `CCountryColors` +0x8; `CCountry` +0x20; scripted-GUI vtables and the politics tab token +0x30; `CInGameIdler` +0xA0; `CCharacter` +0x190; execute-effect +0x3F8; politics view/observer, `CScriptedWindowTemplate` +0x720; player-context token +0xE30; `CSession`, `CContainerWindow`, `CInstantTextBox` +0xE40; `CEditBox` family +0xE60; servers, console gate message +0xE80 |
| `.data` | 0 | gamestate, application, focus manager, sprite database, map singleton, colour defines, console slot, GUI allocator |

Triage (`port.py triage` on the 1.6.0 sources): 179 entries, 4 unchanged, 64 moved, 106 short
moved, 1 bytes changed (`focus.2379EC0`, RIP displacement only), 4 not found (below). All 115
anchors of `anchors.json` resolved uniquely on the new executable (69 moved, no `FAIL`, no
`NO ANCHOR`), and after the port `port.py locate --check` reports 115 resolved, 0 problems.

## How each pin was placed

Without the old executable no byte diff was possible, so every new value was derived from the
new file and cross-checked (scratch scripts `build_map.py`, pefile/capstone):

- Signatures of 16 bytes or more: the one hit in `.text` (all unique); function signatures start
  a `.pdata` function.
- Short instruction pins: `shortpins.py`, placed at the old address plus the shift of their own
  anchored function, distance 0 for every pin. Where a pattern occurs twice in its function
  (`refresh.map_manager`, `scoped_variable.name_capacity`, `update_names.tag_argument`,
  `idler.ingame_true`, `ui.out_buffer`) the pin keeps its exact offset. The portrait `ui.*` pins
  (`B964F7`, `B96538`, `B96548`, in `B964C0`), `set_cosmetic_tag.argc` (`299B70`) and
  `drop_cosmetic_tag.argc` (`26A7A4`) were placed by the shift of their function (+0x70).
- RIP/rel32 pins (49): decoded at old + shift; every target equals the new value of the named
  constant, or for literal targets the new function start / string (`continent.log_format` →
  "Getting continent for %d" `29F5BE8`, `dispatch.gate_message` → "Console not available in
  multiplayer or ironman mode." `2C04AF0`, the three rebuild builders `FB82C0`, `FBAEF0`,
  `FBA020`, `claims.calls_colour` → `FB47F0`, `cosmetic.calls_map` → `AB7EA0`).
- Named constants (86 `rva::`): `locate.py`. Host and console constants: RTTI and the RIP pins
  above; `idler_get_session` (`B8E400`) is `vt_ingame_idler` slot `0x88`, written by hand as in
  1.5.2.
- Vtables: RTTI (`rtti_exe_test` checks every class name). Slots re-read on the new file:
  politics view `0x28` → `12C4F10`, observer `0x38`/`0x48` → `161DF70`/`1620CC0`, idler `0xB8`
  → `E3A9A0`, edit box `0xC0`/`0xC8` → `23715D0`/`2371E00`, application `0x88` → `17F880`.
  Token values unchanged (`0x3AB2`, `0x3A70`, `0x3A6E`).

## Bytes changed (RIP fields only)

| Signature | Instruction | Old target | New target |
|---|---|---|---|
| `focus.2379F30` | `mov rcx,[rip]` | `3500FC0` | `3500FC0` (the thunk moved +0x70, the global did not) |
| `editor.set_text` | `movdqa xmm1,[rip]` | `2BB6D30` | `2BB7B70` (+0xE40, the `.rdata` shift there) |
| `caption.12C3BC0` | `lea rax,[rip]` (vtable) | `2A1F9D0` | `2A1FE50` (`CUpdateable`, +0x480) |
| `caption.F8915` | token DWORD, string `lea`, call | `29CB040`, `2B6C0B0`, `25474F0` | `29CB070` (value `0x3AB2`), `2B6CEE0` "politics_tab", `2547630` |
| `caption.F7B47` | token DWORD, string `lea`, call | `2B30230`, `2B6B6D0`, `25474F0` | `2B31060` (value `0x3A6E`), `2B6C500` "parent_window_token", `2547630` |

## Not found in triage

- `host.get_session`: a host constant with a short pattern; `CGameApplication` slot `0x88` is
  still `17F880` (`mov rax,[rcx+380h]; ret`).
- `rebuild.case_political` / `case_claims` / `case_continent`: entries of the 23-way map-mode
  jump table, whose values are code RVAs. The switch `cmp edi,16h; ja; lea rdx,[image];
  mov r9d,[rdx+rdi*4+0FB825Ch]` at `FB8009` gives the new table `FB825C` (+0x70); entries 0,
  19, 21 are `FB8027`, `FB8161`, `FB8185` (old + 0x70). Set by hand; `gen_tables.py` disassembles
  code and cannot regenerate table data.

## Other changes

- The 31 signature ids named after a 1.19.4.1 address that moved were renamed to the new address,
  as in the earlier ports (`caption.BC1700`, `focus.22BD4A0`, …); comments and descriptions that
  named a moved address follow (`check_comment_addresses.py`: 0 unpinned).
- `map_1.19.4.1.json` still listed six signatures under their 1.19.4.0 names; the new map uses the
  1.6.0 ids.
- Pattern text is kept byte for byte where the code bytes are unchanged (`gen_tables.py` would
  have dropped the trailing partial instruction of `colour.map_colors` and `colour.refresh`).
- Test-only literals: `CNullEditBox` vtable `2BC8F30` (RTTI), parent window token `2B31060`.
- New accepted table `tools/port/accepted-1.19.4.2.json`, pinned in `check_signature_table.py`.
  Its SHA-256 is that of the CRLF working-copy file (as for 1.19.4.1, with `core.autocrlf=true`).

## Multiple builds

Not supported by the DLL: `game_build.hpp` holds one `game_exe_sha256` ("the only accepted
`hoi4.exe`"), and `game_layout.hpp` and the pin tables hold one value per item. CAPIE 1.6.1
therefore supports 1.19.4.2 only; Steam updates the game automatically.

## Open

- In-game test on 1.19.4.2 (text fields, name, flag, portrait, colour, faction, caption refresh,
  focus release, console off); the offline and `*_exe_test` checks pass.
- Release pipeline: reviewed binary hashes, installer pins and Workshop texts are written at
  release time and were not changed here.
