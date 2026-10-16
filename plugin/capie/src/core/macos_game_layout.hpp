/** @file macos_game_layout.hpp
 * @brief Numeric native map for the exact SHA 09419b08...b1fb; evidence: docs/macos/address-inventory.json.
 * Static mappings are not call-ABI or lifecycle authorization. Tagged fields and policy replacements
 * intentionally have no numeric aliases; callers must use the macOS adapters.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace capie::core::macos::rva {
/// @brief 0x10288da90; localisation-coverage.md.
inline constexpr std::uintptr_t loc_lookup_n = 0x288da90;
/// @brief 0x10288d420; localisation-coverage.md.
inline constexpr std::uintptr_t loc_lookup_z = 0x288d420;
/// @brief 0x10288e4c0; localisation-coverage.md.
inline constexpr std::uintptr_t loc_exists = 0x288e4c0;
/// @brief 0x1011ca76a, 0x1011ca772, 0x10117abe1, 0x10117ac2d; layers-map.md.
inline constexpr std::uintptr_t update_names = 0x117a790;
/// @brief 0x10117abe1, 0x10117ac2a, 0x10117ac2d; layers-map.md.
inline constexpr std::uintptr_t build_name_key = 0x1177f30;
/// @brief 0x101060800, 0x101060844; layers-map.md.
inline constexpr std::uintptr_t set_variable_execute = 0x10607f0;
/// @brief 0x100b031a4, 0x100b03287, 0x100b03294, 0x100b03354, 0x100b0335b; layers-map.md.
inline constexpr std::uintptr_t variable_find = 0xb031c0;
/// @brief 0x100b0319c, 0x100b031a4, 0x100b031a9; layers-map.md.
inline constexpr std::uintptr_t variable_get = 0xb03190;
/// @brief 0x10095c13d, 0x10095c142, 0x1011d7250; layers-map.md.
inline constexpr std::uintptr_t country_of = 0x11d7110;
/// @brief 0x1011d7074; layers-map.md.
inline constexpr std::uintptr_t gamestate_global = 0x3519740;
/// @brief 0x100e527ee, 0x100e52832, 0x100e52848, 0x100db7a9e; layers-map.md.
inline constexpr std::uintptr_t faction_get_name = 0xe527e0;
/// @brief 0x10194ac3a, 0x10194ad8a, 0x10194ad91; layers-map.md.
inline constexpr std::uintptr_t flag_atlas_update = 0x194a920;
/// @brief 0x10194a1fd, 0x10194bdc1, 0x10194a4e2; layers-map.md.
inline constexpr std::uintptr_t load_country_flag = 0x194a460;
/// @brief 0x10194a4ac, 0x10194a4d9, 0x10194a4e2; layers-map.md.
inline constexpr std::uintptr_t build_flag_candidates = 0x257750;
/// @brief 0x1023d33db, 0x100d6fbf7, 0x100d6fd01; layers-map.md.
inline constexpr std::uintptr_t portrait_resolve = 0xd6fbe0;
/// @brief 0x1023d33e3, 0x1007581af, 0x1007581bb; layers-map.md.
inline constexpr std::uintptr_t portrait_valid = 0x7581a0;
/// @brief 0x1007581e6; layers-map.md.
inline constexpr std::uintptr_t sprite_find = 0x27b7b30;
/// @brief 0x1007581d8, 0x1007581df; layers-map.md.
inline constexpr std::uintptr_t sprite_database_global = 0x35dc028;
/// @brief Inventory evidence; layers-map.md.
inline constexpr std::uintptr_t vt_character = 0x332efe0;
/// @brief 0x100b44034, 0x100b443d5; editor-map.md.
inline constexpr std::uintptr_t window_tick = 0xB44020;
/// @brief 0x100b45ed4, 0x100b462a7, 0x100b462af, 0x100b463df; editor-map.md.
inline constexpr std::uintptr_t window_bind = 0xB45EC0;
/// @brief 0x100b480f7, 0x100b4b8ec, 0x100b4bae5; editor-map.md.
inline constexpr std::uintptr_t window_dispatch = 0xB4B7E0;
/// @brief 0x10020b2f6, 0x10020b825, 0x100b4bae5; editor-map.md.
inline constexpr std::uintptr_t effect_ctor = 0x20B820;
/// @brief 0x10020b8a6, 0x10020b98f, 0x10020b9c4; editor-map.md.
inline constexpr std::uintptr_t effect_execute = 0x20B830;
/// @brief Inventory evidence; editor-map.md.
inline constexpr std::uintptr_t edit_key_handler = 0x285E6D0;
/// @brief 0x102793b79, 0x102793b8f; editor-map.md.
inline constexpr std::uintptr_t edit_key_wrapper = 0x2793AD0;
/// @brief 0x10254881f, 0x10254883a, 0x1027722ea, 0x102772309, 0x10277240e, 0x102772470; editor-map.md.
inline constexpr std::uintptr_t find_edit = 0x27722D0;
/// @brief 0x10077aa63, 0x10077aae1, 0x102772129, 0x10277222e, 0x102772290; editor-map.md.
inline constexpr std::uintptr_t find_instant = 0x27720F0;
/// @brief 0x1000dfeb9, 0x10285e3e0, 0x10285e4a4; editor-map.md.
inline constexpr std::uintptr_t text_buffer_set = 0x285E4A0;
/// @brief 0x1000dfec0, 0x1000dfece, 0x102793bd4; editor-map.md.
inline constexpr std::uintptr_t edit_refresh = 0x2793F10;
/// @brief 0x1020ee387, 0x1020ee392, 0x1020ee3a2, 0x1020ee3b1; editor-map.md.
inline constexpr std::uintptr_t request_view_refresh = 0x20EE380;
/// @brief 0x102389144; editor-map.md.
inline constexpr std::uintptr_t observer_mark_dirty = 0x2389140;
/// @brief 0x10238915c, 0x10238915f; editor-map.md.
inline constexpr std::uintptr_t observer_consume = 0x2389150;
/// @brief 0x10077bd04, 0x1020f2590; editor-map.md.
inline constexpr std::uintptr_t idler_gui_getter = 0x77BD00;
/// @brief 0x101fc8e39, 0x101fc8e5d; editor-map.md.
inline constexpr std::uintptr_t view_is_visible = 0x1FC8E30;
/// @brief 0x101fc8f34; editor-map.md.
inline constexpr std::uintptr_t view_get_root = 0x1FC8F30;
/// @brief 0x101fa3c10, 0x101fa4324; editor-map.md.
inline constexpr std::uintptr_t observer_frame = 0x1FA4320;
/// @brief 0x101f9d6c0, 0x101f9fc14, 0x10238915c; editor-map.md.
inline constexpr std::uintptr_t observer_refresh = 0x1F9FC10;
/// @brief 0x1027c86cd, 0x1027c86dc, 0x1027c86e2, 0x1027c86f8; editor-map.md.
inline constexpr std::uintptr_t focus_switch = 0x27C86C0;
/// @brief 0x102794bd9, 0x102794be2, 0x102794c43; editor-map.md.
inline constexpr std::uintptr_t edit_unfocus = 0x2794BD0;
/// @brief 0x102794afb, 0x102794b0d, 0x102794ba0; editor-map.md.
inline constexpr std::uintptr_t edit_focus = 0x2794AF0;
/// @brief 0x102863eea, 0x102863ef1; editor-map.md.
inline constexpr std::uintptr_t focus_manager_rip_displacement = 0x2863EED;
/// @brief 0x102863eea; editor-map.md.
inline constexpr std::uintptr_t focus_manager_rip_next = 0x2863EF1;
/// @brief 0x103326868, 0x1033268b8; editor-map.md.
inline constexpr std::uintptr_t vt_sg_model = 0x3326868;
/// @brief 0x1033269b0, 0x103326a18; editor-map.md.
inline constexpr std::uintptr_t vt_sg_context = 0x33269b0;
/// @brief 0x100b46308, 0x1027664c0, 0x1034ac0b0, 0x1034ac6a0; editor-map.md.
inline constexpr std::uintptr_t vt_sg_root = 0x34AC0B0;
/// @brief 0x1034b1768, 0x1034b1a10; editor-map.md.
inline constexpr std::uintptr_t vt_instant_text = 0x34b1768;
/// @brief 0x1034add98, 0x1034ae0f8; editor-map.md.
inline constexpr std::uintptr_t vt_edit = 0x34add98;
/// @brief 0x1034ae058, 0x1034ae0f8; editor-map.md.
inline constexpr std::uintptr_t vt_text_buffer = 0x34ae058;
/// @brief 0x1027cee90, 0x102842ec4; editor-map.md.
inline constexpr std::uintptr_t vt_edit_definition = 0x34AE3E0;
/// @brief 0x1032bcb18, 0x1032be8f0; editor-map.md.
inline constexpr std::uintptr_t vt_execute_effect = 0x32bcb18;
/// @brief 0x103444508, 0x103444658; editor-map.md.
inline constexpr std::uintptr_t vt_politics_view = 0x3444508;
/// @brief 0x1020ee3b1, 0x103444600; editor-map.md.
inline constexpr std::uintptr_t vt_politics_observer = 0x3444600;
/// @brief 0x103308f90, 0x103309410; editor-map.md.
inline constexpr std::uintptr_t vt_ingame_idler = 0x3308f90;
/// @brief 0x1000e40b8, 0x102863eea, 0x102863ef1, 0x102863f07, 0x1035dc058; editor-map.md.
inline constexpr std::uintptr_t manager_global = 0x35DC058;
/// @brief 0x1024d72b9, 0x1024d72c0, 0x1024d72ca, 0x102a7a1c6; editor-map.md.
inline constexpr std::uintptr_t token_politics_tab = 0x35AF598;
/// @brief 0x1024d6c65, 0x1024d6c6c, 0x1024d6c76, 0x102a7a1c6; editor-map.md.
inline constexpr std::uintptr_t token_player_context = 0x35AE4B8;
/// @brief 0x1020e84bd, 0x10329c9c8, 0x103505780; editor-map.md.
inline constexpr std::uintptr_t gui_structural = 0x3505780;
}  // namespace capie::core::macos::rva

namespace capie::core::macos::off {
/// @brief 0x10095c142; layers-map.md.
inline constexpr std::uintptr_t country_variables = 0x1d8;
/// @brief 0x100b063c9; layers-map.md.
inline constexpr std::uintptr_t variables_entries = 0x10;
/// @brief 0x100b063b6; layers-map.md.
inline constexpr std::uintptr_t variables_mask = 0x1c;
/// @brief 0x100b0319c, 0x100b03314; layers-map.md.
inline constexpr std::uintptr_t variables_extra = 0x20;
/// @brief 0x100b063f7; layers-map.md.
inline constexpr std::uintptr_t variable_entry_size = 0x28;
/// @brief 0x100b063d6; layers-map.md.
inline constexpr std::uintptr_t variable_entry_probe = 0x4;
/// @brief 0x100b06400; layers-map.md.
inline constexpr std::uintptr_t variable_entry_key = 0x8;
/// @brief 0x100b03424; layers-map.md.
inline constexpr std::uintptr_t variable_entry_value = 0x20;
/// @brief 0x101060800, 0x10095d9bd; layers-map.md.
inline constexpr std::uintptr_t set_variable_name = 0xc0;
/// @brief 0x1011d7250; layers-map.md.
inline constexpr std::uintptr_t gamestate_countries = 0x2d8;
/// @brief 0x1006ee534, 0x10194aa63; layers-map.md.
inline constexpr std::uintptr_t gamestate_country_count = 0x2e4;
/// @brief 0x10194ad8a, 0x10194a146; layers-map.md.
inline constexpr std::uintptr_t flag_atlas_cosmetic_keys = 0xc0;
/// @brief 0x10194aa80; layers-map.md.
inline constexpr std::uintptr_t flag_atlas_cosmetic_count = 0xcc;
/// @brief 0x10194a4ac, 0x10194acf1; layers-map.md.
inline constexpr std::uintptr_t country_cosmetic_tag = 0x1200;
/// @brief 0x10194ac45, 0x10194ac51; layers-map.md.
inline constexpr std::uintptr_t country_tag = 0x8;
/// @brief 0x100db7460, 0x100db7464, 0x100db7467; layers-map.md.
inline constexpr std::uintptr_t faction_members = 0x48;
/// @brief 0x100e503d5, 0x100e4f96a; layers-map.md.
inline constexpr std::uintptr_t faction_member_count = 0x54;
/// @brief 0x100d6fd01; layers-map.md.
inline constexpr std::uintptr_t character_id = 0xc;
/// @brief 0x100d5df73; layers-map.md.
inline constexpr std::uintptr_t character_variables = 0xd0;
/// @brief 0x10117c434; layers-map.md.
inline constexpr std::uintptr_t country_politics = 0xd38;
/// @brief 0x100fbef1d; layers-map.md.
inline constexpr std::uintptr_t politics_ruling_party = 0xd0;
/// @brief 0x101366a7a; layers-map.md.
inline constexpr std::uintptr_t party_leaders = 0x60;
/// @brief 0x101366a74; layers-map.md.
inline constexpr std::uintptr_t party_leader_count = 0x6c;
/// @brief 0x101300d14; layers-map.md.
inline constexpr std::uintptr_t leader_role_character = 0x8;
/// @brief 0x1007581bb; layers-map.md.
inline constexpr std::uintptr_t portrait_entry_kind = 0x4;
/// @brief 0x1007581e2, 0x100d6fc21; layers-map.md.
inline constexpr std::uintptr_t portrait_entry_name = 0x8;
/// @brief 0x1007581cb, 0x100d6fcc2; layers-map.md.
inline constexpr std::uintptr_t portrait_entry_handle = 0x20;
/// @brief 0x100b44175, 0x100b45de9; editor-map.md.
inline constexpr std::uintptr_t window_idler = 0xD8;
/// @brief 0x100b45df0, 0x100b47d34; editor-map.md.
inline constexpr std::uintptr_t window_model = 0xE0;
/// @brief 0x100b45df7, 0x100b45ee7; editor-map.md.
inline constexpr std::uintptr_t window_manager = 0xE8;
/// @brief 0x100b462af, 0x100b46624; editor-map.md.
inline constexpr std::uintptr_t window_root = 0xF0;
/// @brief 0x100b44078, 0x100b46023; editor-map.md.
inline constexpr std::uintptr_t window_parent = 0xF8;
/// @brief 0x100b46123, 0x100b46150; editor-map.md.
inline constexpr std::uintptr_t window_parent_sg = 0x100;
/// @brief 0x100b443cb, 0x100b45dfe, 0x100b45e25; editor-map.md.
inline constexpr std::uintptr_t window_context = 0x110;
/// @brief 0x100b441f1, 0x100b4421e; editor-map.md.
inline constexpr std::uintptr_t window_parent_seen = 0xA50;
/// @brief 0x100b44307, 0x100b44425; editor-map.md.
inline constexpr std::uintptr_t window_desired = 0xA51;
/// @brief 0x100b45e20, 0x100b47dc8; editor-map.md.
inline constexpr std::uintptr_t context_window = 0x10;
/// @brief 0x100b47f79, 0x100b47f94; editor-map.md.
inline constexpr std::uintptr_t context_model = 0x20;
/// @brief 0x100b463d7, 0x100b46634; editor-map.md.
inline constexpr std::uintptr_t context_root = 0x28;
/// @brief 0x100b4b7f8, 0x100b4bae1; editor-map.md.
inline constexpr std::uintptr_t context_scope = 0x3C0;
/// @brief 0x100b4b873, 0x100b4bacf; editor-map.md.
inline constexpr std::uintptr_t context_records = 0x470;
/// @brief Inventory evidence; editor-map.md.
inline constexpr std::uintptr_t context_extent = 0x470;
/// @brief 0x100b47d40; editor-map.md.
inline constexpr std::uintptr_t model_name = 0x8;
/// @brief 0x100b4420e, 0x100b46739; editor-map.md.
inline constexpr std::uintptr_t model_context_token = 0x20;
/// @brief 0x100b45edb; editor-map.md.
inline constexpr std::uintptr_t model_parent_token = 0x24;
/// @brief 0x100b44092, 0x100b443bc, 0x101fc8e5d; editor-map.md.
inline constexpr std::uintptr_t root_flags = 0x95;
/// @brief 0x10077aa63, 0x10077aae1, 0x102772111; editor-map.md.
inline constexpr std::uintptr_t root_instant_list = 0xE20;
/// @brief 0x10276d7f3, 0x1027722f1; editor-map.md.
inline constexpr std::uintptr_t root_edit_list = 0xE80;
/// @brief 0x10020b30f; editor-map.md.
inline constexpr std::uintptr_t list_data = 0x0;
/// @brief 0x10020b591; editor-map.md.
inline constexpr std::uintptr_t list_capacity = 0x8;
/// @brief 0x10020b312; editor-map.md.
inline constexpr std::uintptr_t list_count = 0xC;
/// @brief 0x102842ec4, 0x102842ecc; editor-map.md.
inline constexpr std::uintptr_t edit_definition = 0x50;
/// @brief 0x100b46f50, 0x102793b5b; editor-map.md.
inline constexpr std::uintptr_t edit_visibility_flags = 0x65;
/// @brief 0x102793b79; editor-map.md.
inline constexpr std::uintptr_t edit_buffer = 0x70;
/// @brief 0x10285e4e3; editor-map.md.
inline constexpr std::uintptr_t edit_text = 0xA0;
/// @brief 0x102794ba0, 0x102794c43, 0x102794d94; editor-map.md.
inline constexpr std::uintptr_t edit_receiver = 0x180;
/// @brief 0x102794afb, 0x102794b0f; editor-map.md.
inline constexpr std::uintptr_t edit_flags = 0x239;
/// @brief 0x1027cee90, 0x1027cee94; editor-map.md.
inline constexpr std::uintptr_t definition_name = 0x28;
/// @brief 0x1027d0914, 0x1027d0930, 0x1027d094c, 0x1027d096c; editor-map.md.
inline constexpr std::uintptr_t instant_text = 0x100;
/// @brief 0x1027c86cd, 0x1027c86e2; editor-map.md.
inline constexpr std::uintptr_t manager_focus = 0x138;
/// @brief 0x1027c6439, 0x1027c8f39, 0x1027c8f81; editor-map.md.
inline constexpr std::uintptr_t manager_receiver = 0x210;
/// @brief 0x100b429c0, 0x100b429d1, 0x100b429f9, 0x100b429fc, 0x1033268e0; editor-map.md.
inline constexpr std::uintptr_t manager_idler = 0x18;
/// @brief 0x10269b1b7, 0x10269b1bb, 0x10269b1c5; editor-map.md.
inline constexpr std::size_t key_event_words = 8;
/// @brief 0x10269b154, 0x102793b74; editor-map.md.
inline constexpr std::uintptr_t key_wrapper_type = 0x74;
/// @brief 0x10020b2f9, 0x10020b30f; editor-map.md.
inline constexpr std::uintptr_t command_records = 0x28;
/// @brief 0x10020b591, 0x10020b600; editor-map.md.
inline constexpr std::uintptr_t command_scope_capacity = 0x48;
/// @brief 0x10020b584, 0x10020b649; editor-map.md.
inline constexpr std::uintptr_t command_scope_count = 0x4C;
/// @brief 0x10020b334, 0x10020b98f; editor-map.md.
inline constexpr std::uintptr_t command_selector = 0x58;
/// @brief 0x10020b3b1, 0x10020b8a6; editor-map.md.
inline constexpr std::uintptr_t command_model = 0x70;
/// @brief 0x10020b91e; editor-map.md.
inline constexpr std::uintptr_t effect_records = 0x28;
/// @brief 0x10077bd04, 0x100b46782; editor-map.md.
inline constexpr std::uintptr_t idler_gui = 0x670;
/// @brief 0x1020ee932, 0x1020ee946; editor-map.md.
inline constexpr std::uintptr_t gui_idler = 0x180;
/// @brief 0x1020f25f0; editor-map.md.
inline constexpr std::uintptr_t gui_views = 0xC8;
/// @brief 0x1020e84bd, 0x1020e8508, 0x1020ef2eb, 0x1020ef302; editor-map.md.
inline constexpr std::uintptr_t gui_structural = 0xB8;
/// @brief 0x1020ee3b1; editor-map.md.
inline constexpr std::uintptr_t view_observer = 0x28;
/// @brief 0x101fc88fa, 0x101fc892f, 0x101fc8937, 0x1020e939f, 0x10238865e; editor-map.md.
inline constexpr std::uintptr_t view_gui = 0x30;
/// @brief 0x102389144; editor-map.md.
inline constexpr std::uintptr_t view_dirty = 0x3C;
/// @brief 0x101f9824b, 0x101fc88fd, 0x101fc8aa9; editor-map.md.
inline constexpr std::uintptr_t view_idler = 0x428;
/// @brief 0x101fc8e39, 0x101fc8e56; editor-map.md.
inline constexpr std::uintptr_t view_root = 0x430;
/// @brief 0x101fc8e30; editor-map.md.
inline constexpr std::uintptr_t slot_view_visible = 0x30;
/// @brief 0x101fa4320; editor-map.md.
inline constexpr std::uintptr_t slot_observer_frame = 0x40;
/// @brief 0x101f9fc10, 0x10238915c; editor-map.md.
inline constexpr std::uintptr_t slot_observer_refresh = 0x50;
/// @brief 0x10077bd00, 0x100b4417f; editor-map.md.
inline constexpr std::uintptr_t slot_idler_gui = 0xD8;
/// @brief 0x102794af0; editor-map.md.
inline constexpr std::uintptr_t slot_edit_focus = 0xC0;
/// @brief 0x102794bd0; editor-map.md.
inline constexpr std::uintptr_t slot_edit_unfocus = 0xC8;
/// @brief 0x100b44084; editor-map.md.
inline constexpr std::uintptr_t parent_flags = 0x95;
/// @brief 0x100d5dd21 writer token0x4c29: embedded portrait map.
inline constexpr std::size_t character_portraits = 0x60;
/// @brief 0x102772470/0x102772290 load root's dummy-widget owner.
inline constexpr std::size_t root_dummy_owner = 0x88;
/// @brief 0x102772477 forms missing CEditBox address, not nullptr.
inline constexpr std::size_t dummy_edit = 0x1b8;
/// @brief 0x102772297 forms missing CInstantTextBox address, not nullptr.
inline constexpr std::size_t dummy_instant = 0x1c0;
}  // namespace capie::core::macos::off

namespace capie::core::macos::flag {
/// @brief 0x100b44092; editor-map.md.
inline constexpr unsigned char hidden_a = 0x04;
/// @brief 0x101fc8e64, 0x102793b5b; editor-map.md.
inline constexpr unsigned char shown = 0x08;
/// @brief 0x100b443bc; editor-map.md.
inline constexpr unsigned char hidden_b = 0x10;
/// @brief Inventory evidence; editor-map.md.
inline constexpr unsigned char hidden_any = 0x14;
/// @brief 0x102794b0d; editor-map.md.
inline constexpr unsigned char edit_active = 0x01;
/// @brief 0x102794b02; editor-map.md.
inline constexpr unsigned char edit_disabled = 0x02;
/// @brief Shared visibility predicate over the independently pinned macOS bits.
constexpr bool shown_not_hidden(unsigned char flags) noexcept {
    return (flags & shown) != 0 && (flags & hidden_any) == 0;
}
}  // namespace capie::core::macos::flag

namespace capie::core::macos::value {
/// @brief 0x1024d72ca; editor-map.md.
inline constexpr std::uint32_t token_politics_tab = 0x3AB2;
/// @brief 0x1024d6c76; editor-map.md.
inline constexpr std::uint32_t token_player_context = 0x3A70;
/// @brief 0x1020e93a2, 0x1020e93a7; editor-map.md.
inline constexpr int politics_view_index = 12;
/// @brief 0x1020ee387; editor-map.md.
inline constexpr int gui_view_limit = 0x17;
/// @brief 0x10285e7cb, 0x10285e7d9, 0x10285e89a, 0x102ef654c; editor-map.md.
inline constexpr std::uint32_t key_enter = 0xD;
/// @brief 0x10269b150, 0x102793b74; editor-map.md.
inline constexpr std::uint32_t wrapper_type_key = 1;
/// @brief 0x102793c81, 0x102793cdb; editor-map.md.
inline constexpr std::size_t inline_utf32_capacity = 4;
}  // namespace capie::core::macos::value
