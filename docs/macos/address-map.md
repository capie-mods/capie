# macOS address inventory

Target: HOI4 1.19.3.0, SHA-256 `09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.

**Research inventory, not an activation profile (`runtime_ready=false`).** Every one of the 164 constants in `game_layout.hpp` and 140 reference pin roles is retained in [address-inventory.json](address-inventory.json). All 140 pin roles now have static counterpart evidence or an explicit replacement: **137 static counterparts and 3 replacements**. This accounts for roles, not runtime readiness, safe hooks or complete API support.

| Reference group | Roles | Static counterparts | Explicit replacements | Evidence manifest |
|---|---:|---:|---:|---|
| Core country/editor/caption/focus | 56 | 56 | 0 | [core-pins.json](core-pins.json) |
| Country name/portrait/flag layers | 65 | 62 | 3 | [layers-pins.json](layers-pins.json) |
| Host pins | 19 | 19 | 0 | [host-pins.json](host-pins.json) |
| Total | 140 | 137 | 3 | [Per-role inventory](address-inventory.json) |

The three replacement roles are `faction.string_copy` and `faction.get_name_copy`
(explicit libc++ return construction), and `tag.index_is_int` (tag remapping before
array indexing). They are deliberately not claimed as native one-to-one equivalents.

**The 164 constants remain a separate inventory.** Current labels are 45 `proven`,
112 `static`, 5 `requires_adapter`, 2 `replaced_by_policy`, and no `candidate` records.
The three previous `unresolved` records now identify an implemented standalone
adapter or replacement guard, not invented native equivalents. `string_copy` still
has no native RVA; `root_third_list` and `root_readable_span` are superseded by the
four-array guard and its derived prefix. Their numeric mapping values remain null,
and runtime integration remains disabled. This does not prove all 164 constants.

The [editor manifest](editor-pins.json) remains a dependency of core coverage.
Its constructor-body companion, core supplemental instruction/dispatch records, and
additional layer evidence remain preserved and are not counted as extra Windows
roles. Runtime token DWORDs require post-initialization checks; repeated snippets
require their documented identity companions.

The tables below preserve the original identifiers. Values and instruction evidence are in the linked reports and JSON. `requires_adapter` means the Windows field representation cannot be transferred directly; `candidate` and `unresolved` must block the dependent feature.

| Windows identifier | macOS value | Evidence status | Report |
|---|---|---|---|
| `core::rva::loc_lookup_n` | `0x288da90` | proven | [localisation-coverage.md](localisation-coverage.md) |
| `core::rva::loc_lookup_z` | `0x288d420` | proven | [localisation-coverage.md](localisation-coverage.md) |
| `core::rva::loc_exists` | `0x288e4c0` | proven | [localisation-coverage.md](localisation-coverage.md) |
| `core::rva::update_names` | `0x117a790` | proven | [layers-map.md](layers-map.md) |
| `core::rva::build_name_key` | `0x1177f30` | proven | [layers-map.md](layers-map.md) |
| `core::rva::set_variable_execute` | `0x10607f0` | proven | [layers-map.md](layers-map.md) |
| `core::rva::variable_find` | `0xb031c0` | static (evidence only) | [layers-map.md](layers-map.md) |
| `core::rva::variable_get` | `0xb03190` | proven | [layers-map.md](layers-map.md) |
| `core::rva::country_of` | `0x11d7110` | proven | [layers-map.md](layers-map.md) |
| `core::rva::gamestate_global` | `0x3519740` | proven | [layers-map.md](layers-map.md) |
| `core::rva::faction_get_name` | `0xe527e0` | proven | [layers-map.md](layers-map.md) |
| `core::rva::flag_atlas_update` | `0x194a920` | proven | [layers-map.md](layers-map.md) |
| `core::rva::load_country_flag` | `0x194a460` | proven | [layers-map.md](layers-map.md) |
| `core::rva::build_flag_candidates` | `0x257750` | proven | [layers-map.md](layers-map.md) |
| `core::rva::string_copy` | no 1:1 native value | requires_adapter | [layers-map.md](layers-map.md) |
| `core::rva::portrait_resolve` | `0xd6fbe0` | proven | [layers-map.md](layers-map.md) |
| `core::rva::portrait_valid` | `0x7581a0` | proven | [layers-map.md](layers-map.md) |
| `core::rva::sprite_find` | `0x27b7b30` | proven | [layers-map.md](layers-map.md) |
| `core::rva::sprite_database_global` | `0x35dc028` | proven | [layers-map.md](layers-map.md) |
| `core::rva::vt_character` | `0x332efe0` | proven | [layers-map.md](layers-map.md) |
| `core::rva::window_tick` | `0xB44020` | static | [editor-map.md](editor-map.md) |
| `core::rva::window_bind` | `0xB45EC0` | static | [editor-map.md](editor-map.md) |
| `core::rva::window_dispatch` | `0xB4B7E0` | static | [editor-map.md](editor-map.md) |
| `core::rva::effect_ctor` | `0x20B820` | static | [editor-map.md](editor-map.md) |
| `core::rva::effect_execute` | `0x20B830` | static | [editor-map.md](editor-map.md) |
| `core::rva::edit_key_handler` | `0x285E6D0` | static | [editor-map.md](editor-map.md) |
| `core::rva::edit_key_wrapper` | `0x2793AD0` | static | [editor-map.md](editor-map.md) |
| `core::rva::find_edit` | `0x27722D0` | static | [editor-map.md](editor-map.md) |
| `core::rva::find_instant` | `0x27720F0` | static | [editor-map.md](editor-map.md) |
| `core::rva::text_buffer_set` | `0x285E4A0` | static | [editor-map.md](editor-map.md) |
| `core::rva::edit_refresh` | `0x2793F10` | static | [editor-map.md](editor-map.md) |
| `core::rva::request_view_refresh` | `0x20EE380` | static | [editor-map.md](editor-map.md) |
| `core::rva::observer_mark_dirty` | `0x2389140` | static | [editor-map.md](editor-map.md) |
| `core::rva::observer_consume` | `0x2389150` | static | [editor-map.md](editor-map.md) |
| `core::rva::idler_gui_getter` | `0x77BD00` | static | [editor-map.md](editor-map.md) |
| `core::rva::view_is_visible` | `0x1FC8E30` | static | [editor-map.md](editor-map.md) |
| `core::rva::view_get_root` | `0x1FC8F30` | static | [editor-map.md](editor-map.md) |
| `core::rva::observer_frame` | `0x1FA4320` | static | [editor-map.md](editor-map.md) |
| `core::rva::observer_refresh` | `0x1F9FC10` | static | [editor-map.md](editor-map.md) |
| `core::rva::focus_switch` | `0x27C86C0` | static | [editor-map.md](editor-map.md) |
| `core::rva::edit_unfocus` | `0x2794BD0` | static | [editor-map.md](editor-map.md) |
| `core::rva::edit_focus` | `0x2794AF0` | static | [editor-map.md](editor-map.md) |
| `core::rva::focus_manager_rip_displacement` | `0x2863EED` | static | [editor-map.md](editor-map.md) |
| `core::rva::focus_manager_rip_next` | `0x2863EF1` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_sg_model` | `0x3326868` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_sg_context` | `0x33269b0` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_sg_root` | `0x34AC0B0` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_instant_text` | `0x34b1768` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_edit` | `0x34add98` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_text_buffer` | `0x34ae058` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_edit_definition` | `0x34AE3E0` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_execute_effect` | `0x32bcb18` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_politics_view` | `0x3444508` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_politics_observer` | `0x3444600` | static | [editor-map.md](editor-map.md) |
| `core::rva::vt_ingame_idler` | `0x3308f90` | static | [editor-map.md](editor-map.md) |
| `core::rva::manager_global` | `0x35DC058` | static | [editor-map.md](editor-map.md) |
| `core::rva::token_politics_tab` | `0x35AF598` | static | [editor-map.md](editor-map.md) |
| `core::rva::token_player_context` | `0x35AE4B8` | static | [editor-map.md](editor-map.md) |
| `core::rva::gui_structural` | `0x3505780` | static | [editor-map.md](editor-map.md) |
| `core::off::country_variables` | `0x1d8` | proven | [layers-map.md](layers-map.md) |
| `core::off::variables_entries` | `0x10` | proven | [layers-map.md](layers-map.md) |
| `core::off::variables_mask` | `0x1c` | proven | [layers-map.md](layers-map.md) |
| `core::off::variables_extra` | `0x20` | proven | [layers-map.md](layers-map.md) |
| `core::off::variable_entry_size` | `0x28` | proven | [layers-map.md](layers-map.md) |
| `core::off::variable_entry_probe` | `0x4` | proven | [layers-map.md](layers-map.md) |
| `core::off::variable_entry_key` | `0x8` | proven | [layers-map.md](layers-map.md) |
| `core::off::variable_entry_value` | `0x20` | proven | [layers-map.md](layers-map.md) |
| `core::off::set_variable_name` | `0xc0` | proven | [layers-map.md](layers-map.md) |
| `core::off::gamestate_countries` | `0x2d8` | proven | [layers-map.md](layers-map.md) |
| `core::off::gamestate_country_count` | `0x2e4` | proven | [layers-map.md](layers-map.md) |
| `core::off::flag_atlas_cosmetic_keys` | `0xc0` | proven | [layers-map.md](layers-map.md) |
| `core::off::flag_atlas_cosmetic_count` | `0xcc` | proven | [layers-map.md](layers-map.md) |
| `core::off::country_cosmetic_tag` | `0x1200` | proven | [layers-map.md](layers-map.md) |
| `core::off::country_tag` | `0x8` | proven | [layers-map.md](layers-map.md) |
| `core::off::faction_members` | `0x48` | proven | [layers-map.md](layers-map.md) |
| `core::off::faction_member_count` | `0x54` | proven | [layers-map.md](layers-map.md) |
| `core::off::character_id` | `0xc` | proven | [layers-map.md](layers-map.md) |
| `core::off::character_variables` | `0xd0` | proven | [layers-map.md](layers-map.md) |
| `core::off::country_politics` | `0xd38` | proven | [layers-map.md](layers-map.md) |
| `core::off::politics_ruling_party` | `0xd0` | proven | [layers-map.md](layers-map.md) |
| `core::off::party_leaders` | `0x60` | proven | [layers-map.md](layers-map.md) |
| `core::off::party_leader_count` | `0x6c` | proven | [layers-map.md](layers-map.md) |
| `core::off::leader_role_character` | `0x8` | proven | [layers-map.md](layers-map.md) |
| `core::off::portrait_entry_kind` | `0x4` | proven | [layers-map.md](layers-map.md) |
| `core::off::portrait_entry_name` | `0x8` | proven | [layers-map.md](layers-map.md) |
| `core::off::portrait_entry_handle` | `0x20` | proven | [layers-map.md](layers-map.md) |
| `core::off::window_idler` | `0xD8` | static | [editor-map.md](editor-map.md) |
| `core::off::window_model` | `0xE0` | static | [editor-map.md](editor-map.md) |
| `core::off::window_manager` | `0xE8` | static | [editor-map.md](editor-map.md) |
| `core::off::window_root` | `0xF0` | static | [editor-map.md](editor-map.md) |
| `core::off::window_parent` | `0xF8` | static | [editor-map.md](editor-map.md) |
| `core::off::window_parent_sg` | `0x100` | static | [editor-map.md](editor-map.md) |
| `core::off::window_context` | `0x110` | static | [editor-map.md](editor-map.md) |
| `core::off::window_parent_seen` | `0xA50` | static | [editor-map.md](editor-map.md) |
| `core::off::window_desired` | `0xA51` | static | [editor-map.md](editor-map.md) |
| `core::off::context_window` | `0x10` | static | [editor-map.md](editor-map.md) |
| `core::off::context_model` | `0x20` | static | [editor-map.md](editor-map.md) |
| `core::off::context_root` | `0x28` | static | [editor-map.md](editor-map.md) |
| `core::off::context_scope` | `0x3C0` | static | [editor-map.md](editor-map.md) |
| `core::off::context_records` | `0x470` | static | [editor-map.md](editor-map.md) |
| `core::off::context_extent` | `0x470` | static | [editor-map.md](editor-map.md) |
| `core::off::model_name` | `0x8` | static | [editor-map.md](editor-map.md) |
| `core::off::model_context_token` | `0x20` | static | [editor-map.md](editor-map.md) |
| `core::off::model_parent_token` | `0x24` | static | [editor-map.md](editor-map.md) |
| `core::off::model_parent_window_len` | `unresolved` | requires_adapter | [editor-map.md](editor-map.md) |
| `core::off::model_parent_sg_len` | `unresolved` | requires_adapter | [editor-map.md](editor-map.md) |
| `core::off::root_flags` | `0x95` | static | [editor-map.md](editor-map.md) |
| `core::off::root_instant_list` | `0xE20` | static | [editor-map.md](editor-map.md) |
| `core::off::root_edit_list` | `0xE80` | static | [editor-map.md](editor-map.md) |
| `core::off::root_third_list` | no 1:1 native value | replaced_by_policy | [editor-map.md](editor-map.md) |
| `core::off::root_readable_span` | no 1:1 native value | replaced_by_policy | [editor-map.md](editor-map.md) |
| `core::off::list_data` | `0x0` | static | [editor-map.md](editor-map.md) |
| `core::off::list_capacity` | `0x8` | static | [editor-map.md](editor-map.md) |
| `core::off::list_count` | `0xC` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_definition` | `0x50` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_visibility_flags` | `0x65` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_buffer` | `0x70` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_text` | `0xA0` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_size` | `unresolved` | requires_adapter | [editor-map.md](editor-map.md) |
| `core::off::edit_capacity` | `unresolved` | requires_adapter | [editor-map.md](editor-map.md) |
| `core::off::edit_receiver` | `0x180` | static | [editor-map.md](editor-map.md) |
| `core::off::edit_flags` | `0x239` | static | [editor-map.md](editor-map.md) |
| `core::off::definition_name` | `0x28` | static | [editor-map.md](editor-map.md) |
| `core::off::instant_text` | `0x100` | static | [editor-map.md](editor-map.md) |
| `core::off::manager_focus` | `0x138` | static | [editor-map.md](editor-map.md) |
| `core::off::manager_receiver` | `0x210` | static | [editor-map.md](editor-map.md) |
| `core::off::manager_idler` | `0x18` | static | [editor-map.md](editor-map.md) |
| `core::off::key_event_words` | `8` | static | [editor-map.md](editor-map.md) |
| `core::off::key_wrapper_type` | `0x74` | static | [editor-map.md](editor-map.md) |
| `core::off::command_records` | `0x28` | static | [editor-map.md](editor-map.md) |
| `core::off::command_scope_capacity` | `0x48` | static | [editor-map.md](editor-map.md) |
| `core::off::command_scope_count` | `0x4C` | static | [editor-map.md](editor-map.md) |
| `core::off::command_selector` | `0x58` | static | [editor-map.md](editor-map.md) |
| `core::off::command_model` | `0x70` | static | [editor-map.md](editor-map.md) |
| `core::off::effect_records` | `0x28` | static | [editor-map.md](editor-map.md) |
| `core::off::idler_gui` | `0x670` | static | [editor-map.md](editor-map.md) |
| `core::off::gui_idler` | `0x180` | static | [editor-map.md](editor-map.md) |
| `core::off::gui_views` | `0xC8` | static | [editor-map.md](editor-map.md) |
| `core::off::gui_structural` | `0xB8` | static | [editor-map.md](editor-map.md) |
| `core::off::view_observer` | `0x28` | static | [editor-map.md](editor-map.md) |
| `core::off::view_gui` | `0x30` | static | [editor-map.md](editor-map.md) |
| `core::off::view_dirty` | `0x3C` | static | [editor-map.md](editor-map.md) |
| `core::off::view_idler` | `0x428` | static | [editor-map.md](editor-map.md) |
| `core::off::view_root` | `0x430` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_view_visible` | `0x30` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_observer_frame` | `0x40` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_observer_refresh` | `0x50` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_idler_gui` | `0xD8` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_edit_focus` | `0xC0` | static | [editor-map.md](editor-map.md) |
| `core::off::slot_edit_unfocus` | `0xC8` | static | [editor-map.md](editor-map.md) |
| `core::off::parent_flags` | `0x95` | static | [editor-map.md](editor-map.md) |
| `core::flag::hidden_a` | `0x04` | static | [editor-map.md](editor-map.md) |
| `core::flag::shown` | `0x08` | static | [editor-map.md](editor-map.md) |
| `core::flag::hidden_b` | `0x10` | static | [editor-map.md](editor-map.md) |
| `core::flag::hidden_any` | `0x14` | static | [editor-map.md](editor-map.md) |
| `core::flag::edit_active` | `0x01` | static | [editor-map.md](editor-map.md) |
| `core::flag::edit_disabled` | `0x02` | static | [editor-map.md](editor-map.md) |
| `core::value::token_politics_tab` | `0x3AB2` | static | [editor-map.md](editor-map.md) |
| `core::value::token_player_context` | `0x3A70` | static | [editor-map.md](editor-map.md) |
| `core::value::politics_view_index` | `12` | static | [editor-map.md](editor-map.md) |
| `core::value::gui_view_limit` | `0x17` | static | [editor-map.md](editor-map.md) |
| `core::value::key_enter` | `0xD` | static | [editor-map.md](editor-map.md) |
| `core::value::wrapper_type_key` | `1` | static | [editor-map.md](editor-map.md) |
| `core::value::inline_utf32_capacity` | `4` | static | [editor-map.md](editor-map.md) |

## Additional dependencies

The [console map](console-map.md), [layers map](layers-map.md), and [host manifest](host-pins.json) retain dependencies and limitations beyond the 164 constants. Host pin correspondence covers its 19 reference roles, not every runtime dependency or host authorization behavior. Existing `additional_dependencies` and `replacement_policies` remain unchanged in the inventory. Counts must not be interpreted as a runtime completeness percentage.

## Validation performed

This inventory update checked a bijection between all 140 reference roles and the three coverage manifests: core 56, layers 65, host 19, with no missing or duplicate role keys. Target hashes match. All 164 constant identifiers, additional dependencies and replacement policies were preserved. Exactly three constant statuses were subsequently clarified to reference the implemented adapter/policy, with null native values; the other 161 records remain unchanged. Supplemental evidence remains linked. These bookkeeping checks do not run the game or activate a dylib; standalone build/test results and their limits are recorded separately in [install-and-test.md](install-and-test.md).

## Variable lookup pin classification

`variable_find` is now static evidence, not an approved native call. The Windows declaration explicitly says “pinned, never called”; its macOS corroborating lookup is established by the exported prologue at `0x100b031c0`, caller `0x100b031a4`, table reads `0x100b03287`/`0x100b03294`, and value/fallback selection `0x100b03354`/`0x100b0335b`. The native return is a value pointer, not the Windows iterator. No callable ABI compatibility or adapter integration is claimed; `runtime_ready=false` remains in force.
