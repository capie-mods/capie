/**
 * @file portrait_layer_pins.hpp
 * @brief Code-byte pins of the leader portrait layer (`capie_core.dll`), checked before its hook goes live.
 *
 * Evidence: monorepo archive: docs/evidence/session-20261014/portrait-layer/design.txt and review.txt. Pinned:
 * - the hooked `ResolvePortrait` (139DF20) and its only caller B44490 (call at B44520, the
 *   character as the fifth argument, the caller's buffer, size 0 = small);
 * - inside 139DF20 the character argument and its id at +0xC;
 * - the validity function 139D1E0 the hook calls (flag byte, kind, name) and its call by the GUI;
 * - the character layout as CCharacter::Write/Read use it (portraits +0x78 token 4C29,
 *   variables +0xD8 token 2A4A); the getters 0xCE9BA0 / 0xF9F9F0 are folded by the linker and
 *   prove nothing (review 1.2);
 * - the ruling-leader chain of the `set_country_leader_portrait` effect (2E5680): politics
 *   +0xF90, ruling party +0xD0, first leader role (+0x70, count +0x7C), role +0x8 character,
 *   and its civilian large write (type 0, size 1).
 * None of the ranges overlaps a DIR64 relocation (checked with recore.py on the 1.19.3.0 file).
 *
 * The portrait layer needs the name layer's UpdateNames and set_variable hooks (its cache is
 * filled there), so it is activated only when the name-layer pins matched too. A failed pin
 * turns the portrait layer off; the other layers keep working.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/signatures.hpp"
#include "name_layer_pins.hpp"

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
inline constexpr layer::pins::InstructionPin instruction_pins[] = {
    {"ui.size_small_is_zero", 0xB444C7, "83bc249000000000"},        // cmp dword [rsp+90],0 (0 -> GFX_idea_unknown)
    {"ui.character_argument", 0xB44508, "48897c2420"},              // mov [rsp+20],rdi: 5th argument = character
    {"ui.out_buffer", 0xB44518, "488d542430"},                      // lea rdx,[rsp+30]: caller's entry buffer
    {"resolve.character_argument", 0x139DFAD, "488b5d77"},          // mov rbx,[rbp+77]: 5th argument
    {"resolve.character_id", 0x139DFC9, "448b430c"},                // mov r8d,[rbx+0C]: character id
    {"valid.flag_byte", 0x139D1E6, "803900"},                       // cmp byte [rcx],0
    {"valid.kind", 0x139D1EF, "8b4104"},                            // mov eax,[rcx+4]
    {"valid.name", 0x139D20D, "488d5108"},                          // lea rdx,[rcx+8]: sprite name
    {"character.write_portraits", 0xFA6605, "4c8d4678ba294c0000"},  // lea r8,[rsi+78]; mov edx,4C29
    {"character.write_variables", 0xFA688B, "4c8d86d8000000ba4a2a0000"},  // lea r8,[rsi+D8]; mov edx,2A4A
    {"character.read_variables_token", 0xFA5127, "4181f84a2a0000"},       // cmp r8d,2A4A
    {"character.read_variables", 0xFA5246, "498d97d8000000"},             // lea rdx,[r15+D8]
    {"leader.politics", 0x6CF890, "488b81900f0000c3"},                    // mov rax,[rcx+F90]; ret
    {"leader.ruling_party", 0x2E56B7, "488b80d0000000"},                  // mov rax,[rax+D0]
    {"leader.first_role", 0x11A3300, "83797c00750333c0c3488b4970"},       // count +7C, roles +70
    {"leader.deref", 0x1AD6E0, "488b01c3"},                               // mov rax,[rcx]; ret
    {"leader.character", 0x2AA270, "488b4108c3"},                         // mov rax,[rcx+8]; ret
    {"effect.civilian_large", 0x2E5734, "33d241b801000000"},              // type 0, size 1
};

/// @brief Call targets (rel32 displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"ui.calls_resolve", 0xB44521, 0xB44525, core::rva::portrait_resolve},
    {"ui.calls_valid", 0xB4452C, 0xB44530, core::rva::portrait_valid},
    {"leader.calls_politics", 0x2E56B3, 0x2E56B7, 0x6CF890},
    {"leader.calls_first_role", 0x2E56F1, 0x2E56F5, 0x11A3300},
    {"leader.first_role_jumps", 0x11A330E, 0x11A3312, 0x1AD6E0},
    {"leader.calls_character", 0x2E5702, 0x2E5706, 0x2AA270},
    {"effect.writes_portrait", 0x2E5740, 0x2E5744, 0x139F140},
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
    const auto image = reinterpret_cast<uptr>(text.base);
    const core::ProcessMemory reader{};
    for (const auto& signature : signatures) {
        if (!core::verify_signature(text, signature)) {
            return signature.id;
        }
    }
    for (const auto& pin : instruction_pins) {
        if (!layer::pins::verify_instruction(image, pin)) {
            return pin.id;
        }
    }
    for (const auto& pin : target_pins) {
        if (!core::verify_rip_pin(reader, image, pin)) {
            return pin.id;
        }
    }
    return nullptr;
}

}  // namespace capie::country::portrait::pins
