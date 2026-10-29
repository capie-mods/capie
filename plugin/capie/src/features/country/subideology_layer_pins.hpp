/**
 * @file subideology_layer_pins.hpp
 * @brief Code-byte pins of the ruling-party ideology name layer (`capie_core.dll`), checked before its hook goes live.
 *
 * Evidence: `docs/research/subideology-rename-design.md`, re-read in Ghidra on 2026-10-29. Pinned:
 * - the hooked writer (`rva::politics_panel`): its prologue, the shown tag through the view's
 *   idler (+0x5F0, slot 0xA0), the politics getter, the ruling party at +0xD0, the ideology object
 *   at party+0x18 and the call that localises its token, then the first leader role;
 * - the localising helper: the token at object+0x18 (capacity +0x30) and its call of the
 *   localiser, whose formatter calls `rva::loc_lookup_n` (the lookup CAPIE hooks);
 * - the layouts behind the two offsets: the `CPoliticalParty` constructor stores its ideology at
 *   +0x18; the `CIdeologyGroup` constructor builds the token string at +0x18;
 * - the idler's shown tag: `CInGameIdler` slot 0xA0 is the function that returns
 *   `gamestate + (gs[0x520] > 0 ? 0x520 : 0x524)`;
 * - the ruling-leader chain (as the portrait layer pins it): politics +0xF90, first leader role
 *   (+0x70, count +0x7C), the role's character at +0x8.
 * None of the ranges overlaps a DIR64 relocation; the slot pin is read at the image base.
 *
 * The layer needs the name layer's UpdateNames and set_variable hooks and the always-installed
 * `lookup_n` hook, so it is activated only when the name-layer pins matched too. It also needs its
 * RTTI table (`core::subideology_vtable_classes`). A failed pin turns this layer off; the other
 * layers keep working.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::country::subideology::pins {

using core::uptr;

/// @brief Hooked function.
inline constexpr core::Signature signatures[] = {
    {"subideology.panel", core::SignatureGroup::country_hook, core::rva::politics_panel,
     "488bc4488958104889701848897820554154415541564157488d68a14881ec900000000f2970c84c8bf9488b89f0050000",
     "ruling-party panel prologue up to mov rcx,[rcx+5F0] (no RIP operand; unique only with the tail)"},
};

/// @brief Offsets, the shown tag and the leader chain, in the game's instructions.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"panel.idler_tag", 0x1622BBA, "488b89f0050000488b01ff90a0000000"},  // mov rcx,[rcx+5F0]; call [rax+A0]
    {"panel.ruling_party", 0x1622BE0, "4c8bb0d0000000"},                  // mov r14,[rax+D0]
    {"panel.ideology_object", 0x1622C65, "498b4e18"},                     // mov rcx,[r14+18]
    {"ideology.key", 0x66A4E6, "48837930" "0f488bda488d5118"},             // cmp [rcx+30],F; lea rdx,[rcx+18]
    {"party.ctor_ideology", 0x12302BC, "48895e1048897e18"},               // mov [rsi+10],rbx; mov [rsi+18],rdi
    {"ideology_group.ctor_key", 0x121D992, "488d4e18"},                   // lea rcx,[rsi+18]: token string
    {"idler.shown_tag", 0xB8E373, "b824050000ba2005000083b920050000000f4fc2"},  // 524 / 520, cmp [rcx+520],0
    {"leader.politics", core::rva::country_politics_getter, "488b81900f0000c3"},  // mov rax,[rcx+F90]; ret
    {"leader.first_role", core::rva::party_first_leader_role, "83797c00750333c0c3488b4970"},  // count +7C, roles +70
    {"leader.deref", core::rva::pointer_load, "488b01c3"},                                    // mov rax,[rcx]; ret
    {"leader.character", core::rva::role_character_getter, "488b4108c3"},                     // mov rax,[rcx+8]; ret
};

/// @brief Call and data targets (rel32 / RIP displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"panel.calls_politics", 0x1622BD9, 0x1622BDD, core::rva::country_politics_getter},
    {"panel.calls_localise", 0x1622C6A, 0x1622C6E, 0x66A4E0},
    {"panel.calls_first_role", 0x1622C7D, 0x1622C81, core::rva::party_first_leader_role},
    {"localise.calls_formatter", 0x66A501, 0x66A505, 0x22AE6F0},
    {"formatter.calls_lookup", 0x22AE0FA, 0x22AE0FE, core::rva::loc_lookup_n},
    {"idler.shown_tag_state", 0xB8E36F, 0xB8E373, core::rva::gamestate_global},
    {"leader.first_role_jumps", 0x123050E, 0x1230512, core::rva::pointer_load},
};

/// @brief Vtable slots (read at the image base: the file holds preferred-base pointers).
inline constexpr core::NamedSlotPin slot_pins[] = {
    {"idler.shown_tag_slot", {core::rva::vt_ingame_idler, core::off::slot_idler_shown_tag, core::rva::idler_shown_tag}},
};

/// @brief Expected signatures.
inline constexpr std::size_t expected_signatures = 1;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 11;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 7;
/// @brief Expected slot pins.
inline constexpr std::size_t expected_slot_pins = 1;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins && std::size(slot_pins) == expected_slot_pins);

/**
 * @brief Checks every pin of the layer.
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

}  // namespace capie::country::subideology::pins
