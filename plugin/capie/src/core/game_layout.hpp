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
inline constexpr std::uintptr_t loc_lookup_n = 0x2422910;
/// @brief `const char* lookup(const char* key)`; NUL-terminated lookup entry, hooked.
inline constexpr std::uintptr_t loc_lookup_z = 0x2422830;
/// @brief `bool exists(const char* key)`; key-existence test, hooked.
inline constexpr std::uintptr_t loc_exists = 0x2420BE0;

// ---- Country name layer (hooked or read by capie_core.dll) ------------------------------
/// @brief `u64 CCountry_UpdateNames(CCountry*)`; recomputes the cached name/adjective/definite name, hooked.
inline constexpr std::uintptr_t update_names = 0x75F850;
/// @brief `string* CCountry_BuildNameKey(string* out, const string* tag, ...)` (8 arguments), hooked.
inline constexpr std::uintptr_t build_name_key = 0x735D50;
/// @brief `CSetVariableEffect::Execute(effect*, context*)` (vtable slot +0x68), hooked.
inline constexpr std::uintptr_t set_variable_execute = 0x37DAF0;
/// @brief Variable table find (FNV-1a, robin-hood probe); pinned, never called.
inline constexpr std::uintptr_t variable_find = 0xC1C060;
/// @brief Variable get (`container+0x10` table, value at entry+0x28); pinned, never called.
inline constexpr std::uintptr_t variable_get = 0xC1F580;
/// @brief Country of a tag (`[gamestate+0x310][index]`); pinned, never called by capie_core.
inline constexpr std::uintptr_t country_of = 0xC093E0;
/// @brief Global pointer to the game state (read, never written).
inline constexpr std::uintptr_t gamestate_global = 0x33E3B30;
/// @brief `string* CFaction::GetName(faction*, string* out)`: +0x18 text, else template name; hooked.
inline constexpr std::uintptr_t faction_get_name = 0xDF9AB0;
/// @brief `u64 UpdateFlagAtlas(atlas*, gfx*)`: per-frame key check and reload of one flag atlas; hooked.
inline constexpr std::uintptr_t flag_atlas_update = 0x12D1870;
/// @brief `int LoadCountryFlag(atlas*, country*, dst*, int, int, int, int, void*)`: one atlas slot; hooked.
inline constexpr std::uintptr_t load_country_flag = 0x12D1230;
/// @brief `vector* BuildFlagCandidates(vector* out, const string* tag, const string* cosmetic, group*)`; hooked.
inline constexpr std::uintptr_t build_flag_candidates = 0x151C8C0;
/// @brief `string* string_copy(string* out, const string* source)` (MSVC copy-construct on the game heap); called.
inline constexpr std::uintptr_t string_copy = 0x11F800;
/// @brief `entry* ResolvePortrait(map*, entry* out, int type, int size, character*, const char* fallback)`; hooked.
inline constexpr std::uintptr_t portrait_resolve = 0x143D800;
/// @brief `bool PortraitEntryValid(const entry*)` (sprite exists for kind 0); called by the portrait hook.
inline constexpr std::uintptr_t portrait_valid = 0x143CAC0;
/// @brief `sprite* FindSprite(database*, const string* name)`; called by the console (sprite check).
inline constexpr std::uintptr_t sprite_find = 0x22A46E0;
/// @brief Global pointer to the sprite database (read by `PortraitEntryValid`).
inline constexpr std::uintptr_t sprite_database_global = 0x3507360;
/// @brief `CCharacter` primary vtable (hierarchy CCharacter, CReferenceObject, CPersistent).
inline constexpr std::uintptr_t vt_character = 0x29FFE20;
/// @brief `politics* GetPolitics(country*)`: `mov rax,[rcx+F90]; ret`; pinned, never called.
inline constexpr std::uintptr_t country_politics_getter = 0x718560;
/// @brief First leader role of a party (count +7C, roles +70); pinned, never called.
inline constexpr std::uintptr_t party_first_leader_role = 0x1233DC0;
/// @brief `mov rax,[rcx]; ret`, the tail jump of `party_first_leader_role`; pinned, never called.
inline constexpr std::uintptr_t pointer_load = 0x1B6C10;
/// @brief Character of a leader role: `mov rax,[rcx+8]; ret`; pinned, never called.
inline constexpr std::uintptr_t role_character_getter = 0x2C1AC0;
/// @brief Portrait write of `set_country_leader_portrait` (civilian, large); pinned, never called.
inline constexpr std::uintptr_t leader_portrait_write = 0x143EA20;
/// @brief `entry* PathPortraitEntry(entry* out, const string* path, bool warn, bool flag)`: always kind 1,
/// loads the texture at once; called by the portrait layer for format 2 paths (logic thread).
inline constexpr std::uintptr_t portrait_path_entry = 0x143CC90;
/// @brief `entry* MakeIconEntry(entry* out, int kind, string&& name, bool flag)`: the texture load of a
/// kind-1 entry; pinned (the builder's tail call), never called.
inline constexpr std::uintptr_t make_icon_entry = 0x143C380;
/// @brief `int AddTexture(texmgr*, const string* path, const options*, bool load_now)`; pinned, never called.
inline constexpr std::uintptr_t texture_add = 0x248C4B0;
/// @brief `texture* GetTexture(texmgr*, int id)`; pinned, never called.
inline constexpr std::uintptr_t texture_get = 0x248CF50;
/// @brief `void operator delete(void*, size_t)` of the game's CRT (jump thunk); called to free game strings.
inline constexpr std::uintptr_t game_delete = 0x25A39E8;
/// @brief `int* TextureSize(texmgr*, int out[2], int id)`: width and height of a texture; called (spikes).
inline constexpr std::uintptr_t texture_size = 0x248CD80;
/// @brief `void ReleaseTexture(texmgr*, int id)`: takes the AddTexture mutex, drops one reference; called (spikes).
inline constexpr std::uintptr_t texture_release = 0x248EDD0;
/// @brief `CSpriteType` lazy texture loader (writes +0x1A0, +0x170, +0xA4); pinned, never called.
inline constexpr std::uintptr_t sprite_lazy_load = 0x22AB790;
/// @brief `CSpriteType::SetTextureFile` (release, AddTexture, GetTexture, TextureSize); pinned, never called.
inline constexpr std::uintptr_t sprite_set_texture_file = 0x22AAFE0;
/// @brief `CSpriteType` vtable (RTTI-checked; a slot sprite must have it).
inline constexpr std::uintptr_t vt_sprite_type = 0x2BB8F80;
/// @brief `void GetFiles(const char* dir, CArray* out, const char* ends_with, const char* contains, int depth)`:
/// the VFS listing (honours `replace_path`); called by the flag and portrait catalogs (UI thread).
inline constexpr std::uintptr_t vfs_get_files = 0x25643B0;
/// @brief Sprite map lookup the FindSprite thunk jumps to (map at database +0x100); pinned, never called.
inline constexpr std::uintptr_t sprite_map_find = 0xA8AAF0;
/// @brief `void ScopeSetCountry(scope*, const tag*, bool)`: writes the scope's country at +0x8; pinned.
inline constexpr std::uintptr_t scope_set_country = 0x582B30;

// ---- State/VP view (API level 9; read and called by capie_editor.dll) -------------------------
/// @brief `string* ProvinceName(province*, string* out, bool* is_vp)`: custom name, else VP name; called.
inline constexpr std::uintptr_t province_name = 0xEF2680;
/// @brief `void ScopeSetState(scope*, int state_id, bool)`: writes the scope's state id at +0xA8; pinned.
inline constexpr std::uintptr_t scope_set_state = 0x583B60;
/// @brief The game's `_tls_index` (static TLS slot of hoi4.exe); read for `ThreadForbidCount`.
inline constexpr std::uintptr_t tls_index = 0x3699F28;
/// @brief `CState` primary vtable (RTTI-checked; every state the view reads must have it).
inline constexpr std::uintptr_t vt_state = 0x29B9988;
/// @brief `CProvince` primary vtable (RTTI-checked; every province the view reads must have it).
inline constexpr std::uintptr_t vt_province = 0x29F30A8;

// ---- Country colour layer (hooked, called or read by capie_core.dll) ---------------------
/// @brief `const CCountryColors* GetMapColors(const CCountry*)`: walks to the overlord, returns `+0x350`; hooked.
inline constexpr std::uintptr_t country_map_colors = 0x735CD0;
/// @brief `void RefreshCountryColor(map*, const tag*)`: mode 0 dirty, faction colour; called (guarded).
inline constexpr std::uintptr_t refresh_country_color = 0xAB9CD0;
/// @brief Global pointer to the map object `RefreshCountryColor` takes (read, never written).
inline constexpr std::uintptr_t map_singleton = 0x33EDC98;
/// @brief Global pointer to the current idler (read, never written; its slot 0x2D8 says "in game").
inline constexpr std::uintptr_t current_idler = 0x33E3F88;
/// @brief `CInGameIdler` slot 0x2D8: `mov al,1; ret` (in game); compared by address, never called.
inline constexpr std::uintptr_t idler_in_game_true = 0x188E20;
/// @brief Base idler slot 0x2D8: `xor al,al; ret` (not in game); compared by address, never called.
inline constexpr std::uintptr_t idler_in_game_false = 0x11D5C0;
/// @brief float `NGraphics.COUNTRY_COLOR_HUE_MODIFIER` (read).
inline constexpr std::uintptr_t define_color_hue = 0x33EB664;
/// @brief float `NGraphics.COUNTRY_COLOR_SATURATION_MODIFIER` (read).
inline constexpr std::uintptr_t define_color_saturation = 0x33EB70C;
/// @brief float `NGraphics.COUNTRY_COLOR_BRIGHTNESS_MODIFIER` (read).
inline constexpr std::uintptr_t define_color_value = 0x33EB794;
/// @brief float `NGraphics.COUNTRY_UI_COLOR_HUE_MODIFIER` (read).
inline constexpr std::uintptr_t define_ui_color_hue = 0x33EB824;
/// @brief float `NGraphics.COUNTRY_UI_COLOR_SATURATION_MODIFIER` (read).
inline constexpr std::uintptr_t define_ui_color_saturation = 0x33EB8C8;
/// @brief float `NGraphics.COUNTRY_UI_COLOR_BRIGHTNESS_MODIFIER` (read).
inline constexpr std::uintptr_t define_ui_color_value = 0x33EB968;
/// @brief `CCountryColors` vtable (the block the getter returns; RTTI-checked).
inline constexpr std::uintptr_t vt_country_colors = 0x27A35B0;
/// @brief `CColor` vtable (the two colours inside the block; RTTI-checked).
inline constexpr std::uintptr_t vt_color = 0x279A228;
/// @brief `CCountry` primary vtable (RTTI-checked; the getter's resolved country must have it).
inline constexpr std::uintptr_t vt_country = 0x28691D0;

// ---- Scripted window manager and widgets (hooked or called by capie_editor.dll) ---------
/// @brief `void Tick(window*, char force)`; per-frame scripted window update, hooked.
inline constexpr std::uintptr_t window_tick = 0xBDD2C0;
/// @brief `void Bind(window*)`; scripted window (re)bind, hooked. Called by the manager's rebuild
/// (`window_manager_rebuild`) for each new window before it enters the window array, and by
/// the window's rebind (vtable slot 0x10, which unbinds first).
inline constexpr std::uintptr_t window_bind = 0xBDAC90;
/// @brief `CScriptedWindowManager::Rebuild(manager*)`: destroys every window, then builds one per
/// scripted GUI template (window construction inlined) and binds it. The only place windows are
/// created; documentation only (pinned through `window_pins.hpp`), never called.
inline constexpr std::uintptr_t window_manager_rebuild = 0xBDD870;
/// @brief Constructor of the context embedded in a scripted window; pinned (`window_pins.hpp`), never called.
inline constexpr std::uintptr_t window_context_ctor = 0xBD9B00;
/// @brief `void Dispatch(context*, selector*)`; button click to effect dispatch, hooked.
inline constexpr std::uintptr_t window_dispatch = 0xBDB140;
/// @brief `CExecuteScriptedWindowEffect` constructor, hooked to substitute the record view.
inline constexpr std::uintptr_t effect_ctor = 0x11D1F70;
/// @brief `CExecuteScriptedWindowEffect::Execute` (`vt_execute_effect` slot 0x50), hooked (debug flavor).
inline constexpr std::uintptr_t effect_execute = 0x11E3FC0;
/// @brief CEditBox text-buffer key handler (subobject +0x80), hooked (debug flavor).
inline constexpr std::uintptr_t edit_key_handler = 0x2371E00;
/// @brief CEditBox primary key wrapper (vtable slot +0x10), hooked (debug flavor).
inline constexpr std::uintptr_t edit_key_wrapper = 0x2375340;
/// @brief `widget* FindEdit(root*, const string*, bool, bool)`; called.
inline constexpr std::uintptr_t find_edit = 0x2336D20;
/// @brief `widget* FindInstantText(root*, const string*, bool, bool)`; called.
inline constexpr std::uintptr_t find_instant = 0x23388A0;
/// @brief `void SetText(textbuffer*, const string*)` SSO thunk; called.
inline constexpr std::uintptr_t text_buffer_set = 0x23736C0;
/// @brief `void Refresh(CEditBox*)`; native edit-box refresh after SetText; called.
inline constexpr std::uintptr_t edit_refresh = 0x2375D90;

// ---- Caption refresh chain ----------------------------------------------------------------
/// @brief `void RequestViewRefresh(gui*, int index)`; the only caption call.
inline constexpr std::uintptr_t request_view_refresh = 0xBC3450;
/// @brief `mov byte [rcx+14],1; ret` (observer dirty mark); pinned, never called.
inline constexpr std::uintptr_t observer_mark_dirty = 0x12C7AC0;
/// @brief Frame-loop consumer: observer slot 0x48 then clears dirty; pinned, never called.
inline constexpr std::uintptr_t observer_consume = 0x12C7AF0;
/// @brief `mov rax,[rcx+6B8]; ret` (idler slot B8 getter); pinned, its semantics are read directly.
inline constexpr std::uintptr_t idler_gui_getter = 0xE3E070;
/// @brief Politics view visibility (primary slot 0x28); pinned.
inline constexpr std::uintptr_t view_is_visible = 0x12C8760;
/// @brief Politics view root getter `[rcx+5F8]`; pinned.
inline constexpr std::uintptr_t view_get_root = 0x12C8730;
/// @brief Politics observer slot 0x38; pinned.
inline constexpr std::uintptr_t observer_frame = 0x1621620;
/// @brief Politics observer slot 0x48 (leader and party writers); pinned.
inline constexpr std::uintptr_t observer_refresh = 0x1624370;
/// @brief Politics observer bind: `[observer+8] = gui`, `[observer+14] = 0`; pinned, never called.
inline constexpr std::uintptr_t observer_bind = 0x12C7410;
/// @brief Politics view base constructor (stores the idler and the view root); pinned, never called.
inline constexpr std::uintptr_t view_base_ctor = 0x12C8390;

// ---- Focus release chain -------------------------------------------------------------------
/// @brief `void FocusSwitch(manager*, widget*)`; called with `nullptr` once per politics hide.
inline constexpr std::uintptr_t focus_switch = 0x22C0C40;
/// @brief CEditBox unfocus (vtable slot 0xC8); pinned.
inline constexpr std::uintptr_t edit_unfocus = 0x23755A0;
/// @brief CEditBox focus (vtable slot 0xC0); pinned.
inline constexpr std::uintptr_t edit_focus = 0x2374D70;
/// @brief Receiver release thunk: `mov rdx,rcx; mov rcx,[manager_global]; jmp manager_release`; pinned.
inline constexpr std::uintptr_t receiver_release = 0x237D6D0;
/// @brief Manager release: releases `[manager+210]` and zeroes it; pinned, never called.
inline constexpr std::uintptr_t manager_release = 0x22C0CD0;
/// @brief RVA of the displacement of `mov rcx,[rip+disp]` in the receiver release thunk (`focus_release` chain).
inline constexpr std::uintptr_t focus_manager_rip_displacement = 0x237D6D6;
/// @brief RVA of the instruction after that `mov` (RIP base).
inline constexpr std::uintptr_t focus_manager_rip_next = 0x237D6DA;

// ---- Vtables (.rdata) -----------------------------------------------------------------------
/// @brief Scripted GUI model, `CScriptedWindowTemplate` (runtime-proven by the render bridge, RTTI-checked).
inline constexpr std::uintptr_t vt_sg_model = 0x2A59878;
/// @brief `CScriptedWindowManager` primary vtable (RTTI-checked; slot 0x38 is the per-frame Tick loop).
inline constexpr std::uintptr_t vt_sg_manager = 0x29CFFC8;
/// @brief Scripted GUI window context (runtime-proven).
inline constexpr std::uintptr_t vt_sg_context = 0x29CFF70;
/// @brief Scripted GUI root widget (runtime-proven).
inline constexpr std::uintptr_t vt_sg_root = 0x2BBFB60;
/// @brief instantTextBox widget used as a text carrier (runtime-proven).
inline constexpr std::uintptr_t vt_instant_text = 0x2BC10E8;
/// @brief CEditBox primary vtable.
inline constexpr std::uintptr_t vt_edit = 0x2BC4C08;
/// @brief CEditBox text-buffer subobject vtable (at edit+0x80).
inline constexpr std::uintptr_t vt_text_buffer = 0x2BC4EA8;
/// @brief CEditBox definition object vtable.
inline constexpr std::uintptr_t vt_edit_definition = 0x2BCC518;
/// @brief `CExecuteScriptedWindowEffect` vtable.
inline constexpr std::uintptr_t vt_execute_effect = 0x2A18078;
/// @brief `CCountryPoliticsView` primary vtable.
inline constexpr std::uintptr_t vt_politics_view = 0x2A579B0;
/// @brief Politics view observer subobject vtable (view+0x30).
inline constexpr std::uintptr_t vt_politics_observer = 0x2A57A60;
/// @brief In-game idler vtable.
inline constexpr std::uintptr_t vt_ingame_idler = 0x29EA628;

// ---- Data -------------------------------------------------------------------------------------
/// @brief Global pointer to the GUI focus/input manager.
inline constexpr std::uintptr_t manager_global = 0x3507450;
/// @brief DWORD token id of `politics_tab` (expected 0x3AB2).
inline constexpr std::uintptr_t token_politics_tab = 0x29CFCA0;
/// @brief DWORD token id of `player_context` (expected 0x3A70).
inline constexpr std::uintptr_t token_player_context = 0x2B35E34;
/// @brief Shared allocator global stored at container+B8 (diagnostic only, never a pin; R5-NOTE-1).
inline constexpr std::uintptr_t gui_structural = 0x3103000;
}  // namespace rva

/// @brief Byte offsets inside engine objects.
namespace off {
// ---- Country name layer (pinned in features/country/name_layer_pins.hpp) -----------------
inline constexpr std::uintptr_t country_variables = 0x218;    ///< Variable container pointer (`country.variables`).
inline constexpr std::uintptr_t variables_entries = 0x18;     ///< Entry array (table +8; variable find/get).
inline constexpr std::uintptr_t variables_mask = 0x24;        ///< int32 bucket mask (table +0x14; variable find).
inline constexpr std::uintptr_t variables_extra = 0x28;       ///< Byte: slots past the mask (table +0x18).
inline constexpr std::uintptr_t variable_entry_size = 0x30;   ///< Entry stride (variable find).
inline constexpr std::uintptr_t variable_entry_probe = 0x4;   ///< Byte: probe distance, 0 = empty (variable find).
inline constexpr std::uintptr_t variable_entry_key = 0x8;     ///< MSVC string key (variable find).
inline constexpr std::uintptr_t variable_entry_value = 0x28;  ///< int64 fixed-point value (variable get).
inline constexpr std::uintptr_t set_variable_name =
    0xC8;  ///< Effect +0x58 scoped variable, +0x70 name (`scoped_variable.name_*`).
inline constexpr std::uintptr_t gamestate_countries = 0x310;       ///< Country pointer array (`gamestate.countries`).
inline constexpr std::uintptr_t gamestate_country_count = 0x31C;   ///< int32 country count (`gamestate.country_count`).
inline constexpr std::uintptr_t flag_atlas_cosmetic_keys = 0xC0;   ///< int64 cosmetic id per slot (atlas pins).
inline constexpr std::uintptr_t flag_atlas_cosmetic_count = 0xCC;  ///< int32 slots of that array (atlas pins).
inline constexpr std::uintptr_t country_cosmetic_tag = 0x1488;     ///< Cosmetic tag string (`load.cosmetic_argument`).
inline constexpr std::uintptr_t country_tag = 0x8;                 ///< Tag; its int is the slot index (tag pin).
inline constexpr std::uintptr_t faction_members = 0x58;       ///< Member country array; [0] = leader (faction pins).
inline constexpr std::uintptr_t faction_member_count = 0x64;  ///< int32 member count (`faction.member_count`).
// ---- Leader portrait layer (pinned in features/country/portrait_layer_pins.hpp) ----------------
inline constexpr std::uintptr_t character_id = 0xC;            ///< int32 character id (`resolve.character_id`).
inline constexpr std::uintptr_t character_variables = 0xD8;    ///< Inline variable container (`character.*_variables`).
inline constexpr std::uintptr_t country_politics = 0xF90;      ///< Politics object pointer (`leader.politics`).
inline constexpr std::uintptr_t politics_ruling_party = 0xD0;  ///< Ruling party pointer (`leader.ruling_party`).
inline constexpr std::uintptr_t party_leaders = 0x70;          ///< Leader role pointer array (`leader.first_role`).
inline constexpr std::uintptr_t party_leader_count = 0x7C;     ///< int32 leader count (`leader.first_role`).
inline constexpr std::uintptr_t leader_role_character = 0x8;   ///< Character of a leader role (`leader.character`).
inline constexpr std::uintptr_t portrait_entry_kind = 0x4;     ///< int32 0 = sprite name, 1 = .dds path (`valid.kind`).
inline constexpr std::uintptr_t portrait_entry_name = 0x8;     ///< MSVC string (`valid.name`).
inline constexpr std::uintptr_t portrait_entry_handle = 0x28;  ///< Texture handle of a path entry (`valid.handle`).
// ---- State/VP view (pinned in features/catalog/catalog_pins.hpp) ----------------------------------
inline constexpr std::uintptr_t gamestate_states = 0x2C8;       ///< `CState*` array by id (`state.array`).
inline constexpr std::uintptr_t gamestate_state_count = 0x2D4;  ///< int32 slots of that array (`state.count`).
inline constexpr std::uintptr_t state_provinces = 0x18;         ///< `CProvince*` array (`state.provinces`).
inline constexpr std::uintptr_t state_province_count = 0x24;    ///< int32 province count (`state.province_count`).
inline constexpr std::uintptr_t province_vp = 0x38;             ///< int32 victory points (`province.vp_read`).
inline constexpr std::uintptr_t province_custom_name = 0x40;    ///< Player-given name, MSVC string (`province.custom_name`).
inline constexpr std::uintptr_t province_id = 0xA4;             ///< int32 province id (`province.id`).
inline constexpr std::uintptr_t scope_state_id = 0xA8;          ///< int32 state id of a scope (`scope.state_id`).
inline constexpr std::uintptr_t scope_country = 0x8;            ///< int32 country tag of a scope (`scope.country_write`).
inline constexpr std::uintptr_t gamestate_tag_texts = 0x358;    ///< Tag text array, stride 0x20 (`tag.text_array`).
inline constexpr std::uintptr_t tls_thread_forbid = 0x10;       ///< int32 `ThreadForbidCount` in the TLS block.
// ---- Slot sprites (pinned in features/catalog/catalog_pins.hpp, `sprite.*`) ------------------------
inline constexpr std::uintptr_t sprite_db = 0x48;            ///< Sprite database of a `CSpriteType` (`sprite.database`).
inline constexpr std::uintptr_t db_texture_manager = 0x170;  ///< Texture manager of the database (`sprite.texmgr`).
inline constexpr std::uintptr_t sprite_path = 0x70;          ///< `texturefile`, MSVC string (`sprite.path`).
inline constexpr std::uintptr_t sprite_size = 0xA4;          ///< Two int32: width, height (`sprite.store_size`).
inline constexpr std::uintptr_t sprite_texture = 0x170;      ///< `texture*` (`sprite.store_texture`).
inline constexpr std::uintptr_t sprite_texture_id = 0x1A0;   ///< int32 texture id, -1 = not loaded (`sprite.store_id`).
inline constexpr std::uintptr_t sprite_lazy = 0x1D3;         ///< Byte: loads lazily (`sprite.lazy_flag`).
inline constexpr std::uintptr_t texmgr_live = 0x38;          ///< int32 live path count of the texture manager (diagnostics).
// ---- Country colour layer (pinned in features/country/colour_layer_pins.hpp) ---------------------
inline constexpr std::uintptr_t country_colors = 0x350;      ///< `CCountryColors` block (`getter.block_offset`).
inline constexpr std::uintptr_t colors_color = 0x10;         ///< `CColor color` in the block (`colors.layout`).
inline constexpr std::uintptr_t colors_color_ui = 0x30;      ///< `CColor color_ui` in the block (`colors.layout`).
inline constexpr std::uintptr_t colors_flags = 0x50;         ///< Byte: 1 color read, 2 color_ui read (`colors.layout`).
inline constexpr std::uintptr_t color_rgba = 0x10;           ///< Four floats inside a `CColor` (CColor assign).
inline constexpr std::uintptr_t map_manager = 0x8;           ///< Map-mode manager of the map (`refresh.map_manager`).
inline constexpr std::uintptr_t map_modes = 0x30;            ///< Mode table of the manager (`mark.modes`).
inline constexpr std::uintptr_t mode_dirty = 0x48;           ///< Byte per mode: rebuild on the next update (`mark.mode0_dirty`).
inline constexpr std::uintptr_t idler_in_game_slot = 0x2D8;  ///< Idler vtable slot "in game" (`cosmetic.idler_slot`).
// ---- Scripted window ----------------------------------------------------------------------
inline constexpr std::uintptr_t window_idler = 0xE0;  ///< Idler pointer (tick's idler load).
inline constexpr std::uintptr_t window_model = 0xE8;  ///< Model (`CScriptedWindowTemplate`) pointer (runtime-proven).
/// @brief The owning `CScriptedWindowManager` (stored by the rebuild; vtable `vt_sg_manager`).
inline constexpr std::uintptr_t window_manager = 0xF0;
inline constexpr std::uintptr_t window_root = 0xF8;          ///< Root widget (runtime-proven).
inline constexpr std::uintptr_t window_parent = 0x100;       ///< Parent root (window bind).
inline constexpr std::uintptr_t window_parent_sg = 0x108;    ///< Parent scripted GUI (window bind).
inline constexpr std::uintptr_t window_context = 0x110;      ///< Embedded context object (runtime-proven).
inline constexpr std::uintptr_t window_parent_seen = 0xBF8;  ///< Parent-visible cache byte (diagnostic copy only).
inline constexpr std::uintptr_t window_desired = 0xBF9;      ///< Visible-trigger result byte (runtime-proven).
/// @brief Byte: 1 forces the next Tick to refresh (set by the unbind before every rebind, cleared by
/// Tick); diagnostic knowledge only, CAPIE never reads or writes it. The unbind also destroys the
/// old root, which is why no root pointer is ever kept.
inline constexpr std::uintptr_t window_force_refresh = 0xBFA;

// ---- Window context (at window + window_context) -------------------------------------------
inline constexpr std::uintptr_t context_window = 0x10;    ///< Back-pointer to the window.
inline constexpr std::uintptr_t context_model = 0x20;     ///< Model pointer.
inline constexpr std::uintptr_t context_root = 0x28;      ///< Root widget pointer.
inline constexpr std::uintptr_t context_scope = 0x3C0;    ///< Scope passed to the effect constructor (Dispatch).
inline constexpr std::uintptr_t context_records = 0x470;  ///< Temp-variable record vector (Dispatch).
/// @brief Highest context offset used; the context address must not wrap when it is added.
inline constexpr std::uintptr_t context_extent = context_records;

// ---- Scripted GUI model ---------------------------------------------------------------------
inline constexpr std::uintptr_t model_name = 0x8;                ///< Model name (MSVC string).
inline constexpr std::uintptr_t model_context_token = 0x28;      ///< Context token (window tick).
inline constexpr std::uintptr_t model_parent_token = 0x2C;       ///< Parent window token (window bind, token parser).
inline constexpr std::uintptr_t model_parent_window_len = 0x60;  ///< parent_window name length (window bind).
inline constexpr std::uintptr_t model_parent_sg_len = 0x80;      ///< parent_scripted_gui name length (window bind).

// ---- Root widget ------------------------------------------------------------------------------
inline constexpr std::uintptr_t root_flags = 0xA5;           ///< Flag byte (`flag::destroyed`, `shown`, `hidden`).
// CContainerWindow keeps one name hash map per widget type (0x38 apart, +12A0..+15E8, initialised
// by its constructor) and one vector of all children (docs/research/port-1.19.4.0.md).
inline constexpr std::uintptr_t root_instant_map = 0x1380;  ///< CInstantTextBox name map (`rva::find_instant`).
inline constexpr std::uintptr_t root_edit_map = 0x13F0;     ///< CEditBox name map (`rva::find_edit`).
inline constexpr std::uintptr_t root_children = 0x1508;     ///< Vector of all children, list layout (FindX recursion).
inline constexpr std::uintptr_t root_definition = 0x17D0;   ///< Window definition pointer (FindX error path).
inline constexpr std::size_t root_readable_span = 0x17D8;   ///< Bytes of the root checked readable (through +17D0).

// ---- Name hash map (lookup helper shared by the FindX functions) ----------------------------
inline constexpr std::uintptr_t map_entries = 0x20;     ///< Entry array pointer.
inline constexpr std::uintptr_t map_mask = 0x2C;        ///< int32 bucket mask.
inline constexpr std::uintptr_t map_entry_size = 0x30;  ///< Entry stride.

// ---- Bounded child list (data, capacity, count) ---------------------------------------------
inline constexpr std::uintptr_t list_data = 0x0;      ///< Element pointer array.
inline constexpr std::uintptr_t list_capacity = 0x8;  ///< int32 capacity.
inline constexpr std::uintptr_t list_count = 0xC;     ///< int32 count.

// ---- CEditBox ----------------------------------------------------------------------------------
inline constexpr std::uintptr_t edit_definition = 0x60;        ///< Definition object pointer.
inline constexpr std::uintptr_t edit_visibility_flags = 0x75;  ///< Widget flag byte (same bits as `root_flags`).
inline constexpr std::uintptr_t edit_buffer = 0x80;            ///< Text-buffer subobject (SetText `this`).
inline constexpr std::uintptr_t edit_text = 0xB0;              ///< UTF-32 text (inline when capacity <= 3).
inline constexpr std::uintptr_t edit_size = 0xC0;              ///< UTF-32 length.
inline constexpr std::uintptr_t edit_capacity = 0xC8;          ///< UTF-32 capacity.
inline constexpr std::uintptr_t edit_receiver = 0x1A0;         ///< Text-receiver subobject.
inline constexpr std::uintptr_t edit_flags = 0x281;            ///< Bit 0 focused/active, bit 1 disabled.
inline constexpr std::uintptr_t definition_name = 0x28;        ///< Edit definition name (MSVC string).

// ---- instantTextBox carrier --------------------------------------------------------------------
inline constexpr std::uintptr_t instant_text = 0x110;  ///< Displayed text (MSVC string).

// ---- Focus/input manager -------------------------------------------------------------------------
inline constexpr std::uintptr_t manager_focus = 0x138;     ///< Focused widget.
inline constexpr std::uintptr_t manager_receiver = 0x210;  ///< Active text receiver.
inline constexpr std::uintptr_t manager_idler = 0x18;      ///< Idler of the `CScriptedWindowManager` (window+E0 copies it).

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
inline constexpr std::uintptr_t idler_gui = 0x6B8;      ///< GUI container (`rva::idler_gui_getter` body).
inline constexpr std::uintptr_t gui_idler = 0x180;      ///< Back-pointer to the idler (politics view constructor).
inline constexpr std::uintptr_t gui_views = 0xC8;       ///< View pointer array (`rva::request_view_refresh`).
inline constexpr std::uintptr_t gui_structural = 0xB8;  ///< Allocator global copy (diagnostic only).
inline constexpr std::uintptr_t view_observer = 0x30;   ///< Observer subobject (`rva::request_view_refresh`).
inline constexpr std::uintptr_t view_gui = 0x38;        ///< GUI container back-pointer (view base constructor).
inline constexpr std::uintptr_t view_dirty = 0x44;      ///< Dirty byte (observer+0x14).
inline constexpr std::uintptr_t view_idler = 0x5F0;     ///< Idler (view base constructor).
inline constexpr std::uintptr_t view_root = 0x5F8;      ///< View root (`rva::view_get_root`).

// ---- Vtable slots ---------------------------------------------------------------------------------------
inline constexpr std::uintptr_t slot_view_visible = 0x28;      ///< Politics view visibility.
inline constexpr std::uintptr_t slot_observer_frame = 0x38;    ///< Observer frame.
inline constexpr std::uintptr_t slot_observer_refresh = 0x48;  ///< Observer refresh.
inline constexpr std::uintptr_t slot_idler_gui = 0xB8;         ///< Idler GUI getter.
inline constexpr std::uintptr_t slot_edit_focus = 0xC0;        ///< CEditBox focus.
inline constexpr std::uintptr_t slot_edit_unfocus = 0xC8;      ///< CEditBox unfocus.
inline constexpr std::uintptr_t parent_flags = root_flags;     ///< Parent root visibility byte (same layout as a root).
}  // namespace off

/// @brief Widget/root bits in the flag byte at `root_flags` / `edit_visibility_flags`.
namespace flag {
/// @brief Bit 2: being destroyed. Tick logs "window for %s is destroyed." (or "Parent window ...")
/// and drops the root and parent pointers. Every guard treats it as invalid, not hidden, so no
/// game call is made for such a window.
inline constexpr unsigned char destroyed = 0x04;
inline constexpr unsigned char shown = 0x08;                      ///< Bit 3: shown.
inline constexpr unsigned char hidden = 0x10;                     ///< Bit 4: hidden by script (Tick sets it on hide).
inline constexpr unsigned char hidden_any = destroyed | hidden;  ///< Any bit that means "not shown".
inline constexpr unsigned char edit_active = 0x01;                ///< edit+281 bit 0: focused/receiving input.
inline constexpr unsigned char edit_disabled = 0x02;              ///< edit+281 bit 1: disabled.

/**
 * @brief Visible test shared by every guard: shown bit set and no hidden bit.
 * @param flags Flag byte.
 * @return `true` when shown.
 */
constexpr bool shown_not_hidden(unsigned char flags) noexcept {
    return (flags & shown) != 0 && (flags & hidden_any) == 0;
}

/**
 * @brief Whether the destroyed bit is set (the guards then return "invalid").
 * @param flags Flag byte.
 * @return `true` when the object is being destroyed.
 */
constexpr bool is_destroyed(unsigned char flags) noexcept {
    return (flags & destroyed) != 0;
}
}  // namespace flag

/// @brief Token values and engine constants.
namespace value {
inline constexpr std::uint32_t token_politics_tab = 0x3AB2;    ///< `politics_tab` token id.
inline constexpr std::uint32_t token_player_context = 0x3A70;  ///< `player_context` token id.
inline constexpr int politics_view_index = 12;                 ///< Politics view slot in gui+C8 ((128-C8)/8).
inline constexpr int gui_view_limit = 0x17;                    ///< `RequestViewRefresh` ignores index 23 (`cmp edx,17`).
inline constexpr std::uint32_t key_enter = 0x0D;               ///< Enter in key event word 0.
inline constexpr std::uint32_t wrapper_type_key = 1;           ///< Wrapper event type of a key event.
inline constexpr std::size_t inline_utf32_capacity = 3;        ///< Edit text is stored inline up to this capacity.
inline constexpr std::size_t map_mode_count = 23;              ///< Map modes the update loop visits (`update.mode_count`).
inline constexpr std::uint8_t political_map_mode = 0;          ///< Builder reads `GetMapColors` (`rebuild.case_political`).
inline constexpr std::uint8_t claims_map_mode = 19;            ///< Builder reads `GetMapColors` (`rebuild.case_claims`).
inline constexpr std::uint8_t continent_map_mode = 21;         ///< Builder logs "Getting continent for" (`rebuild.case_continent`).
inline constexpr std::uint8_t colors_both_read = 3;            ///< `colors_flags` with color and color_ui set.
}  // namespace value

}  // namespace capie::core
