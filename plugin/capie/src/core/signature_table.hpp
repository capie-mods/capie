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
     "48895c240848896c241048897424205741544155415641574883ec40488bda488bf9488bcae8c622f8ff",
     "text-buffer key handler (edit+80 subobject)"},
    {"editor.find_edit", SignatureGroup::editor_callee, rva::find_edit,
     "48895c241048896c241856574154415641574883ec60450fb6e1450fb6f04c8bfa488be94c8d8140120000488d9158120000498bcf",
     "FindEdit: list displacements +1240/+1258 (twin of find_instant)"},
    {"editor.find_instant", SignatureGroup::editor_callee, rva::find_instant,
     "48895c241048896c241856574154415641574883ec60450fb6e1450fb6f04c8bfa488be94c8d81e0110000",
     "FindInstantText: list displacement +11E0"},
    {"editor.set_text", SignatureGroup::editor_callee, rva::text_buffer_set,
     "48837a180f7603488b12e901000000cc48895c2410574883ec40488bd90f57c00f11442420660f6f0dc3f68400",
     "SetText SSO thunk (capacity > 15 -> heap pointer)"},
    {"editor.refresh_edit", SignatureGroup::editor_callee, rva::edit_refresh,
     "48895c2410488974241848897c24205541564157488d6c24b94881ecd0000000488bf14533ff44897d674c39b9b00100000f84bd030000",
     "void(CEditBox*) refresh; complete 55-byte prefix ends at 22F4DB7"},
    {"editor.wrapper", SignatureGroup::editor_hook, rva::edit_key_wrapper,
     "48895c24105556574883ec40488bea488bf1f68171020000020f85c4010000488bcae88915f8ff85c0",
     "CEditBox key wrapper: edit+271 bit1 disabled test"},
    {"editor.dispatch", SignatureGroup::editor_hook, rva::window_dispatch,
     "48895c241048896c2418488974242057415641574883ec604c8bfa488bf9488d99c00300004533c0",
     "Dispatch(context*, selector*): context+3C0 scope"},
    {"editor.ctor", SignatureGroup::editor_hook, rva::effect_ctor,
     "48895c241848894c240855565741544155415641574883ec600f297424504d8bf1498bf8488bf24c8be1",
     "CExecuteScriptedWindowEffect ctor"},
    {"editor.execute", SignatureGroup::editor_hook, rva::effect_execute,
     "48895c240848896c2410488974241857415641574883ec604c8bf1e840c295ff498d5678488bc8e8742e96ff4c8bf84885c0",
     "Execute void(command*): vtable 2996ED0 slot 50, whole-instruction 50-byte prefix"},
};

/// @brief Caption refresh chain (25 entries; caption core evidence, RE 82CA2D2D).
inline constexpr Signature caption_signatures[] = {
    {"caption.DCCF30", SignatureGroup::caption, 0xDCCF30, "488b81b8060000c3",
     "idler slot B8 getter returns [idler+6B8]"},
    {"caption.B6E210", SignatureGroup::caption, 0xB6E210,
     "4883ec2883fa1774404863c248895c2420488d1cc1488b8bc80000004885c97423488b01ff502884c07419488b8bc80000004883c130488b5c24204883c428e9fc866c00488b5c24204883c428c3",
     "void(gui,int) index!=23, gui+C8+8i, view slot28, tail 1236950(view+30)"},
    {"caption.1236950", SignatureGroup::caption, 0x1236950, "c6411401c3", "observer+14=1 (view+44 dirty)"},
    {"caption.1236980", SignatureGroup::caption, 0x1236980, "40534883ec20488b01488bd9ff5048c64314004883c4205bc3",
     "consume: observer slot48 then +14=0"},
    {"caption.1237520", SignatureGroup::caption, 0x1237520,
     "40534883ec20488bd9488b89780500004885c97426488b01ff904802000084c07519488b83780500000fb680a5000000c0e80324014883c4205bc3",
     "politics view visibility (slot28) reads view+578"},
    {"caption.12374F0", SignatureGroup::caption, 0x12374F0, "488b8178050000c3", "view root = [view+578]"},
    {"caption.12362A0", SignatureGroup::caption, 0x12362A0,
     "40534883ec20488d0553db760148895108488901488bd944894110c6411400e85c050000",
     "observer bind: [obs+8]=rdx, [obs+14]=0"},
    {"caption.1237180", SignatureGroup::caption, 0x1237180,
     "48895c2410488974241848897c242048894c24085541564157488d6c24d04881ec30010000498bf1498bf84c8bf24c8bf9",
     "view base ctor prologue: rsi=param_4, rdi=param_3, r14=param_2, r15=this"},
    {"caption.12371DB", SignatureGroup::caption, 0x12371DB, "448b4570488bd7498d4f30e8b5f0ffff90",
     "observer bind(view+30, param_3=gui)"},
    {"caption.1237358", SignatureGroup::caption, 0x1237358, "4d89b77005000049899f78050000",
     "view+570=param_2, view+578 init"},
    {"caption.B62D52", SignatureGroup::caption, 0xB62D52,
     "4c8bc6488b9680010000488bc8e89c98a100eb03498bc748898628010000", "politics ctor(gui+180, gui); gui+128=view"},
    {"caption.B68C52", SignatureGroup::caption, 0xB68C52,
     "488d9fc80000000f1f8000000000488b0b4885c9740f4438714474094883c130e809dd6c004883c3084883ed0175df",
     "frame loop consumes view+44 via 1236980(view+30)"},
    {"caption.B88BEE", SignatureGroup::caption, 0xB88BEE, "488b8be0000000488b01ff90b8000000488bc8",
     "tick: window+E0 idler, vslot B8 -> gui"},
    {"caption.B88B27", SignatureGroup::caption, 0xB88B27, "488b83e8000000400fb6ff817828703a0000",
     "model+28 == 3A70 player_context"},
    {"caption.B86346", SignatureGroup::caption, 0xB86346, "488db1e80000004c8b2e418b452c85c0",
     "bind: model=[window+E8], model+2C token"},
    {"caption.B86350", SignatureGroup::caption, 0xB86350, "418b452c85c00f8eb80000004c8b99f0000000",
     "bind: eax=[model+2C]; r11=[window+F0] (token map owner)"},
    {"caption.B8A1C0", SignatureGroup::caption, 0xB8A1C0,
     "48895c24185556574154415541564157488d6c24d94881ecc00000000f29b424b0000000488bf1488b4918488b01ff90b80000004c8bf0488d4e70e8f0490000",
     "manager: rsi=this, [this+18] vslot B8 -> gui (r14), map at this+70"},
    {"caption.B86414", SignatureGroup::caption, 0xB86414, "4983bd80000000000f84cf000000",
     "bind: parent_scripted_gui length model+80"},
    {"caption.B863F9", SignatureGroup::caption, 0xB863F9, "493bd07406488b4218eb0233c048898700010000",
     "bind: window+100 = token-map entry+18"},
    {"caption.B8649E", SignatureGroup::caption, 0xB8649E, "488db7e800000048c7870801000000000000",
     "bind: window+108 = 0"},
    {"caption.B864F1", SignatureGroup::caption, 0xB864F1, "4883bf00010000000f858a000000488b164883c25048837a1000747c",
     "bind: parent_window_window length model+60"},
    {"caption.B8A660", SignatureGroup::caption, 0xB8A660,
     "ba0c000000498bcee883cefdff488bc8e87bce6a00488bd8c74567b23a00004c8d4567488d5597488d4e70e8709dffff488b0848895918",
     "token 3AB2 -> GetRoot(GetView(gui,12))"},
    {"caption.F86CC", SignatureGroup::caption, 0xF86CC,
     "8b15fa5d8502488b8c24d0af0100e8713605004c8d05da079f028b10488d0db98d3203e87cc43c02",
     "politics_tab literal <- DWORD 294E4CC"},
    {"caption.F78FE", SignatureGroup::caption, 0xF78FE,
     "8b15e4519f02488b8c24e0ad0100e83f4405004c8d05f80b9f028b10488d0d178a3203e84ad23c02",
     "parent_window_token literal <- DWORD 2AECAE8 (=3A6E)"},
    {"caption.159F33D", SignatureGroup::caption, 0x159F33D, "8b86c00000004189452ce9d6090000",
     "parser copies token to model+2C"},
};

/// @brief Focus release chain (8 entries).
inline constexpr Signature focus_signatures[] = {
    {"focus.225E320", SignatureGroup::focus, 0x225E320,
     "48895c2408574883ec20488bf9488bda488b89580100004885c97409488b01ff90c800000048899f580100004885db740c488b03488bcbff90c0000000488b5c24304883c4205fc3",
     "focus switch: old->C8, [m+158]=new, new->C0 if non-null (whole function)"},
    {"focus.22F4770", SignatureGroup::focus, 0x22F4770,
     "48895c2408574883ec200fb68171020000488bd924fe888171020000a8107415", "edit unfocus: clear +271 bit0"},
    {"focus.22F47F0", SignatureGroup::focus, 0x22F47F0, "488d8ba0010000488b5c24304883c4205fe9ba760000",
     "edit unfocus tail: 22FBEC0(edit+1A0)"},
    {"focus.22FBEC0", SignatureGroup::focus, 0x22FBEC0, "488bd1488b0d66731501e9e124f6ff",
     "receiver release via manager global 3453230"},
    {"focus.225E3B0", SignatureGroup::focus, 0x225E3B0,
     "40534883ec204883b93002000000488bd97419e838fdfeff488bc8488b10ff523048c78330020000000000004883c4205bc3",
     "manager: release [m+230] and zero it (whole function)"},
    {"focus.B889AE", SignatureGroup::focus, 0xB889AE, "488b83000100004885c07444f680a500000004743b",
     "tick: parent=[w+100]; parent+A5 bit4"},
    {"focus.B88A5B", SignatureGroup::focus, 0xB88A5B, "4885c00f84ae0000000fb688a5000000c0e90380e1010f8488000000",
     "tick: parent+A5 bit3 visible"},
    {"focus.B88AFF", SignatureGroup::focus, 0xB88AFF, "3a8b780b0000",
     "tick: cmp cl,[window+B78] parent-visible cache (diagnostic)"},
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
