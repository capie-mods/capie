/**
 * @file colour_layer_pins.hpp
 * @brief Code-byte pins of the country colour layer (`capie_core.dll`), checked before its hook goes live.
 *
 * Evidence: `docs/research/country-colour-2026-10-28.md` and the implementation spec
 * (`docs/contributing/country-colour-layer-spec.md` section 7), re-read in Ghidra on 2026-10-29
 * (1.19.4.0); unchanged on 1.19.4.1 (`docs/research/port-1.19.4.1.md`, section "API level 8").
 * Pinned:
 * - the hooked getter `GetMapColors` (`rva::country_map_colors`, overlord walk through +0xF88 and
 *   +0x3B8, result `lea rax,[rbx+0x350]`) and the called `RefreshCountryColor`
 *   (`rva::refresh_country_color`: map +8, the tag argument, faction colour copy from +0x20);
 * - the block layout as `SetColors` copies it (+0x20 colour floats, +0x40 UI floats, +0x50 flags);
 * - the game's own guard before its refresh in `SetCosmeticTag`: current idler global, its slot
 *   0x2D8 (the `CInGameIdler` and base implementations, compared by address), the map getter and
 *   the call of the refresh with `lea rdx,[rdi+8]`;
 * - the map-mode dirty bytes: the manager's mode table at +0x30, byte +0x48 per mode, and the
 *   update loop's `cmp edi,23`;
 * - the modes the layer marks (`colour_map_modes`): the rebuild switch's bound (`cmp edi,22`), its
 *   jump-table entries for the political, claims and continent modes (each pin's address is the
 *   table plus four times the `value::` mode constant, so a wrong constant fails the pin), the tail
 *   jumps of those cases to their builders, the political builder's and the claims colour
 *   reader's calls of `GetMapColors`, and the continent builder's load of its
 *   "Getting continent for %d" log format (the mode CAPIE never marks);
 * - the six `NGraphics` colour defines as the `CCountryColors` token reader loads them, and which
 *   set feeds `color_ui` (`lea rcx,[rbx+0x30]`) and `color`.
 * None of the ranges overlaps a DIR64 relocation; the slot pin is read at the image base.
 *
 * The colour layer needs the name layer's UpdateNames and set_variable hooks (its slots are filled
 * there), so it is activated only when the name-layer pins matched too. It also needs its RTTI
 * table (`core::colour_vtable_classes`). A failed pin turns the colour layer off; the other layers
 * keep working.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::country::colour::pins {

using core::uptr;

/// @brief Jump table of the 23-way map-mode rebuild switch (int32 case RVAs, indexed by mode).
inline constexpr uptr rebuild_mode_table = 0xFB81EC;
/// @brief Bytes per entry of `rebuild_mode_table`.
inline constexpr uptr rebuild_entry_bytes = 4;

/// @brief Hooked and called functions.
inline constexpr core::Signature signatures[] = {
    {"colour.map_colors", core::SignatureGroup::country_hook, core::rva::country_map_colors,
     "40534883ec20488bd90f1f8000000000488b83880f0000488b80b80300004885",
     "GetMapColors prologue + subject walk (+F88, +3B8)"},
    {"colour.refresh", core::SignatureGroup::country_hook, core::rva::refresh_country_color,
     "40574883ec20488b4908488bfae8", "RefreshCountryColor prologue: map+8, rdx = tag"},
};

/// @brief Block layout, guard, dirty bytes and define order, in the game's instructions.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"getter.block_offset", 0x734B38, "488d8350030000"},  // lea rax,[rbx+350]
    {"colors.layout", 0x759340,
     "0f1042200f1181700300000f1042400f1181900300000fb642508881a0030000c3"},  // SetColors: +20, +40, +50
    {"cosmetic.idler_slot", 0x75950E, "ff90d8020000"},                           // call [rax+2D8]
    {"cosmetic.tag_argument", 0x75951D, "488d5708"},                             // lea rdx,[rdi+8]
    {"refresh.map_manager", 0xAB7F16, "488b4908"},                               // mov rcx,[rcx+8]
    {"refresh.faction_copy", 0xAB7F81, "0f1040200f1183e0090000"},               // colour +20 -> faction +9E0
    {"mark.modes", 0xE86C20, "488b7930"},                                        // mov rdi,[rcx+30]
    {"mark.mode0_dirty", 0xE86C48, "c6404801"},                                  // mov byte [rax+48],1
    {"update.mode_dirty", 0xFBF7A0, "41807c1e4800"},                             // cmp byte [r14+rbx+48],0
    {"update.mode_count", 0xFBF819, "83ff17"},                                   // cmp edi,23
    {"idler.ingame_true", core::rva::idler_in_game_true, "b001c3"},              // CInGameIdler slot 2D8
    {"idler.default_false", core::rva::idler_in_game_false, "32c0c3"},           // base idler slot 2D8
    {"define.ui_target", 0x752D59, "488d4b30"},                                  // lea rcx,[rbx+30]: color_ui
    {"define.map_target", 0x752DAB, "488d4b10"},                                 // lea rcx,[rbx+10]: color
    {"rebuild.mode_bound", 0xFB7F99, "83ff16"},                                  // cmp edi,22
    {"rebuild.case_political", rebuild_mode_table + (rebuild_entry_bytes * core::value::political_map_mode),
     "b77ffb00"},  // case of mode 0
    {"rebuild.case_claims", rebuild_mode_table + (rebuild_entry_bytes * core::value::claims_map_mode),
     "f180fb00"},  // case of mode 19
    {"rebuild.case_continent", rebuild_mode_table + (rebuild_entry_bytes * core::value::continent_map_mode),
     "1581fb00"},  // case of mode 21
};

/// @brief Call and data targets (rel32 / RIP displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"cosmetic.calls_map", 0x759519, 0x75951D, 0xAB7E30},
    {"cosmetic.calls_refresh", 0x759525, 0x759529, core::rva::refresh_country_color},
    {"refresh.calls_getter", 0xAB7F7D, 0xAB7F81, core::rva::country_map_colors},
    {"cosmetic.idler_global", 0x759502, 0x759506, core::rva::current_idler},
    {"map.global", 0xAB7E33, 0xAB7E37, core::rva::map_singleton},
    {"define.ui_value", 0x752D3B, 0x752D3F, core::rva::define_ui_color_value},
    {"define.ui_saturation", 0x752D48, 0x752D4C, core::rva::define_ui_color_saturation},
    {"define.ui_hue", 0x752D50, 0x752D54, core::rva::define_ui_color_hue},
    {"define.value", 0x752D8D, 0x752D91, core::rva::define_color_value},
    {"define.saturation", 0x752D9A, 0x752D9E, core::rva::define_color_saturation},
    {"define.hue", 0x752DA2, 0x752DA6, core::rva::define_color_hue},
    {"rebuild.political_builder", 0xFB7FC5, 0xFB7FC9, 0xFB8250},
    {"rebuild.claims_builder", 0xFB80FF, 0xFB8103, 0xFBAE80},
    {"rebuild.continent_builder", 0xFB8123, 0xFB8127, 0xFB9FB0},
    {"political.calls_getter", 0xFB8795, 0xFB8799, core::rva::country_map_colors},
    {"claims.calls_colour", 0xFBB0AB, 0xFBB0AF, 0xFB4780},
    {"claims_colour.calls_getter", 0xFB48D9, 0xFB48DD, core::rva::country_map_colors},
    {"continent.log_format", 0xFBA440, 0xFBA444, 0x29F5A58},
};

/// @brief Vtable slots (read at the image base: the file holds preferred-base pointers).
inline constexpr core::NamedSlotPin slot_pins[] = {
    {"idler.ingame_slot", {core::rva::vt_ingame_idler, core::off::idler_in_game_slot, core::rva::idler_in_game_true}},
};

/// @brief Expected signatures.
inline constexpr std::size_t expected_signatures = 2;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 18;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 18;
/// @brief Expected slot pins.
inline constexpr std::size_t expected_slot_pins = 1;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins && std::size(slot_pins) == expected_slot_pins);

/**
 * @brief Checks every colour-layer signature and pin.
 * @tparam M Memory policy (instruction bytes, displacements, vtable slots).
 * @param text `.text` of the game image (signatures).
 * @param mem Memory policy.
 * @param image Image base the slot pointers are relocated to.
 * @return Null when all pass, else the id of the first failed check.
 */
template <class M>
const char* first_failed_pin(const core::TextSection& text, const M& mem, uptr image) noexcept {
    return core::first_failed(text, mem, image, signatures, instruction_pins, target_pins, slot_pins);
}

}  // namespace capie::country::colour::pins
