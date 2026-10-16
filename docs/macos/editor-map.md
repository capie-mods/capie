# macOS editor, scripted-window, caption and focus map

Static analysis, 2026-10-16. **Not a runtime-ready pin table.** No game launch, hook,
write to game memory or Windows source change was performed. Unknowns are deliberately
kept unresolved; no Windows address or offset is assumed valid on macOS.

Target: HOI4 macOS x86_64, SHA-256 `09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`, image base `0x100000000`.
`rva.*` values below are image-relative; instruction citations and RTTI address points
are unslid VAs. `off.*` values are offsets, never RVAs.

Evidence method: local `docs/macos/re/macho.py` RTTI/pointer reader and a search dump, checked against fresh
llvm-objdump disassembly of the current binary. Retained focused instruction excerpts are in
[editor-evidence.txt](re/editor-evidence.txt); they include freshly decoded source-binary bytes for each
instruction. The dump is an analysis aid, not a signature validator. A production pin
requires instruction-boundary decoding, uniqueness check, and exact-build check.

## Findings that affect the port

The scripted-window chain has identifiable macOS counterparts, but several contracts
are structurally different. The model uses libc++ strings; parent name lengths cannot
be read unconditionally at the Windows offsets. The GUI updater stays at window+0x110,
while root/model/manager/idler and visibility-cache fields move.

The native dispatch `0x100b4b7e0` first applies its temporary records (32-byte stride,
24-byte libc++ name plus 8-byte value) through `0x100b03430`, and later constructs an
88-byte (`0x88`) command. Constructor arguments are `this, model-name, selector, scope,
record-vector` in `rdi,rsi,rdx,rcx,r8`. Constructor entry `0x10020b820` is a tail thunk to
`0x10020b2b0`. The effect execute virtual is +0x58. A constructor-only substitution
must be checked against the preceding dispatch-side record application and Windows
bridge semantics; this report does not prove that existing interception is equivalent.

CEditBox has confirmed base subobjects at +0x70 and +0x180. Its focus/unfocus functions
remain at slots +0xc0/+0xc8 in this binary: **do not shift every Windows slot by eight**.
Its text is a tagged libc++ UTF-32 string, not the MSVC inline/size/capacity layout.

The politics view observer at +0x28, request/dirty/consume chain and focus-switch
manager are identified. Detailed leader/party writer coverage and in-game frame timing
remain unchecked; static frame-loop scheduling is now proven in [core-pins.md](core-pins.md). No support/API-level claim follows from this map.

## Complete inventory for this scope

`static` means the listed fact is directly backed by local instructions or RTTI; it
does not mean tested in-game, safe to hook, or all ABI details established. `candidate`
is an explicit incomplete semantic association. Child-list capacity/data/count layout
must still be confirmed against actual root child collections.

| Source field | Status | macOS value | Evidence / limit |
|---|---|---|---|
| `rva.window_tick` | static | `0xB44020` | 0x100b44034 saves `esi` force flag; +0xf0 root, +0xa50 parent-visible and +0xa51 visible-trigger result; 0x100b443d5 updates embedded context. |
| `rva.window_bind` | static | `0xB45EC0` | 0x100b45ed4 loads model; resolves parent, creates root at 0x100b462a7; stores root at 0x100b462af and binds context at 0x100b463df. |
| `rva.window_dispatch` | static | `0xB4B7E0` | 0x100b480f7 calls with GUI updater and selector; 0x100b4b8ec resolves selector; 0x100b4bae5 constructs effect. |
| `rva.effect_ctor` | static | `0x20B820 (thunk), 0x20B2B0 (body)` | 0x100b4bae5 calls thunk; 0x10020b825 jumps to body; body writes RTTI-backed effect vptr at 0x10020b2f6. |
| `rva.effect_execute` | static | `0x20B830` | Effect vtable +0x58 points here; 0x10020b8a6 reads model +0x70; 0x10020b98f reads selector +0x58; 0x10020b9c4 executes resolved effect. |
| `rva.edit_key_handler` | static | `0x285E6D0` | CEditBox secondary vtable +0x38 points here; receives key event in rsi, uses key helpers; not a Windows-handler ABI substitute. |
| `rva.edit_key_wrapper` | static | `0x2793AD0` | CEditBox primary vtable +0x10; 0x102793b79 adjusts this by +0x70 and 0x102793b8f calls buffer slot +0x38. |
| `rva.find_edit` | static | `0x27722D0` | CContainerWindow virtual +0xa0; previewer_editbox caller 0x10254881f passes root,name,bool then 0x10254883a adjusts result+0x70. Body 0x1027722ea / 2f1 searches name array/root child list. This is a THREE-argument lookup; Windows four-argument recursion/boolean behavior remains unverified. 0x102772309 tests report-error bool; failure path logs at 0x10277240e, returns dummy [root+0x88]+0x1b8 at 0x102772470/477. Native identity proven; ABI needs macOS adapter and explicit missing-widget guard. |
| `rva.find_instant` | static | `0x27720F0` | CContainerWindow virtual +0x98; 0x10077aa63 resolves floating_number name, 0x10077aae1 passes result to CInstantTextBox string setter. Body uses root+0xe38 names and +0xe20 pointers. THREE-argument lookup; Windows fourth argument semantics unverified. 0x102772129 tests report-error bool; failure path logs at 0x10277222e, returns dummy [root+0x88]+0x1c0 at 0x102772290/297. Native identity proven; ABI needs macOS adapter and explicit missing-widget guard. |
| `rva.text_buffer_set` | static | `0x285E4A0` | 0x10285e4a4 tests libc++ source mode and tails to 0x10285e3e0; body clears buffer+0x30 UTF-32 string and inserts converted text. 0x1000dfeb9 passes edit+0x70, then refreshes. |
| `rva.edit_refresh` | static | `0x2793F10` | 0x1000dfec0 calls SetText with edit+0x70 and 0x1000dfece tails here with edit; wrapper 0x102793bd4 refreshes after key input. |
| `rva.request_view_refresh` | static | `0x20EE380` | 0x1020ee387 rejects sentinel 0x17; 0x1020ee392 indexes gui+0xc8; 0x1020ee3a2 checks visible slot +0x30; 0x1020ee3b1 adds observer+0x28; tails to dirty mark. |
| `rva.observer_mark_dirty` | static | `0x2389140` | 0x102389144 mov byte ptr [rdi+0x14],1. |
| `rva.observer_consume` | static | `0x2389150` | 0x10238915c invokes observer slot +0x50, 0x10238915f clears dirty byte +0x14. Runtime frame scheduling still unchecked. |
| `rva.idler_gui_getter` | static | `0x77BD00` | 0x10077bd04 loads [rdi+0x670]; scripted-window tick calls idler slot +0xd8 then GUI view getter 0x1020f2590. |
| `rva.view_is_visible` | static | `0x1FC8E30` | Politics primary vtable +0x30; 0x101fc8e39 loads +0x430 root, 0x101fc8e5d reads root+0x95 shown bit. |
| `rva.view_get_root` | static | `0x1FC8F30` | 0x101fc8f34 mov rax,[rdi+0x430]. |
| `rva.observer_frame` | static | `0x1FA4320 (thunk), 0x1FA3C10 (body)` | Politics observer address point +0x40; 0x101fa4324 adjusts -0x28 then jumps to 0x101fa3c10, which updates frame countdown using idler+0x18. |
| `rva.observer_refresh` | static | `0x1F9FC10 (thunk), 0x1F9D6C0 (body)` | Politics observer +0x50; 0x101f9fc14 adjusts -0x28 and tails to 0x101f9d6c0. Consumer 0x10238915c calls this slot. Detailed leader/party writer coverage not yet traced. |
| `rva.focus_switch` | static | `0x27C86C0` | 0x1027c86cd loads old focus +0x138, 0x1027c86dc calls unfocus +0xc8, 0x1027c86e2 stores new focus, 0x1027c86f8 calls focus +0xc0. |
| `rva.edit_unfocus` | static | `0x2794BD0` | 0x102794bd9 reads +0x239; 0x102794be2 clears bit 0; 0x102794c43 adjusts +0x180 and tails to receiver unregistration. |
| `rva.edit_focus` | static | `0x2794AF0` | 0x102794afb reads +0x239, rejects bit 1; 0x102794b0d sets bit 0; 0x102794ba0 passes +0x180 to receiver registration. |
| `rva.focus_manager_rip_displacement` | static | `0x2863EED` | Displacement bytes of LEA at 0x102863eea, unlike Windows MOV; dereference at 0x102863ef1. Adapter must understand two-step access. |
| `rva.focus_manager_rip_next` | static | `0x2863EF1` | Next RIP of LEA at 0x102863eea. |
| `rva.vt_sg_model` | static | `0x3326868` | RTTI CScriptedWindowTemplate: typeinfo 0x1033268b8; address point 0x103326868, offset-to-top 0.  |
| `rva.vt_sg_context` | static | `0x33269b0` | RTTI CScriptedWindowGUIUpdater: typeinfo 0x103326a18; address point 0x1033269b0, offset-to-top 0.  |
| `rva.vt_sg_root` | static | `0x34AC0B0` | 0x100b46308 dynamic_cast target typeinfo 0x1034ac6a0 names 16CContainerWindow; RTTI primary address point 0x1034ac0b0. Constructor 0x1027664c0 / 4c7 writes it. |
| `rva.vt_instant_text` | static | `0x34b1768` | RTTI CInstantTextBox: typeinfo 0x1034b1a10; address point 0x1034b1768, offset-to-top 0.  |
| `rva.vt_edit` | static | `0x34add98` | RTTI CEditBox: typeinfo 0x1034ae0f8; address point 0x1034add98, offset-to-top 0.  |
| `rva.vt_text_buffer` | static | `0x34ae058` | RTTI CEditBox: typeinfo 0x1034ae0f8; address point 0x1034ae058, offset-to-top -112.  |
| `rva.vt_edit_definition` | static | `0x34AE3E0` | RTTI CEditBoxType; edit name accessor 0x102842ec4 loads definition+0x50, then virtual +0x50 -> 0x1027cee90 (name+0x28). |
| `rva.vt_execute_effect` | static | `0x32bcb18` | RTTI CExecuteScriptedWindowEffect: typeinfo 0x1032be8f0; address point 0x1032bcb18, offset-to-top 0.  |
| `rva.vt_politics_view` | static | `0x3444508` | RTTI CCountryPoliticsView: typeinfo 0x103444658; address point 0x103444508, offset-to-top 0.  |
| `rva.vt_politics_observer` | static | `0x3444600` | RTTI CCountryPoliticsView address point 0x103444600 top=-40; 0x1020ee3b1 uses view+0x28 with observer dirty marker. |
| `rva.vt_ingame_idler` | static | `0x3308f90` | RTTI CInGameIdler: typeinfo 0x103309410; address point 0x103308f90, offset-to-top 0.  |
| `rva.manager_global` | static | `0x35DC058` | 0x102863eea lea rax,[rip+0xd78167], next RIP 0x102863ef1 => 0x1035dc058; 0x102863ef1 loads manager. Corroborated 0x102863f07 and focus-switch caller 0x1000e40b8. |
| `rva.token_politics_tab` | static | `0x35AF598` | 0x1024d72b9 LEA destination, 0x1024d72c0 string politics_tab, 0x1024d72ca token 0x3ab2, constructor 0x102a7a1c6 writes DWORD [this]. Runtime-initialized, not readonly constant. |
| `rva.token_player_context` | static | `0x35AE4B8` | 0x1024d6c65 LEA destination, 0x1024d6c6c string player_context, 0x1024d6c76 token 0x3a70, constructor 0x102a7a1c6 writes DWORD [this]. Runtime-initialized. |
| `rva.gui_structural` | static | `0x3505780` | 0x1020e84bd loads GOT slot 0x10329c9c8, whose on-disk pointer is allocator object 0x103505780 (__ZZ24PdxGetNewDeleteAllocatorvE5Alloc). GUI+0xb8 stores object address, not GOT slot address; diagnostic only. |
| `off.window_idler` | static | `0xD8` | 0x100b45de9 constructor stores idler; 0x100b44175 loads it and invokes GUI getter. |
| `off.window_model` | static | `0xE0` | 0x100b45df0 constructor store; 0x100b47d34 loads model and returns model+8 name. |
| `off.window_manager` | static | `0xE8` | 0x100b45df7 constructor store; bind 0x100b45ee7 reads token-map owner. |
| `off.window_root` | static | `0xF0` | 0x100b462af stores created widget; 0x100b46624 getter. |
| `off.window_parent` | static | `0xF8` | 0x100b46023 stores token lookup result; 0x100b44078 reads visibility flags. |
| `off.window_parent_sg` | static | `0x100` | 0x100b46123 stores matching scripted parent; 0x100b46150 loads its root. |
| `off.window_context` | static | `0x110` | 0x100b45dfe / 0x100b45e25 constructs GUI updater; 0x100b443cb updates it. |
| `off.window_parent_seen` | static | `0xA50` | 0x100b441f1 compares and 0x100b4421e stores computed parent-visible state. |
| `off.window_desired` | static | `0xA51` | 0x100b44425 invokes model trigger; 0x100b44307 stores result. |
| `off.context_window` | static | `0x10` | 0x100b45e20 supplies window as rdx; 0x100b47dc8 stores at +0x10. |
| `off.context_model` | static | `0x20` | 0x100b47f79 loads window+0xe0; 0x100b47f94 stores model at context+0x20. |
| `off.context_root` | static | `0x28` | 0x100b463d7 stores window+0x138 = context+0x28; 0x100b46634 setter. |
| `off.context_scope` | static | `0x3C0` | 0x100b4b7f8 forms scope passed as rcx to effect constructor at 0x100b4bae1. |
| `off.context_records` | static | `0x470` | 0x100b4b873 reads records data; 0x100b4bacf forms constructor r8 record-vector argument. |
| `off.context_extent` | static | `0x470` | Derived maximum of currently retained context fields; validate readable span separately. |
| `off.model_name` | static | `0x8` | 0x100b47d40 tests libc++ mode at model+8, short data +9 / long pointer +0x18. |
| `off.model_context_token` | static | `0x20` | 0x100b4420e compares to 0x3a70; 0x100b46739 switches context token. |
| `off.model_parent_token` | static | `0x24` | 0x100b45edb token-map lookup key. |
| `off.model_parent_window_len` | requires_adapter | `not a fixed unconditional length field` | String at model+0x40; 0x100b4622c reads short tag/length, 0x100b462d7 reads long length +0x48. Windows direct length read invalid. |
| `off.model_parent_sg_len` | requires_adapter | `not a fixed unconditional length field` | String at model+0x58; 0x100b4602b short mode, 0x100b4603c long length +0x60. |
| `off.root_flags` | static | `0x95` | 0x100b44092 tests bit 2; 0x100b443bc sets bit 4; 0x101fc8e5d reads bit 3. |
| `off.root_instant_list` | static | `0xE20` | 0x102772111 supplies pointer vector; instant consumer 0x10077aa63 / 0x10077aae1. |
| `off.root_edit_list` | static | `0xE80` | 0x1027722f1 supplies pointer vector; 0x10276d7f3 / 7f6 index selected pointer after matching names at +0xe98. |
| `off.root_third_list` | unresolved | `UNRESOLVED` | No macOS equivalent established in this pass. Windows number must not be used. |
| `off.root_readable_span` | unresolved | `UNRESOLVED` | No macOS equivalent established in this pass. Windows number must not be used. |
| `off.list_data` | static | `0x0` | 0x10020b30f reads input records data from [r8]. |
| `off.list_capacity` | static | `0x8` | Scope array begins +0x40, capacity loaded at 0x10020b591 from +0x48; child-list-specific verification still required. |
| `off.list_count` | static | `0xC` | 0x10020b312 reads input record count [r8+0xc]. |
| `off.edit_definition` | static | `0x50` | 0x102842ec4 loads definition, 0x102842ecc invokes name getter slot +0x50. |
| `off.edit_visibility_flags` | static | `0x65` | 0x102793b5b tests shown bit 3; 0x100b46f50 clears hidden bit 4 on widget. |
| `off.edit_buffer` | static | `0x70` | RTTI secondary top=-112; 0x102793b79 forms buffer this pointer. |
| `off.edit_text` | static | `0xA0 (libc++ UTF-32 object)` | 0x10285e4e3 tests buffer+0x30 mode, short data buffer+0x34, long pointer +0x40. Edit buffer +0x70 => string object +0xa0. Size/capacity are tagged, not MSVC fields. |
| `off.edit_size` | requires_adapter | `libc++ tagged size; no single field` | 0x102793c4f short/long test at edit+0xa0; long size edit+0xa8 at 0x102793c73; short size encoded byte>>1. Requires new reader. |
| `off.edit_capacity` | requires_adapter | `libc++ tagged capacity; no MSVC field` | 0x102793ccf..cd3 ORs capacity with 1 before storing first qword; inline/long layout must use libc++ reader. |
| `off.edit_receiver` | static | `0x180` | RTTI third top=-384; 0x102794d94 adjusts -0x180; focus 0x102794ba0 and unfocus 0x102794c43. |
| `off.edit_flags` | static | `0x239` | 0x102794afb / 0x102794b0f focused bit 0, disabled bit 1. |
| `off.definition_name` | static | `0x28` | CEditBoxType vtable +0x50 => 0x1027cee90; 0x1027cee94 lea rax,[rdi+0x28]. libc++ string. |
| `off.instant_text` | static | `0x100` | 0x1027d0930 forms destination; 0x1027d094c..958 moves 24-byte string; 0x1027d096c forwards to text renderer. Getter 0x1027d0914. |
| `off.manager_focus` | static | `0x138` | 0x1027c86cd read; 0x1027c86e2 write. |
| `off.manager_receiver` | static | `0x210` | 0x1027c8f39 registers pointer passed by edit+0x180; 0x1027c8f81 clears it; 0x1027c6439 dispatches text event to it. |
| `off.manager_idler` | static | `0x18` | RTTI CScriptedWindowManager constructor 0x100b429c0 saves rsi at 0x100b429d1 and stores +0x18 at 0x100b429fc; vptr 0x1033268e0 installed 0x100b429f9. |
| `off.key_event_words` | static | `8` | Diagnostic prefix length, not event sizeof: key base constructor 0x10269b1b7 and 0x10269b1bb initializes two 16-byte blocks at +0x10/+0. Next allocator field is +0x20 at 0x10269b1c5. First 32 bytes contain a dynamic-array header; do not interpret all eight words as Windows event payload. |
| `off.key_wrapper_type` | static | `0x74` | 0x10269b154 loads type DWORD; wrapper 0x102793b74 compares to 1. |
| `off.command_records` | static | `0x28` | 0x10020b2f9 destination vector; 0x10020b30f / 312 source data/count. |
| `off.command_scope_capacity` | static | `0x48` | 0x10020b591 loads capacity; 0x10020b600 updates it. |
| `off.command_scope_count` | static | `0x4C` | 0x10020b584 reads count; 0x10020b649 writes it. |
| `off.command_selector` | static | `0x58` | 0x10020b334 destination string; execute 0x10020b98f passes it to effect lookup. |
| `off.command_model` | static | `0x70` | 0x10020b3b1 destination string; execute 0x10020b8a6 passes to scripted-window database. |
| `off.effect_records` | static | `0x28` | Same constructed command; execute 0x10020b91e loads +0x28 records. |
| `off.idler_gui` | static | `0x670` | 0x10077bd04 getter and 0x100b46782 idler slot +0xd8 consumer. |
| `off.gui_idler` | static | `0x180` | 0x1020ee932 loads pointer; 0x1020ee946 assertion names _Idler.CanUpdateGui(). |
| `off.gui_views` | static | `0xC8` | 0x1020f25f0 loads [rdi+8*rax+0xc8]. |
| `off.gui_structural` | static | `0xB8` | GUI constructor loads allocator GOT at 0x1020e84bd and stores +0xb8 at 0x1020e8508; 0x1020ef2eb loads it and 0x1020ef302 invokes allocator virtual+0x10. |
| `off.view_observer` | static | `0x28` | 0x1020ee3b1 adjustment from view to dirty marker; RTTI offset-to-top=-40. |
| `off.view_gui` | static | `0x30` | Politics constructor rdx=GUI at 0x1020e939f. Common ctor 0x101fc88fa saves rdx, 0x101fc892f forms view+0x28, 0x101fc8937 passes GUI; observer ctor 0x10238865e stores +0x8 => view+0x30. |
| `off.view_dirty` | static | `0x3C` | Observer+0x14, established by 0x102389144 and view observer+0x28. |
| `off.view_idler` | static | `0x428` | Politics ctor 0x101f9824b calls common ctor with idler rsi; 0x101fc88fd saves rsi, 0x101fc8aa9 stores at +0x428. |
| `off.view_root` | static | `0x430` | 0x101fc8e39 and 0x101fc8e56 politics visibility root loads. |
| `off.slot_view_visible` | static | `0x30` | RTTI politics primary address point +0x30 = 0x101fc8e30. |
| `off.slot_observer_frame` | static | `0x40` | RTTI politics observer +0x40 points to 0x101fa4320 adjustment thunk. |
| `off.slot_observer_refresh` | static | `0x50` | 0x10238915c invocation; RTTI politics observer slot points to 0x101f9fc10. |
| `off.slot_idler_gui` | static | `0xD8` | RTTI CInGameIdler +0xd8 = 0x10077bd00; consumer 0x100b4417f. |
| `off.slot_edit_focus` | static | `0xC0` | RTTI CEditBox primary +0xc0 = 0x102794af0. |
| `off.slot_edit_unfocus` | static | `0xC8` | RTTI CEditBox primary +0xc8 = 0x102794bd0. |
| `off.parent_flags` | static | `0x95` | 0x100b44084 reads parent DWORD +0x94, tests 0x400/0x800, corresponding byte +0x95 bits 2/3. |
| `value.token_politics_tab` | static | `0x3AB2` | 0x1024d72ca token registry init associated with politics_tab string. |
| `value.token_player_context` | static | `0x3A70` | 0x1024d6c76 token registry init associated with player_context string. |
| `value.politics_view_index` | static | `12` | 0x1020e93a2 constructs RTTI-backed politics view; 0x1020e93a7 stores gui+0x128. (0x128-0xc8)/8=12. |
| `value.gui_view_limit` | static | `0x17` | 0x1020ee387 exact sentinel check in refresh request; it is not a full range guard. |
| `value.key_enter` | static | `0xD` | 0x10285e7cb loads key constant from 0x102ef654c (DWORD 13), 0x10285e7d9 matches event first word; successful branch reaches newline insertion 0x10285e89a / 8b3 or submit observers. |
| `value.wrapper_type_key` | static | `1` | 0x102793b74 compare after type accessor 0x10269b150 |
| `value.inline_utf32_capacity` | static | `4` | 0x102793c81 compares source length to 4; copy uses inline branch through 0x102793cdb. This differs from Windows capacity=3. |

## Remaining proof and in-game checks

1. Resolve the remaining third-child-list identity and derive a macOS readable-span guard;
   implement the three-argument lookup and missing-widget adapters; establish byte patterns and all
   platform-specific ABI adapters before activating editor features.
2. Validate model/root/context identity on an actual scripted window, short and long
   libc++ names and UTF-32 text, visibility and parent rebinding.
3. Check click-to-effect temporary-variable behavior, Enter handling, text receiver
   ownership and one focus release per politics hide; exercise repeated open/close.
4. Check leader/party captions after rename, view hiding/showing and save reload.
5. Prove Windows builds/tests unchanged separately. None of these runtime checks were
   performed by this static mapping pass.

## Root guard decision: replace the anonymous Windows guard

The remaining Windows fields are **policy**, not two missing engine ABI fields.
`window_view.hpp::inspect_chain` and `debug_observer.hpp` use `root_third_list`
only as a third `bounded_list` check. No reader, lookup or renderer uses its contents.
The Windows source calls it merely “Third child list”; its element type is unproven.
Consequently no macOS list is assigned to that legacy identifier by offset arithmetic.

Use a macOS-specific guard of the four arrays actually used by both typed lookups:

| Array | Header | Element stride | Direct evidence |
|---|---:|---:|---|
| Instant pointers | `0xe20` | 8 | `0x102772111`, helper `0x10276d613/616` |
| Instant lowercase names | `0xe38` | 24 | `0x10277210a`, helper `0x10276d599–0x10276d606` |
| Edit pointers | `0xe80` | 8 | `0x1027722f1`, helper `0x10276d7f3/7f6` |
| Edit lowercase names | `0xe98` | 24 | `0x1027722ea`, helper `0x10276d779–0x10276d7e6` |

Each header is 24 bytes: data+0, capacity+8 (signed 32-bit), count+12
(signed 32-bit), allocator+16. Root constructor `0x1027668e7–0x102766934`
initializes these headers; paired insertion `0x10276b813–0x10276ba23`
reads/writes edit capacity at e88/ea0 and count at e8c/ea4.
A generic `bounded_list` using pointer stride **must not** validate name arrays:
those contain 24-byte libc++ strings, not pointers.

The replacement guard must bound each signed count/capacity, require matching
name/pointer counts for each pair, check multiplication/address overflow, and copy
all `count*stride` bytes through guarded memory reads. A local traversal can then
compare the stored lowercase names with CAPIE's known lowercase ASCII widget names
and validate returned widget vtables. This avoids native lookup's temporary-buffer
allocation and non-null dummy return. It does not establish safe concurrent access;
perform lookup on the game's GUI thread and do not retain snapshots across callbacks.

**Derived policy span: `0xeb0`**, the end of the last header `0xe98 + 24`.
It is sufficient for these four headers and earlier root identity/flags, not the
size of CContainerWindow and not a promise that any native virtual call is safe.
Prefer checking the actual ranges rather than copying this whole prefix. The two
legacy rows remain unresolved as *one-to-one ABI mappings*, but no longer block a
new guard implementation: replace their use, do not inject invented equivalents.

## Concrete editor instruction pins

[editor-pins.json](editor-pins.json) contains all 11 editor signature roles and an
additional constructor-body pin, decoded afresh from the exact SHA-256 binary.
Bytes are unmasked and end at instruction boundaries; each entry includes address,
decoded instructions, length and occurrence count across the complete file.
Ten original roles and the constructor body have unique signatures. The canonical
constructor thunk is only **10 bytes**, and its bytes occur **eight times**. Validate
its exact RVA `0x20b820`, relative branch destination `0x10020b2b0`, and the companion
body pin together. Never select this thunk by unconstrained pattern search and never
overwrite 14 bytes at its entry. These signatures validate identity; they do not
prove trampoline relocation, ABI compatibility, vtable pins or in-game behavior.
