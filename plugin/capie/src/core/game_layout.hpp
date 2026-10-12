/**
 * @file game_layout.hpp
 * @brief Every RVA, structure offset, flag bit and token value CAPIE relies on, by name.
 *
 * This is the only place where engine numbers appear. Each value is valid for the build
 * pinned in `game_build.hpp` and comes with the evidence it rests on: a byte signature in
 * `signature_table.hpp`, an instruction whose displacement is the offset (RE findings
 * section 6.3), or a runtime-accepted guard from earlier bridges ("runtime-proven").
 *
 * Nothing in this file is read or called. Feature code turns these numbers into checked
 * reads through `core/memory.hpp` and into calls through `engine/game_calls.hpp`.
 *
 * Risks: an offset that is only inferred (marked OPEN) is used as an equality guard, never
 * to write or to call, so a wrong inference can only make CAPIE refuse.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace capie::core {

/// @brief RVAs inside the pinned `hoi4.exe` image (relative to the loaded base).
namespace rva {
// ---- Localisation (hooked by capie_core.dll) --------------------------------------------
/// @brief `const char* lookup(const char* key, int length)`; FNV-1a lookup entry, hooked.
inline constexpr std::uintptr_t loc_lookup_n = 0x239EE20;
/// @brief `const char* lookup(const char* key)`; NUL-terminated lookup entry, hooked.
inline constexpr std::uintptr_t loc_lookup_z = 0x239ED40;
/// @brief `bool exists(const char* key)`; key-existence test, hooked.
inline constexpr std::uintptr_t loc_exists = 0x239D0F0;

// ---- Scripted window manager and widgets (hooked or called by capie_editor.dll) ---------
/// @brief `void Tick(window*, char force)`; per-frame scripted window update, hooked.
inline constexpr std::uintptr_t window_tick = 0xB88950;
/// @brief `void Bind(window*)`; scripted window (re)bind, hooked.
inline constexpr std::uintptr_t window_bind = 0xB86320;
/// @brief `void Dispatch(context*, selector*)`; button click to effect dispatch, hooked.
inline constexpr std::uintptr_t window_dispatch = 0xB867D0;
/// @brief `CExecuteScriptedWindowEffect` constructor, hooked to substitute the record view.
inline constexpr std::uintptr_t effect_ctor = 0x1144840;
/// @brief `CExecuteScriptedWindowEffect::Execute` (vtable 2996ED0 slot 0x50), hooked (debug flavor).
inline constexpr std::uintptr_t effect_execute = 0x1156950;
/// @brief CEditBox text-buffer key handler (subobject +0x80), hooked (debug flavor).
inline constexpr std::uintptr_t edit_key_handler = 0x22F1030;
/// @brief CEditBox primary key wrapper (vtable slot +0x10), hooked (debug flavor).
inline constexpr std::uintptr_t edit_key_wrapper = 0x22F4580;
/// @brief `widget* FindEdit(root*, const string*, bool, bool)`; called.
inline constexpr std::uintptr_t find_edit = 0x22BAD40;
/// @brief `widget* FindInstantText(root*, const string*, bool, bool)`; called.
inline constexpr std::uintptr_t find_instant = 0x22BC600;
/// @brief `void SetText(textbuffer*, const string*)` SSO thunk; called.
inline constexpr std::uintptr_t text_buffer_set = 0x22F27C0;
/// @brief `void Refresh(CEditBox*)`; native edit-box refresh after SetText; called.
inline constexpr std::uintptr_t edit_refresh = 0x22F4D80;

// ---- Caption refresh chain ----------------------------------------------------------------
/// @brief `void RequestViewRefresh(gui*, int index)`; the only caption call (B6E210).
inline constexpr std::uintptr_t request_view_refresh = 0xB6E210;
/// @brief `mov byte [rcx+14],1; ret` (observer dirty mark); pinned, never called.
inline constexpr std::uintptr_t observer_mark_dirty = 0x1236950;
/// @brief Frame-loop consumer: observer slot 0x48 then clears dirty; pinned, never called.
inline constexpr std::uintptr_t observer_consume = 0x1236980;
/// @brief `mov rax,[rcx+6B8]; ret` (idler slot B8 getter); pinned, its semantics are read directly.
inline constexpr std::uintptr_t idler_gui_getter = 0xDCCF30;
/// @brief Politics view visibility (primary slot 0x28); pinned.
inline constexpr std::uintptr_t view_is_visible = 0x1237520;
/// @brief Politics view root getter `[rcx+578]`; pinned.
inline constexpr std::uintptr_t view_get_root = 0x12374F0;
/// @brief Politics observer slot 0x38; pinned.
inline constexpr std::uintptr_t observer_frame = 0x1582820;
/// @brief Politics observer slot 0x48 (leader and party writers); pinned.
inline constexpr std::uintptr_t observer_refresh = 0x15863F0;

// ---- Focus release chain -------------------------------------------------------------------
/// @brief `void FocusSwitch(manager*, widget*)`; called with `nullptr` once per politics hide.
inline constexpr std::uintptr_t focus_switch = 0x225E320;
/// @brief CEditBox unfocus (vtable slot 0xC8); pinned.
inline constexpr std::uintptr_t edit_unfocus = 0x22F4770;
/// @brief CEditBox focus (vtable slot 0xC0); pinned.
inline constexpr std::uintptr_t edit_focus = 0x22F3FB0;
/// @brief RVA of the rel32 displacement in `22FBEC3: mov rcx,[rip+disp]`.
inline constexpr std::uintptr_t focus_manager_rip_displacement = 0x22FBEC6;
/// @brief RVA of the instruction after that `mov` (RIP base).
inline constexpr std::uintptr_t focus_manager_rip_next = 0x22FBECA;

// ---- Vtables (.rdata) -----------------------------------------------------------------------
/// @brief Scripted GUI model (runtime-proven by the render bridge).
inline constexpr std::uintptr_t vt_sg_model = 0x29DB558;
/// @brief Scripted GUI window context (runtime-proven).
inline constexpr std::uintptr_t vt_sg_context = 0x294E568;
/// @brief Scripted GUI root widget (runtime-proven).
inline constexpr std::uintptr_t vt_sg_root = 0x2B45278;
/// @brief instantTextBox widget used as a text carrier (runtime-proven).
inline constexpr std::uintptr_t vt_instant_text = 0x2B468C8;
/// @brief CEditBox primary vtable.
inline constexpr std::uintptr_t vt_edit = 0x2B4A940;
/// @brief CEditBox text-buffer subobject vtable (at edit+0x80).
inline constexpr std::uintptr_t vt_text_buffer = 0x2B4ABD0;
/// @brief CEditBox definition object vtable.
inline constexpr std::uintptr_t vt_edit_definition = 0x2B54050;
/// @brief `CExecuteScriptedWindowEffect` vtable.
inline constexpr std::uintptr_t vt_execute_effect = 0x2996ED0;
/// @brief `CCountryPoliticsView` primary vtable.
inline constexpr std::uintptr_t vt_politics_view = 0x29D9648;
/// @brief Politics view observer subobject vtable (view+0x30).
inline constexpr std::uintptr_t vt_politics_observer = 0x29D96F0;
/// @brief In-game idler vtable.
inline constexpr std::uintptr_t vt_ingame_idler = 0x2968FF0;

// ---- Data -------------------------------------------------------------------------------------
/// @brief Global pointer to the GUI focus/input manager.
inline constexpr std::uintptr_t manager_global = 0x3453230;
/// @brief DWORD token id of `politics_tab` (expected 0x3AB2).
inline constexpr std::uintptr_t token_politics_tab = 0x294E4CC;
/// @brief DWORD token id of `player_context` (expected 0x3A70).
inline constexpr std::uintptr_t token_player_context = 0x2AECB3C;
/// @brief Shared allocator global stored at container+B8 (diagnostic only, never a pin; R5-NOTE-1).
inline constexpr std::uintptr_t gui_structural = 0x3085170;
}  // namespace rva

/// @brief Byte offsets inside engine objects.
namespace off {
// ---- Scripted window ----------------------------------------------------------------------
inline constexpr std::uintptr_t window_idler = 0xE0;  ///< Idler pointer (B88BEE).
inline constexpr std::uintptr_t window_model = 0xE8;  ///< Model pointer (runtime-proven).
/// @brief Token-map owner; manager identity is OPEN (used as an equality guard only).
inline constexpr std::uintptr_t window_manager = 0xF0;
inline constexpr std::uintptr_t window_root = 0xF8;          ///< Root widget (runtime-proven).
inline constexpr std::uintptr_t window_parent = 0x100;       ///< Parent root (B863F9).
inline constexpr std::uintptr_t window_parent_sg = 0x108;    ///< Parent scripted GUI (B8649E).
inline constexpr std::uintptr_t window_context = 0x110;      ///< Embedded context object (runtime-proven).
inline constexpr std::uintptr_t window_parent_seen = 0xB78;  ///< Parent-visible cache byte (diagnostic copy only).
inline constexpr std::uintptr_t window_desired = 0xB79;      ///< Visible-trigger result byte (runtime-proven).

// ---- Window context (at window + window_context) -------------------------------------------
inline constexpr std::uintptr_t context_window = 0x10;    ///< Back-pointer to the window.
inline constexpr std::uintptr_t context_model = 0x20;     ///< Model pointer.
inline constexpr std::uintptr_t context_root = 0x28;      ///< Root widget pointer.
inline constexpr std::uintptr_t context_scope = 0x3C0;    ///< Scope passed to the effect constructor (B867EE).
inline constexpr std::uintptr_t context_records = 0x470;  ///< Temp-variable record vector (B8684D).
/// @brief Highest context offset used; the context address must not wrap when it is added.
inline constexpr std::uintptr_t context_extent = context_records;

// ---- Scripted GUI model ---------------------------------------------------------------------
inline constexpr std::uintptr_t model_name = 0x8;                ///< Model name (MSVC string).
inline constexpr std::uintptr_t model_context_token = 0x28;      ///< Context token (B88B27).
inline constexpr std::uintptr_t model_parent_token = 0x2C;       ///< Parent window token (B86346, 159F33D).
inline constexpr std::uintptr_t model_parent_window_len = 0x60;  ///< parent_window name length (B864F1).
inline constexpr std::uintptr_t model_parent_sg_len = 0x80;      ///< parent_scripted_gui name length (B86414).

// ---- Root widget ------------------------------------------------------------------------------
inline constexpr std::uintptr_t root_flags = 0xA5;           ///< Visibility flag byte.
inline constexpr std::uintptr_t root_instant_list = 0x11E0;  ///< instantTextBox child list.
inline constexpr std::uintptr_t root_edit_list = 0x1240;     ///< Edit-box child list.
inline constexpr std::uintptr_t root_third_list = 0x1330;    ///< Third child list checked by the render bridge.
inline constexpr std::size_t root_readable_span = 0x1360;    ///< Bytes of the root checked readable by the bridge.

// ---- Bounded child list (data, capacity, count) ---------------------------------------------
inline constexpr std::uintptr_t list_data = 0x0;      ///< Element pointer array.
inline constexpr std::uintptr_t list_capacity = 0x8;  ///< int32 capacity.
inline constexpr std::uintptr_t list_count = 0xC;     ///< int32 count.

// ---- CEditBox ----------------------------------------------------------------------------------
inline constexpr std::uintptr_t edit_definition = 0x60;        ///< Definition object pointer.
inline constexpr std::uintptr_t edit_visibility_flags = 0x75;  ///< Widget flag byte (bit 3 shown, bits 2/4 hidden).
inline constexpr std::uintptr_t edit_buffer = 0x80;            ///< Text-buffer subobject (SetText `this`).
inline constexpr std::uintptr_t edit_text = 0xB0;              ///< UTF-32 text (inline when capacity <= 3).
inline constexpr std::uintptr_t edit_size = 0xC0;              ///< UTF-32 length.
inline constexpr std::uintptr_t edit_capacity = 0xC8;          ///< UTF-32 capacity.
inline constexpr std::uintptr_t edit_receiver = 0x1A0;         ///< Text-receiver subobject.
inline constexpr std::uintptr_t edit_flags = 0x271;            ///< Bit 0 focused/active, bit 1 disabled.
inline constexpr std::uintptr_t definition_name = 0x28;        ///< Edit definition name (MSVC string).

// ---- instantTextBox carrier --------------------------------------------------------------------
inline constexpr std::uintptr_t instant_text = 0x110;  ///< Displayed text (MSVC string).

// ---- Focus/input manager -------------------------------------------------------------------------
inline constexpr std::uintptr_t manager_focus = 0x158;     ///< Focused widget.
inline constexpr std::uintptr_t manager_receiver = 0x230;  ///< Active text receiver.
inline constexpr std::uintptr_t manager_idler = 0x18;      ///< Idler of the token-map owner (equality guard).

// ---- Key event ------------------------------------------------------------------------------------
inline constexpr std::size_t key_event_words = 8;         ///< DWORDs copied from a key event.
inline constexpr std::uintptr_t key_wrapper_type = 0x7C;  ///< Wrapper event type DWORD.

// ---- Scripted window effect command ----------------------------------------------------------------
inline constexpr std::uintptr_t command_records = 0x28;         ///< Record vector.
inline constexpr std::uintptr_t command_scope_capacity = 0x48;  ///< Scope list capacity.
inline constexpr std::uintptr_t command_scope_count = 0x4C;     ///< Scope list count.
inline constexpr std::uintptr_t command_selector = 0x58;        ///< Selector name.
inline constexpr std::uintptr_t command_model = 0x78;           ///< Model name.
inline constexpr std::uintptr_t effect_records = 0x28;          ///< Record vector of a constructed effect.

// ---- Idler, GUI container, politics view -------------------------------------------------------------
inline constexpr std::uintptr_t idler_gui = 0x6B8;      ///< GUI container (DCCF30 body).
inline constexpr std::uintptr_t gui_idler = 0x180;      ///< Back-pointer to the idler (B62D55).
inline constexpr std::uintptr_t gui_views = 0xC8;       ///< View pointer array (B6E225).
inline constexpr std::uintptr_t gui_structural = 0xB8;  ///< Allocator global copy (diagnostic only).
inline constexpr std::uintptr_t view_observer = 0x30;   ///< Observer subobject (B6E242).
inline constexpr std::uintptr_t view_gui = 0x38;        ///< GUI container back-pointer (12371DB).
inline constexpr std::uintptr_t view_dirty = 0x44;      ///< Dirty byte (observer+0x14).
inline constexpr std::uintptr_t view_idler = 0x570;     ///< Idler (1237358).
inline constexpr std::uintptr_t view_root = 0x578;      ///< View root (12374F0).

// ---- Vtable slots ---------------------------------------------------------------------------------------
inline constexpr std::uintptr_t slot_view_visible = 0x28;      ///< Politics view visibility.
inline constexpr std::uintptr_t slot_observer_frame = 0x38;    ///< Observer frame.
inline constexpr std::uintptr_t slot_observer_refresh = 0x48;  ///< Observer refresh.
inline constexpr std::uintptr_t slot_idler_gui = 0xB8;         ///< Idler GUI getter.
inline constexpr std::uintptr_t slot_edit_focus = 0xC0;        ///< CEditBox focus.
inline constexpr std::uintptr_t slot_edit_unfocus = 0xC8;      ///< CEditBox unfocus.
inline constexpr std::uintptr_t parent_flags = root_flags;     ///< Parent root visibility byte (same layout as a root).
}  // namespace off

/// @brief Widget/root visibility bits in the flag byte at `root_flags` / `edit_visibility_flags`.
namespace flag {
inline constexpr unsigned char hidden_a = 0x04;                   ///< Hidden (bit 2).
inline constexpr unsigned char shown = 0x08;                      ///< Shown (bit 3).
inline constexpr unsigned char hidden_b = 0x10;                   ///< Hidden (bit 4).
inline constexpr unsigned char hidden_any = hidden_a | hidden_b;  ///< Any hidden bit.
inline constexpr unsigned char edit_active = 0x01;                ///< edit+271 bit 0: focused/receiving input.
inline constexpr unsigned char edit_disabled = 0x02;              ///< edit+271 bit 1: disabled.

/**
 * @brief Visible test shared by every guard: shown bit set and no hidden bit.
 * @param flags Flag byte.
 * @return `true` when shown.
 */
constexpr bool shown_not_hidden(unsigned char flags) noexcept {
    return (flags & shown) != 0 && (flags & hidden_any) == 0;
}
}  // namespace flag

/// @brief Token values and engine constants.
namespace value {
inline constexpr std::uint32_t token_politics_tab = 0x3AB2;    ///< `politics_tab` token id.
inline constexpr std::uint32_t token_player_context = 0x3A70;  ///< `player_context` token id.
inline constexpr int politics_view_index = 12;                 ///< Politics view slot in gui+C8 ((128-C8)/8, B62D69).
inline constexpr int gui_view_limit = 0x17;                    ///< B6E210 ignores index 23 (`cmp edx,17`).
inline constexpr std::uint32_t key_enter = 0x0D;               ///< Enter in key event word 0.
inline constexpr std::uint32_t wrapper_type_key = 1;           ///< Wrapper event type of a key event.
inline constexpr std::size_t inline_utf32_capacity = 3;        ///< Edit text is stored inline up to this capacity.
}  // namespace value

}  // namespace capie::core
