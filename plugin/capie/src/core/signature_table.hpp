/**
 * @file signature_table.hpp
 * @brief All byte signatures, vtable slot pins, token pins and RIP pins of the pinned build, as data.
 *
 * One table per consumer. `capie_core.dll` checks `country_signatures`; `capie_editor.dll`
 * checks `editor_signatures`, `caption_signatures` (+ caption slot and token pins) and
 * `focus_signatures` (+ focus slot pins and the manager RIP pin). Initialization refuses on
 * the first mismatch, with the code of the group (20, 21 or 22).
 *
 * Every entry was copied byte-for-byte from the accepted sources (country 18841A6D...,
 * caption bridge rev7 3864532C...). `tests/check_signature_table.py` re-extracts the
 * accepted values from those sources and fails on any difference in RVA, length or bytes;
 * `signature_exe_test.exe` checks every entry against the EXE file.
 *
 * Evidence rules (RE findings 1.1 and 6.1): each run starts and ends on an instruction
 * boundary, is free of DIR64 relocations (so it also matches the ASLR-relocated image),
 * and has exactly one match in `.text`. Country patterns mask the 4-byte RIP displacement.
 * Fragility per entry is listed in RE findings 6.1; anchors for a future scanner in 6.2.
 *
 * Data only; nothing here reads memory.
 */
#pragma once
#include "game_layout.hpp"
#include "signatures.hpp"

namespace capie::core {

/// @brief Localisation hooks of `capie_core.dll` (3 entries, RIP displacement masked).
inline constexpr Signature country_signatures[] = {
    {"country.lookup_n", SignatureGroup::country_hook, rva::loc_lookup_n, "488b05????????49b825232284e49cf2cb",
     "lookup(key,len): mov rax,[rip]; movabs r8,FNV basis"},
    {"country.lookup_z", SignatureGroup::country_hook, rva::loc_lookup_z,
     "488b05????????48ba25232284e49cf2cb4c8b000fb60184c0742a49b9b301000000010000",
     "lookup(key): FNV basis, NUL loop, FNV prime"},
    {"country.exists", SignatureGroup::country_hook, rva::loc_exists, "4883ec28488b05????????48ba25232284e49cf2cb",
     "exists(key): frame, mov rax,[rip], FNV basis"},
};

/// @brief Hook targets and called functions of `capie_editor.dll` (11 entries, whole instructions, unmasked).
inline constexpr Signature editor_signatures[] = {
    {"editor.bind", SignatureGroup::editor_hook, rva::window_bind,
     "488bc44889581048896818488970205741544155415641574883ec50488bf9c7400800000000488db1e80000004c8b2e418b452c",
     "Bind void(window*): fresh-create and rebind callers"},
    {"editor.tick", SignatureGroup::editor_hook, rva::window_tick,
     "48895c2410555657415641574881ec20010000440fb6f2488bd9488d512833ff4885c9480f44d741b902000000",
     "Tick void(window*,char)"},
    {"editor.key", SignatureGroup::editor_hook, rva::edit_key_handler,
     "48895c240848896c241048897424205741544155415641574883ec40488bda488bf9488bcae85686f4ff",
     "text-buffer key handler (edit+80 subobject)"},
    {"editor.find_edit", SignatureGroup::editor_callee, rva::find_edit,
     "48895c241048896c241856574154415641574883ec60450fb6e1450fb6f04c8bfa488be94881c1f01300000f10020f29442430",
     "FindEdit: list displacements +1240/+1258 (twin of find_instant)"},
    {"editor.find_instant", SignatureGroup::editor_callee, rva::find_instant,
     "48895c241048896c241856574154415641574883ec60450fb6e1450fb6f04c8bfa488be94881c180130000",
     "FindInstantText: list displacement +11E0"},
    {"editor.set_text", SignatureGroup::editor_callee, rva::text_buffer_set,
     "48837a180f7603488b12e901000000cc48895c2410574883ec40488bd90f57c00f11442420660f6f0d83718400",
     "SetText SSO thunk (capacity > 15 -> heap pointer)"},
    {"editor.refresh_edit", SignatureGroup::editor_callee, rva::edit_refresh,
     "48895c2410488974241855574156488d6c24b94881ecd0000000488bf9c74567000000004883b9b0010000000f84950300004883e980e885bfffff",
     "void(CEditBox*) refresh; complete 55-byte prefix ends at 22F4DB7"},
    {"editor.wrapper", SignatureGroup::editor_hook, rva::edit_key_wrapper,
     "405556574883ec20488db981020000488beaf60702488bf10f85b6010000488bca48895c2448e8c51cf7ff",
     "CEditBox key wrapper: edit+281 bit1 disabled test"},
    {"editor.dispatch", SignatureGroup::editor_hook, rva::window_dispatch,
     "48895c241048896c2418488974242057415641574883ec604c8bfa488bf9488d99c00300004533c0",
     "Dispatch(context*, selector*): context+3C0 scope"},
    {"editor.ctor", SignatureGroup::editor_hook, rva::effect_ctor,
     "48895c241848894c240855565741544155415641574883ec600f297424504d8bf1498bf8488bf24c8be9",
     "CExecuteScriptedWindowEffect ctor"},
    {"editor.execute", SignatureGroup::editor_hook, rva::effect_execute,
     "48895c240848896c2410488974241857415641574883ec604c8bf1e8c03892ff498d5678488bc8e854a592ff4c8bf84885c0",
     "Execute void(command*): vtable 2A12ED0 slot 50, whole-instruction 50-byte prefix"},
};

/// @brief Caption refresh chain (25 entries; caption core evidence, RE 82CA2D2D).
inline constexpr Signature caption_signatures[] = {
    {"caption.E3A930", SignatureGroup::caption, 0xE3A930, "488b81b8060000c3",
     "idler slot B8 getter returns [idler+6B8]"},
    {"caption.BC1690", SignatureGroup::caption, 0xBC1690,
     "4883ec2883fa1774404863c248895c2420488d1cc1488b8bc80000004885c97423488b01ff502884c07419488b8bc80000004883c130488b5c24204883c428e92c2b7000488b5c24204883c428c3",
     "void(gui,int) index!=23, gui+C8+8i, view slot28, tail 12C4200(view+30)"},
    {"caption.12C4200", SignatureGroup::caption, 0x12C4200, "c6411401c3", "observer+14=1 (view+44 dirty)"},
    {"caption.12C4230", SignatureGroup::caption, 0x12C4230, "40534883ec20488b01488bd9ff5048c64314004883c4205bc3",
     "consume: observer slot48 then +14=0"},
    {"caption.12C4EA0", SignatureGroup::caption, 0x12C4EA0,
     "40534883ec20488bd9488b89f80500004885c97426488b01ff904802000084c07519488b83f80500000fb680a5000000c0e80324014883c4205bc3",
     "politics view visibility (slot28) reads view+5F8"},
    {"caption.12C4E70", SignatureGroup::caption, 0x12C4E70, "488b81f8050000c3", "view root = [view+5F8]"},
    {"caption.12C3B50", SignatureGroup::caption, 0x12C3B50,
     "40534883ec20488d0563be750148895108488901488bd944894110c6411400e85c050000",
     "observer bind: [obs+8]=rdx, [obs+14]=0"},
    {"caption.12C4AD0", SignatureGroup::caption, 0x12C4AD0,
     "48895c2410488974241848897c242048894c24085541564157488d6c24b04881ec50010000498bf94d8bf84c8bf2488bf1498379180f76034d8b09488b47104c894d80",
     "view base ctor prologue: rsi=param_4, rdi=param_3, r14=param_2, r15=this"},
    {"caption.12C4B4B", SignatureGroup::caption, 0x12C4B4B, "448b8590000000498bd7488d4e30e8f2efffff90",
     "observer bind(view+30, param_3=gui)"},
    {"caption.12C4CDF", SignatureGroup::caption, 0x12C4CDF, "4c89b6f005000048899ef8050000",
     "view+5F0=param_2, view+5F8 init"},
    {"caption.BB6085", SignatureGroup::caption, 0xBB6085,
     "4c8bc6488b9680010000488bc8e86918a600eb03498bc748898628010000", "politics ctor(gui+180, gui); gui+128=view"},
    {"caption.BBC112", SignatureGroup::caption, 0xBBC112,
     "488d9fc80000000f1f8000000000488b0b4885c9740f4438614474094883c130e8f98070004883c3084883ed0175df",
     "frame loop consumes view+44 via 12C4230(view+30)"},
    {"caption.BDB79E", SignatureGroup::caption, 0xBDB79E, "488b8be0000000488b01ff90b8000000488bc8",
     "tick: window+E0 idler, vslot B8 -> gui"},
    {"caption.BDB6D7", SignatureGroup::caption, 0xBDB6D7, "488b83e8000000400fb6ff817828703a0000",
     "model+28 == 3A70 player_context"},
    {"caption.BD8EF6", SignatureGroup::caption, 0xBD8EF6, "488db1e80000004c8b2e418b452c85c0",
     "bind: model=[window+E8], model+2C token"},
    {"caption.BD8F00", SignatureGroup::caption, 0xBD8F00, "418b452c85c00f8eb80000004c8b99f0000000",
     "bind: eax=[model+2C]; r11=[window+F0] (token map owner)"},
    {"caption.BDCDB0", SignatureGroup::caption, 0xBDCDB0,
     "488bc45556488bec4883ec6848895818488bf1488b4918488978e84c8968d84c8970d0488b01ff90b8000000488d4e70488bf8e848450000488b97880100004c8d4518",
     "manager: rsi=this, [this+18] vslot B8 -> gui (r14), map at this+70"},
    {"caption.BD8FC4", SignatureGroup::caption, 0xBD8FC4, "4983bd80000000000f84cf000000",
     "bind: parent_scripted_gui length model+80"},
    {"caption.BD8FA9", SignatureGroup::caption, 0xBD8FA9, "493bd07406488b4218eb0233c048898700010000",
     "bind: window+100 = token-map entry+18"},
    {"caption.BD904E", SignatureGroup::caption, 0xBD904E, "488db7e800000048c7870801000000000000",
     "bind: window+108 = 0"},
    {"caption.BD90A1", SignatureGroup::caption, 0xBD90A1, "4883bf00010000000f858a000000488b164883c25048837a1000747c",
     "bind: parent_window_window length model+60"},
    {"caption.BDD0C6", SignatureGroup::caption, 0xBDD0C6,
     "ba0c000000488b0848895918488bcfe846d7fdff488bc8e88e7d6e004c8d4518c74518b23a0000488d55c8488bd8488d4e70e8039effff",
     "token 3AB2 -> GetRoot(GetView(gui,12))"},
    {"caption.F8915", SignatureGroup::caption, 0xF8915,
     "8b1525278d02488b8c2460af0100e828ae05004c8d055137a7028b10488d0de8643d03e863e84402",
     "politics_tab literal <- DWORD 29CB040"},
    {"caption.F7B47", SignatureGroup::caption, 0xF7B47,
     "8b15b386a302488b8c2470ad0100e8f6bb05004c8d053f3ba7028b10488d0d46613d03e831f64402",
     "parent_window_token literal <- DWORD 2B30200 (=3A6E)"},
    {"caption.163B7BD", SignatureGroup::caption, 0x163B7BD, "8b86c00000004189452ce9d6090000",
     "parser copies token to model+2C"},
};

/// @brief Focus release chain (8 entries).
inline constexpr Signature focus_signatures[] = {
    {"focus.22BD0E0", SignatureGroup::focus, 0x22BD0E0,
     "48895c2408574883ec20488bf9488bda488b89380100004885c97409488b01ff90c800000048899f380100004885db740c488b03488bcbff90c0000000488b5c24304883c4205fc3",
     "focus switch: old->C8, [m+138]=new, new->C0 if non-null (whole function)"},
    {"focus.2371A40", SignatureGroup::focus, 0x2371A40,
     "48895c2408574883ec200fb68181020000488bd924fe888181020000a8207415", "edit unfocus: clear +271 bit0"},
    {"focus.2371AC0", SignatureGroup::focus, 0x2371AC0, "488d8ba0010000488b5c24304883c4205fe99a800000",
     "edit unfocus tail: 2379B70(edit+1A0)"},
    {"focus.2379B70", SignatureGroup::focus, 0x2379B70, "488bd1488b0d46741801e9f135f4ff",
     "receiver release via manager global 3500FC0"},
    {"focus.22BD170", SignatureGroup::focus, 0x22BD170,
     "40534883ec204883b91002000000488bd97419e8e83f0000488bc8488b10ff523048c78310020000000000004883c4205bc3",
     "manager: release [m+210] and zero it (whole function)"},
    {"focus.BDB55E", SignatureGroup::focus, 0xBDB55E, "488b83000100004885c07444f680a500000004743b",
     "tick: parent=[w+100]; parent+A5 bit4"},
    {"focus.BDB60B", SignatureGroup::focus, 0xBDB60B, "4885c00f84ae0000000fb688a5000000c0e90380e1010f8488000000",
     "tick: parent+A5 bit3 visible"},
    {"focus.BDB6AF", SignatureGroup::focus, 0xBDB6AF, "3a8bf80b0000",
     "tick: cmp cl,[window+BF8] parent-visible cache (diagnostic)"},
};

/// @brief Caption vtable slot pins (view visibility, observer frame/refresh, idler GUI getter).
inline constexpr SlotPin caption_slot_pins[] = {
    {rva::vt_politics_view, off::slot_view_visible, rva::view_is_visible},
    {rva::vt_politics_observer, off::slot_observer_frame, rva::observer_frame},
    {rva::vt_politics_observer, off::slot_observer_refresh, rva::observer_refresh},
    {rva::vt_ingame_idler, off::slot_idler_gui, rva::idler_gui_getter},
};

/// @brief Token DWORDs the caption chain compares against model tokens.
inline constexpr TokenPin caption_token_pins[] = {
    {"token.politics_tab", rva::token_politics_tab, value::token_politics_tab},
    {"token.player_context", rva::token_player_context, value::token_player_context},
};

/// @brief CEditBox focus/unfocus slot pins used by the focus release.
inline constexpr SlotPin focus_slot_pins[] = {
    {rva::vt_edit, off::slot_edit_unfocus, rva::edit_unfocus},
    {rva::vt_edit, off::slot_edit_focus, rva::edit_focus},
};

/// @brief `22FBEC3: mov rcx,[rip+disp]` must target the focus manager global.
inline constexpr RipPin focus_manager_rip_pin = {"focus.manager_global", rva::focus_manager_rip_displacement,
                                                 rva::focus_manager_rip_next, rva::manager_global};

/// @brief Number of caption signatures (25).
inline constexpr std::size_t caption_signature_count = sizeof(caption_signatures) / sizeof(caption_signatures[0]);
/// @brief Number of focus signatures (8).
inline constexpr std::size_t focus_signature_count = sizeof(focus_signatures) / sizeof(focus_signatures[0]);
/// @brief Number of editor signatures (11).
inline constexpr std::size_t editor_signature_count = sizeof(editor_signatures) / sizeof(editor_signatures[0]);
/// @brief Number of country signatures (3).
inline constexpr std::size_t country_signature_count = sizeof(country_signatures) / sizeof(country_signatures[0]);
/// @brief Expected table sizes (RE evidence: 25 caption, 8 focus, 11 editor, 3 country).
inline constexpr std::size_t expected_caption_signatures = 25;
/// @brief Expected number of focus signatures.
inline constexpr std::size_t expected_focus_signatures = 8;
/// @brief Expected number of editor signatures.
inline constexpr std::size_t expected_editor_signatures = 11;
/// @brief Expected number of country signatures.
inline constexpr std::size_t expected_country_signatures = 3;
static_assert(caption_signature_count == expected_caption_signatures &&
              focus_signature_count == expected_focus_signatures &&
              editor_signature_count == expected_editor_signatures &&
              country_signature_count == expected_country_signatures);

}  // namespace capie::core
