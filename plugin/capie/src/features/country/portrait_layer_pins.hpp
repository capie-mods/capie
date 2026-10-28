/**
 * @file portrait_layer_pins.hpp
 * @brief Code-byte pins of the leader portrait layer (`capie_core.dll`), checked before its hook goes live.
 *
 * "design" and "review" below are the portrait-layer design and review (session 2026-10-14). Pinned:
 * - the hooked `ResolvePortrait` (`rva::portrait_resolve`) and its only caller (the `ui.*` pins:
 *   the character as the fifth argument, the caller's buffer, size 0 = small);
 * - inside `ResolvePortrait` the character argument and its id at +0xC;
 * - the validity function `rva::portrait_valid` the hook calls (flag byte, kind, name) and its call
 *   by the GUI;
 * - the character layout as CCharacter::Write/Read use it (portraits +0x78 token 4C29,
 *   variables +0xD8 token 2A4A); the plain field getters are folded by the linker and
 *   prove nothing (review 1.2);
 * - the ruling-leader chain of the `set_country_leader_portrait` effect: politics
 *   +0xF90, ruling party +0xD0, first leader role (+0x70, count +0x7C), role +0x8 character,
 *   and its civilian large write (type 0, size 1).
 * None of the ranges overlaps a DIR64 relocation (checked by tools/port/gen_tables.py on the 1.19.4.0 file).
 *
 * The portrait layer needs the name layer's UpdateNames and set_variable hooks (its cache is
 * filled there), so it is activated only when the name-layer pins matched too. A failed pin
 * turns the portrait layer off; the other layers keep working.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::country::portrait::pins {

using core::uptr;

/// @brief Hooked and called functions.
inline constexpr core::Signature signatures[] = {
    {"portrait.resolve", core::SignatureGroup::country_hook, core::rva::portrait_resolve,
     "48895C2408488974241848895424105557415441564157488D6C24D9",
     "ResolvePortrait prologue: map, out, type, size, character (review 1.4)"},
    {"portrait.valid", core::SignatureGroup::country_hook, core::rva::portrait_valid,
     "40574883EC308039000F84????????8B4104", "PortraitEntryValid prologue: flag byte, kind (review 1.4)"},
};

/// @brief Argument layout, entry layout, character layout and leader chain, in the game's instructions.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"ui.size_small_is_zero", 0xB96487, "83bc249000000000"},         // cmp dword [rsp+90],0 (0 -> GFX_idea_unknown)
    {"ui.character_argument", 0xB964C8, "48897c2420"},               // mov [rsp+20],rdi: 5th argument = character
    {"ui.out_buffer", 0xB964D8, "488d542430"},                       // lea rdx,[rsp+30]: caller's entry buffer
    {"resolve.character_argument", 0x1439C0D, "488b5d77"},           // mov rbx,[rbp+77]: 5th argument
    {"resolve.character_id", 0x1439C29, "448b430c"},                 // mov r8d,[rbx+0C]: character id
    {"valid.flag_byte", 0x1438E46, "803900"},                        // cmp byte [rcx],0
    {"valid.kind", 0x1438E4F, "8b4104"},                             // mov eax,[rcx+4]
    {"valid.name", 0x1438E6D, "488d5108"},                           // lea rdx,[rcx+8]: sprite name
    {"character.write_portraits", 0x102AB25, "4c8d4678ba294c0000"},  // lea r8,[rsi+78]; mov edx,4C29
    {"character.write_variables", 0x102ADAB, "4c8d86d8000000ba4a2a0000"},         // lea r8,[rsi+D8]; mov edx,2A4A
    {"character.read_variables_token", 0x102964B, "4181f84a2a0000"},              // cmp r8d,2A4A
    {"character.read_variables", 0x1029766, "498d97d8000000"},                    // lea rdx,[r15+D8]
    {"leader.politics", core::rva::country_politics_getter, "488b81900f0000c3"},  // mov rax,[rcx+F90]; ret
    {"leader.ruling_party", 0x2FDC37, "488b80d0000000"},                          // mov rax,[rax+D0]
    {"leader.first_role", core::rva::party_first_leader_role, "83797c00750333c0c3488b4970"},  // count +7C, roles +70
    {"leader.deref", core::rva::pointer_load, "488b01c3"},                                    // mov rax,[rcx]; ret
    {"leader.character", core::rva::role_character_getter, "488b4108c3"},                     // mov rax,[rcx+8]; ret
    {"effect.civilian_large", 0x2FDCB4, "33d241b801000000"},                                  // type 0, size 1
};

/// @brief Call targets (rel32 displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"ui.calls_resolve", 0xB964E1, 0xB964E5, core::rva::portrait_resolve},
    {"ui.calls_valid", 0xB964EC, 0xB964F0, core::rva::portrait_valid},
    {"leader.calls_politics", 0x2FDC33, 0x2FDC37, core::rva::country_politics_getter},
    {"leader.calls_first_role", 0x2FDC71, 0x2FDC75, core::rva::party_first_leader_role},
    {"leader.first_role_jumps", 0x123050E, 0x1230512, core::rva::pointer_load},
    {"leader.calls_character", 0x2FDC82, 0x2FDC86, core::rva::role_character_getter},
    {"effect.writes_portrait", 0x2FDCC0, 0x2FDCC4, core::rva::leader_portrait_write},
};

/// @brief Expected signatures.
inline constexpr std::size_t expected_signatures = 2;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 18;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 7;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins);

/**
 * @brief Checks every portrait-layer signature and pin (code bytes only).
 * @param text `.text` of the game image (the image base is `text.base`).
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), signatures,
                              instruction_pins, target_pins);
}

}  // namespace capie::country::portrait::pins
