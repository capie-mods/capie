# Port to HOI4 1.19.4.0

Working notes of the port of every CAPIE pin from HOI4 1.19.3.0 to 1.19.4.0. Updated as the port
goes; the status column says what is settled.

- Old build: 1.19.3.0, `hoi4.exe` SHA-256 `7DC947BE…3A031D5` (not available on disk during the port).
- New build: 1.19.4.0 ("Operation Postern v1.19.4.0.48d6 (a783)"), `hoi4.exe` SHA-256
  `B6650C85002374218E60598BD04AAA26CBAD996FEDCE77126FCF92FC6BD6D888`, 56 529 528 bytes.
- Addresses are RVAs from image base `0x140000000`. Names are analyst labels or MSVC RTTI names.

## Method

The old executable was not available, so the old pins themselves were the only reference.

1. Every pattern in `signature_table.hpp` and `features/**/*pins*.hpp` (112 entries) was searched in
   the new `.text`, first exactly, then with rel32 branch targets and RIP displacements masked
   (capstone decodes the pattern to find those fields).
2. Patterns without a unique hit were matched approximately in a window around the address
   predicted from the nearest uniquely matched functions; the differing bytes were disassembled
   old against new. A difference in a displacement means a layout change, not just moved code.
3. Short instruction pins (2 to 8 bytes) are never accepted on a global unique hit: two such hits
   (`session.is_server`, `session.mode`) landed megabytes away from their function. They are
   accepted only inside the function they belong to.
4. Vtables were found through MSVC RTTI (type descriptor, complete object locator, vtable), and
   their owners through the constructors that store them.

REA with Ghidra 12.1.4 was used; the first whole-program analysis of the 56 MB image exceeded
REA's default 330 s deadline (`REA_GHIDRA_STARTUP_TIMEOUT_MS`), so most of the work below rests on
capstone decoding of the new image, with every claim tied to an instruction address.

First pass: 55 of 112 patterns unique and unchanged, 16 placed by a local delta, 41 needed analysis.

## Layout changes (observed)

| Field | 1.19.3.0 | 1.19.4.0 | Evidence (new) |
|---|---|---|---|
| `CEditBox` flags `edit_flags` | `+0x271` | `+0x281` | `2371A40` edit unfocus: `movzx eax,[rcx+0x281]`, `and al,0FEh`, store back |
| bit tested after unfocus clear | `0x10` | `0x20` | `2371A40`: `test al,20h` (flag bits moved too; bit 1 "disabled" must be re-derived) |
| politics view idler `view_idler` | `+0x570` | `+0x5F0` | `12C4CDF` view base ctor: `mov [rsi+5F0h],r14` (param 2) |
| politics view root `view_root` | `+0x578` | `+0x5F8` | `12C4CE6`: `mov [rsi+5F8h],rbx`; `12C4E70` GetRoot: `mov rax,[rcx+5F8h]; ret` |
| scripted window parent-visible cache `window_parent_seen` | `+0xB78` | `+0xBF8` | `BDB6AF` tick: `cmp cl,[rbx+0BF8h]`; ctor `BDBEEC`: `mov word [rbx+0BF8h],1` |
| scripted window desired byte `window_desired` | `+0xB79` | `+0xBF9` | same word store at `BDBEEC`; tick `BDB7E4`: `mov [rbx+0BF9h],al`, its only store (pinned, `window_pins.hpp`) |
| session mode | `+0x4C` | `+0x34` | session init `22C379A`: `mov [rcx+34h],r8d` |
| session machine id | `+0xA4` | `+0x84` | `22C37A2`: `mov dword [rcx+84h],1` |

Unchanged (observed): scripted window `+0xE0` idler, `+0xE8` model, `+0xF0` manager, `+0xF8` root,
`+0x100` parent, `+0x108` parent scripted GUI, `+0x110` context (window construction inside the manager rebuild `BDBAB0`, `BDBEAA..BDBEE6`,
pinned store by store in `plugin/capie/src/features/editor/window_pins.hpp`; the rebuild allocates
`0xC10` bytes, the test fixtures use `0xC00`, which covers every offset CAPIE reads);
context `+0x10` window, `+0x20`, `+0x28`, `+0x3C0` scope, `+0x470` records (context ctor
`BD7D40`); politics view observer at `+0x30`, observer `+0x8` GUI, `+0x14` dirty (`12C3B50`);
`CEditBox` subobjects at `+0x80` (text buffer) and `+0x1A0` (receiver) (RTTI offsets); model
offsets `+0x28`, `+0x2C`, `+0x60`, `+0x80` (caption pins at `BD8EF6..BD90A1` match unchanged).

## GUI container: typed name maps replace child lists

In 1.19.3.0 `FindEdit` and `FindInstantText` walked child lists at root `+0x1240` and `+0x11E0`.
In 1.19.4.0 `CContainerWindow` (vtable `2BB9FE0`) keeps one name hash map per widget type,
0x38 bytes apart from `+0x12A0` to `+0x15E8`, plus a vector of all children at `+0x1508`
(data `+0`, capacity `+8`, count `+0xC`, the old list format). Container ctor `23292A0` initialises
them in order.

- Map lookup helper `23319D0`: key is `{const char* ptr; int32 len}`, FNV-1a over lower-cased
  bytes, bucket mask int at map `+0x2C`, entries at map `+0x20`, entry stride `0x30`, probe byte `+4`.
- Ten `FindX` functions share one template (`2332040` … `2336270`): look up own map, else recurse
  into the children vector, else log `Could not find "%s" in window %s` when both bool arguments
  are set, and return a **null-object widget** `[[root+0x98]+slot]` (not `nullptr`).
- Which map holds which type, from the child-clone helpers called per map in `233A3A0`:
  - `+0x13F0` → clone helper `238DB60` calls `CreateEditBox` `23E61C0` (→ ctor `23704E0`): **CEditBox**.
  - `+0x1380` → clone helper `238DC60` calls `CreateInstantTextBox` `2377FA0` (→ ctor `2346F90`): **CInstantTextBox**.

Consequences for CAPIE:

| Item | 1.19.3.0 | 1.19.4.0 |
|---|---|---|
| `find_edit` | `22BAD40` | `23331C0` (map `+0x13F0`, null object slot `+0x1B8`) |
| `find_instant` | `22BC600` | `2334D40` (map `+0x1380`, null object slot `+0x1C0`) |
| name argument | `const MsvcString*` | `const NameView*` = `{const char*, int32}` (16 bytes; `movups xmm0,[rdx]`) |
| not found | `nullptr` | null object: `CNullEditBox` (vtable `2BC80B0`) for edits |
| `root_edit_list` / `root_instant_list` / `root_third_list` sanity checks | bounded lists | gone; check the children vector `+0x1508` and the two maps instead |

## Vtables (RTTI)

| Constant | Class | 1.19.3.0 | 1.19.4.0 | Status |
|---|---|---|---|---|
| `vt_edit` | `CEditBox` | `2B4A940` | `2BBF068` | RTTI |
| `vt_text_buffer` | `CEditBox` @ `+0x80` | `2B4ABD0` | `2BBF308` | RTTI |
| `vt_instant_text` | `CInstantTextBox` | `2B468C8` | `2BBB568` | RTTI |
| `vt_execute_effect` | `CExecuteScriptedWindowEffect` | `2996ED0` | `2A12ED0` | RTTI |
| `vt_politics_view` | `CCountryPoliticsView` | `29D9648` | `2A524D0` | RTTI; slot `0x28` → `12C4EA0` (view visible) |
| `vt_politics_observer` | `CCountryPoliticsView` @ `+0x30` | `29D96F0` | `2A52580` | RTTI; slot `0x38` → `161DB40`, `0x48` → `1620890` |
| `vt_ingame_idler` | `CInGameIdler` | `2968FF0` | `29E5828` | RTTI; slot `0xB8` → `E3A930` (= idler GUI getter) |
| `vt_character` | `CCharacter` | `297EA60` | `29FAF30` | RTTI |
| `vt_sg_context` | `CScriptedWindowGUIUpdater` | `294E568` | `29CB310` | stored by context ctor `BD7D67` at window `+0x110` |
| `vt_sg_model` | `CScriptedWindowTemplate` | `29DB558` | `2A54398` | class inferred (position and role); OPEN: runtime check |
| `vt_sg_root` | `CContainerWindow` | `2B45278` | `2BB9FE0` | class inferred (relative order with instant/edit vtables); OPEN: runtime check |
| `vt_edit_definition` | `CEditBoxType` | `2B54050` | `2BC6978` | class inferred; OPEN: runtime check |

`CEditBox` gained two virtual methods (primary-to-`+0x80` vtable distance `0x290` → `0x2A0`), but
after slot `0xE8`: focus slot `0xC0` → `2371210` and unfocus slot `0xC8` → `2371A40` are unchanged.

## Functions found

| Constant | 1.19.3.0 | 1.19.4.0 | How |
|---|---|---|---|
| `loc_lookup_n` / `loc_lookup_z` / `loc_exists` | `239EE20` / `239ED40` / `239D0F0` | `241ECE0` / `241EC00` / `241CFB0` | pattern unique |
| `update_names` / `build_name_key` | `716580` / `6ECFF0` | `75E660` / `734B60` | pattern unique |
| `set_variable_execute` | `364C60` | `37D1C0` | pattern unique |
| `variable_find` / `variable_get` | `BC4960` / `BC7E80` | `C1A2A0` / `C1D7C0` | pattern unique |
| `faction_get_name` / `string_copy` | `D8BB80` / `11E010` | `DF6370` / `11F770` | pattern unique |
| `load_country_flag` / `flag_atlas_update` / `build_flag_candidates` | `123F7E0` / `123FE20` / `147D990` | `12CD970` / `12CDFB0` / `1518DE0` | pattern unique |
| `portrait_resolve` / `portrait_valid` | `139DF20` / `139D1E0` | `1439B80` / `1438E40` | pattern unique |
| `window_bind` / `window_tick` / `window_dispatch` | `B86320` / `B88950` / `B867D0` | `BD8ED0` / `BDB500` / `BD9380` | pattern unique |
| `edit_key_handler` / `text_buffer_set` | `22F1030` / `22F27C0` | `236E2A0` / `236FB60` | masked pattern unique |
| `effect_execute` | `1156950` | `11E0700` | masked pattern unique |
| `effect_ctor` | `1144840` | `11CE6B0` | 41/42 bytes; register allocation differs (`mov r13,rcx`), arguments same |
| `request_view_refresh` | `B6E210` | `BC1690` | masked pattern unique |
| `observer_mark_dirty` / `observer_consume` | `1236950` / `1236980` | `12C4200` / `12C4230` | pattern unique |
| `idler_gui_getter` | `DCCF30` | `E3A930` | pattern + idler slot `0xB8` |
| `view_is_visible` / `view_get_root` | `1237520` / `12374F0` | `12C4EA0` / `12C4E70` | body with `+0x5F8`; view slot `0x28` |
| `edit_unfocus` / `edit_focus` | `22F4770` / `22F3FB0` | `2371A40` / `2371210` | `CEditBox` slots `0xC8` / `0xC0` |
| view base ctor | `1237180` | `12C4AD0` | registers differ; this=`rsi`, p2=`r14`, p3=`r15`, p4=`rdi` |
| politics view GetView | — | `BBA820` | token `3AB2` site `BDD0C6..BDD0DD`: `GetRoot(GetView(gui,12))` unchanged |

### Editor, caption and focus (second pass)

| Constant / pin | 1.19.3.0 | 1.19.4.0 | Evidence |
|---|---|---|---|
| `edit_key_wrapper` | `22F4580` | `23717E0` | new prologue `push rbp/rsi/rdi; sub rsp,20h; lea rdi,[rcx+281h]; mov rbp,rdx; test byte [rdi],2; jne` (bit 1 = disabled, unchanged) |
| `edit_refresh` | `22F4D80` | `2372230` | new prologue; same `cmp qword [rcx+1B0h],0` guard |
| `edit_visibility_flags` | `+0x75` bit 3 | unchanged | `2371E30`: `test byte [rcx+75h],8` |
| edit text / size / capacity | `+0xB0/+0xC0/+0xC8` | unchanged | `text_buffer_set` `236FB70`: text at buffer `+0x30` (= edit `+0xB0`); still takes `MsvcString*` |
| unfocus tail | `22F47F0` | `2371AC0` | `lea rcx,[rbx+1A0h]; …; jmp 2379B70` |
| receiver release thunk | `22FBEC0` | `2379B70` | `mov rdx,rcx; mov rcx,[rip→3500FC0]; jmp 22BD170` |
| RIP pin displacement / next | `22FBEC6` / `22FBECA` | `2379B76` / `2379B7A` | target `manager_global` |
| `manager_global` | `3453230` | `3500FC0` | RIP target of the thunk |
| `focus_switch` | `225E320` | `22BD0E0` | `mov rcx,[rcx+138h]; …call [rax+0C8h]; mov [rdi+138h],rbx` |
| manager release | `225E3B0` | `22BD170` | `cmp qword [rcx+210h],0; …; mov qword [rbx+210h],0` |
| `manager_focus` | `+0x158` | `+0x138` | focus switch |
| `manager_receiver` | `+0x230` | `+0x210` | manager release |
| caption manager | `B8A1C0` | `BDCDB0` | `mov rsi,rcx; mov rcx,[rcx+18h]`; token map at `rsi+70h` (`BDD0F4`) |
| token `3AB2` → GetRoot(GetView(gui,12)) | `B8A660` | `BDD0C6..BDD0F8` | `mov edx,0Ch; call BBA820; call 12C4E70; mov dword [rbp+18h],3AB2h` |
| `token_politics_tab` DWORD | `294E4CC` | `29CB040` (= `0x3AB2`) | registration `F8915`: `mov edx,[rip→29CB040]` … `lea r8,"politics_tab"` |
| `token_player_context` DWORD | `2AECB3C` | `2B3025C` (= `0x3A70`) | registration `F7BB9` … `"player_context"` |
| parent window token DWORD | `2AECAE8` | `2B30200` (= `0x3A6E`) | registration `F7B47` … `"parent_window_token"` |
| token values | `3AB2`, `3A70`, `3A6E` | unchanged | |
| gui `+0x180` idler, `+0x128` politics view, `+0xC8` views, limit `0x17` | — | unchanged | masked caption pins `BB6085`, `BC1690` |
| `gui_structural` (diagnostic only) | `3085170` | `30FD000` | GUI ctor `BB48D0`: `lea rdi,[rip→30FD000]; mov [rsi+0B8h],rdi`, then `mov [rsi+180h],r12` (idler) |

Caution: the token registration sites share one template; a masked pattern matched hundreds of
them (`exile_army_leaders`, `ai_check`). They are pinned by their string literal now.

### Country name, flag and portrait layers

All instruction pins keep their bytes (the offsets they prove are unchanged: country variables
`+0x218`, scoped variable name `+0x70/+0x80/+0x88`, game state countries `+0x310/+0x31C`,
faction members `+0x58/+0x64`, cosmetic tag `+0x1488`, flag atlas `+0xC0/+0xCC`, character id
`+0xC`, character variables `+0xD8`, politics `+0xF90`, ruling party `+0xD0`, party leaders
`+0x70/+0x7C`, portrait entry `+4/+8`). Only their addresses moved.

| Item | 1.19.3.0 | 1.19.4.0 | Evidence |
|---|---|---|---|
| `country_of` | `BB4390` | `C07620` | 4 call sites `e8 …; mov rbx,[rax+218h]`; old pair distance `0x2C7` = new |
| `gamestate_global` | `332F260` | `33DD6B0` | 27 sites `mov rax,[rip]; mov rbx,[rax+310h]; movsxd rax,[rax+31Ch]` |
| `country.variables` / `variables2` | `54265B` / `542922` | `589A8B` / `589D52` | after the two `country_of` calls |
| `scoped_variable.name_*` | `545029..545033` | `58C463..58C46D` | the three consecutive instructions |
| `country_of.countries` / `.gamestate` | `BB44A7` / `BB448D` | `C07737` / `C0771D` | inside `country_of` (`mov rdi,[rip→33DD6B0]`) |
| `faction.leader_first_member` | `4ABB81` | `4C9568` | two identical sites (`4C9568`, `4C9801`); both prove `[f+58]→[0]+8` |
| `tag.index_is_int` | `BB54A3` | `C08733` | `C08720` tag→index, same `+0x1113` from `country_of` |
| portrait UI pins | `B444C7..B44530` | `B96487..B964F0` | `call portrait_resolve` (`B964E0`) then `call portrait_valid` (`B964EB`); delta `+0x51FC0` |
| leader effect pins | `2E56B3..2E5744` | `2FDC33..2FDCC4` | `call 717370; mov rax,[rax+0D0h]`; delta `+0x18580` |
| `effect.writes_portrait` target | `139F140` | `143ADA0` | rel32 at `2FDCC0` |
| flag call targets | `123F85C`, `1240A71`, `123D77C` | `12CD9EC`, `12CEC01`, `12CB87F` | rel32 to `build_flag_candidates` / `load_country_flag` |
| character pins | `FA5127..FA688B` | `102964B..102ADAB` | 4 unique hits with one delta (`+0x84520`) |

### Host probe (debug flavor only)

| Item | 1.19.3.0 | 1.19.4.0 | Evidence |
|---|---|---|---|
| `app_global` | `3452450` | `33DDB08` | `GetLocalHuman` `1F1A49` loads it and calls slot `0x88` (= `CGameApplication::GetSession` `17F880`); getter `19A5F70` (`mov rax,[rip]; ret`) is the new RIP pin |
| `vt_game_application` / `vt_session` / `vt_human` | `27182E0` / `2B3E9D8` / `2720DA8` | `2796308` / `2BB4548` / `279EDB8` | RTTI |
| backends proxy / network / dummy | `2B53598` / `2B53020` / `2B52C28` | `2BCBA08` / `2BCB310` / `2BCB020` | RTTI |
| `GetMachineId` | `B5418F` | `148D5EF` | `CC 8B 81 84 00 00 00 C3`, unique |
| session `is_server` / `mode` / `backend` / `machine_id` | `+54/+4C/+58/+A4` | `+3C/+34/+40/+84` | `CSession::Init` `22C3760` |
| `lobby.session_is_server` | `1CD5694` | `1D635BF` | two identical sites (`1D635BF`, `1D641E3`) |
| `human.name` | `19A94BB` | `1A413EB` | `lea rdx,[rbx+20h]` before the `USER_NAME` key |
| idler session slot `0x88` | `B3C4A0` | `B8E390` | `CInGameIdler` vtable |

### Console (developer builds)

All 13 signatures are unique in the new image (`console.country_of` is `C07620`,
`restore_device_objects` `2B9D00`), all 25 instruction pins sit at the same offset inside their
function, and the 11 RIP/rel32 pins resolve to the expected targets: console singleton
`35E1A80` → `368F540`, sprite database `3453090` → `3500ED0`, `sprite_find` `2238DF0` →
`22A0B80`, gate message string `2B91060` → `2C03C50` ("Console not available in multiplayer or
ironman mode.").

## Tables and tests

- `tools/port/gen_tables.py` rebuilt every pattern from the new image: the same number of whole
  instructions, the same fields masked, extended until unique; all 77 signatures are unique in
  `.text` and free of DIR64 relocations. `tools/port/apply.py` wrote them into the sources;
  `tools/port/retest.py` moved the address literals of the tests.
- `plugin/capie/tests/check_signature_table.py` now compares the table with
  `tools/port/accepted-1.19.4.0.json` (pinned by SHA-256) instead of the 1.19.3.0 prototype
  sources.
- Code changes, not just numbers: `engine::NameView` for the widget lookups; `guarded_find`
  rejects a result whose vtable is not the type looked up (the null-object widget); the root
  sanity checks use the children vector and the two name maps (`engine::bounded_map`) instead
  of the removed child lists; test fixtures got the new sizes (window `0xC00`, edit `0x290`,
  root `0x17D8`, fake image `0x3502000`) and empty name maps.
- Result on 1.19.4.0: `plugin/capie/build.ps1` (release and debug flavors, all offline tests,
  every `*_exe_test` against the real `hoi4.exe`) and the `-Console` build pass.

## Open

- **In-game test** of every feature on 1.19.4.0 (text fields, name, flag, portrait, faction,
  caption refresh, focus release). `window_model` (`+0xE8`) and `window_desired` (`+0xBF9`) are
  no longer runtime-only: the game's own stores are pinned (`features/editor/window_pins.hpp`,
  checked at editor start and by `tests/window_pins_exe_test.cpp`; scan N10). `vt_sg_model`,
  `vt_sg_root` and `vt_edit_definition` are checked at runtime by `core/rtti.hpp`.
- The release pipeline (`build_launcher.ps1`, `build_proxy.ps1`, `deploy/DEPLOYMENT.md`) bundles
  reviewed binaries by SHA-256; new binaries must be built and reviewed for 1.5.1.

## Ghidra verification (full analysis, 2026-10-27)

| Item | Verdict | Evidence |
|---|---|---|
| Effect ctor copies CAPIE's record vector (H2) | confirmed | `11CE6B0` → `11EADE0(this+0x28, count, data, count)` → `BDA9D0` copies each 0x28-byte record with `11F770` (std::string copy ctor) and the value qword; no pointer into the caller's buffer survives. Dispatch evaluates selector triggers against the game's own `context+0x470` records before the ctor. |
| Portrait entry layout (H3) | confirmed | entry 0x30 bytes at `[rsp+30h]` of `B96450`; `+0` set byte, `+4` kind, `+8` string, `+0x28` non-owning texture handle used only for kind 1; the caller frees only the string. `PortraitEntryValid` asserts `_ThreadForbidCount == 0`: call it only inside the hook (UI thread). |
| `vt_sg_model` / `vt_sg_root` / `vt_edit_definition` (M1) | confirmed | template ctor `1639990`; the window construction inlined in `CScriptedWindowManager::Rebuild` `BDBAB0` (not a constructor, see Ghidra review G2) stores the template at `+0xE8` (`BDBEB1`) and the manager at `+0xF0`; Bind `BD8ED0` fills `+0xF8` from a factory that returns the PARENT on failure; CEditBox base ctor `231B8B0` stores the definition at `+0x60`. Also checked at runtime by `core/rtti.hpp`. |
| `window_desired` `+0xBF9` | confirmed | Tick `BDB500` is its only writer: parent visible (`+0xBF8`) AND the model's visible trigger AND a valid scope. Tick also clears `+0xF8`/`+0x100` when the root or parent is destroyed (root `+0xA5` bit 4). |

Hook points: localisation wrappers, UpdateNames, BuildNameKey, flag atlas, ResolvePortrait and the
scripted-window hooks are already the narrowest useful points. `SetVariableEffect::Execute`
(`37D1C0`) has no direct caller, only vtable slot `27F2FF0` (vtable `27F2F88` + 0x68): patching that
slot is exact and atomic and could replace the inline detour. Pre-hashed localisation lookups
(`241D010`) bypass the lookup hooks; harmless as long as nothing pre-hashes `RN1|`/`CAPIE_` keys.

Follow-ups: refuse a root equal to the parent (`+0x100`) in the window guards (done, tested since
scan H2); pin the `+0xBF9` store in Tick and the `+0xE8` store at `BDBEB1` (done, scan N10);
consider the vtable-slot hook for SetVariable.
