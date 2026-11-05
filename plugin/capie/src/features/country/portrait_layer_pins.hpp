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
 * None of the ranges overlaps a DIR64 relocation (checked by tools/port/gen_tables.py on the 1.19.5.0 file).
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
    {"ui.size_small_is_zero", 0xB964F7, "83bc249000000000"},         // cmp dword [rsp+90],0 (0 -> GFX_idea_unknown)
    {"ui.character_argument", 0xB96538, "48897c2420"},               // mov [rsp+20],rdi: 5th argument = character
    {"ui.out_buffer", 0xB96548, "488d542430"},                       // lea rdx,[rsp+30]: caller's entry buffer
    {"resolve.character_argument", 0x143A03D, "488b5d77"},           // mov rbx,[rbp+77]: 5th argument
    {"resolve.character_id", 0x143A059, "448b430c"},                 // mov r8d,[rbx+0C]: character id
    {"valid.flag_byte", 0x1439276, "803900"},                        // cmp byte [rcx],0
    {"valid.kind", 0x143927F, "8b4104"},                             // mov eax,[rcx+4]
    {"valid.name", 0x143929D, "488d5108"},                           // lea rdx,[rcx+8]: sprite name
    {"character.write_portraits", 0x102AB95, "4c8d4678ba294c0000"},  // lea r8,[rsi+78]; mov edx,4C29
    {"character.write_variables", 0x102AE1B, "4c8d86d8000000ba4a2a0000"},         // lea r8,[rsi+D8]; mov edx,2A4A
    {"character.read_variables_token", 0x10296BB, "4181f84a2a0000"},              // cmp r8d,2A4A
    {"character.read_variables", 0x10297D6, "498d97d8000000"},                    // lea rdx,[r15+D8]
    {"leader.politics", core::rva::country_politics_getter, "488b81900f0000c3"},  // mov rax,[rcx+F90]; ret
    {"leader.ruling_party", 0x2FDCA7, "488b80d0000000"},                          // mov rax,[rax+D0]
    {"leader.first_role", core::rva::party_first_leader_role, "83797c00750333c0c3488b4970"},  // count +7C, roles +70
    {"leader.deref", core::rva::pointer_load, "488b01c3"},                                    // mov rax,[rcx]; ret
    {"leader.character", core::rva::role_character_getter, "488b4108c3"},                     // mov rax,[rcx+8]; ret
    {"effect.civilian_large", 0x2FDD24, "33d241b801000000"},                                  // type 0, size 1
};

/// @brief Call targets (rel32 displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"ui.calls_resolve", 0xB96551, 0xB96555, core::rva::portrait_resolve},
    {"ui.calls_valid", 0xB9655C, 0xB96560, core::rva::portrait_valid},
    {"leader.calls_politics", 0x2FDCA3, 0x2FDCA7, core::rva::country_politics_getter},
    {"leader.calls_first_role", 0x2FDCE1, 0x2FDCE5, core::rva::party_first_leader_role},
    {"leader.first_role_jumps", 0x123057E, 0x1230582, core::rva::pointer_load},
    {"leader.calls_character", 0x2FDCF2, 0x2FDCF6, core::rva::role_character_getter},
    {"effect.writes_portrait", 0x2FDD30, 0x2FDD34, core::rva::leader_portrait_write},
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

/// @brief Format 2 path support (API level 9): the builder the layer calls (research 3.1).
inline constexpr core::Signature path_signatures[] = {
    {"portrait.path_entry", core::SignatureGroup::country_hook, core::rva::portrait_path_entry,
     "48895C2410488974241857" "4883EC50410FB6F1488BDA488BF94584C0",
     "PathPortraitEntry prologue: out, path, warn (r8b), flag (r9b) (runtime catalogs research 3.1)"},
};

/// @brief The builder makes kind 1 through MakeIconEntry, which loads the texture and stores the handle.
inline constexpr core::InstructionPin path_instruction_pins[] = {
    {"path.kind_one", 0x14394CD, "ba01000000"},           // mov edx,1: kind 1
    {"icon.stores_kind", 0x1438B59, "895104"},            // mov [rcx+4],edx
    {"icon.kind_is_path", 0x1438B91, "83fa01"},           // cmp edx,1: only kind 1 loads
    {"icon.texmgr", 0x1438BD6, "488b9870010000"},         // mov rbx,[rax+170]: sprite db texture manager
    {"icon.stores_handle", 0x1438BFB, "49894628"},        // mov [r14+28],rax: texture handle
    {"valid.kind_path", 0x1439282, "83f801"},             // cmp eax,1
    {"valid.handle", 0x1439287, "4883792800"},            // cmp qword [rcx+28],0: kind 1 valid iff handle
};

/// @brief Call targets of the path support.
inline constexpr core::RipPin path_target_pins[] = {
    {"path.calls_make_icon", 0x14394D6, 0x14394DA, core::rva::make_icon_entry},
    {"icon.calls_add_texture", 0x1438BED, 0x1438BF1, core::rva::texture_add},
    {"icon.calls_get_texture", 0x1438BF7, 0x1438BFB, core::rva::texture_get},
};

/// @brief Expected path signatures.
inline constexpr std::size_t expected_path_signatures = 1;
/// @brief Expected path instruction pins.
inline constexpr std::size_t expected_path_instruction_pins = 7;
/// @brief Expected path target pins.
inline constexpr std::size_t expected_path_target_pins = 3;
static_assert(std::size(path_signatures) == expected_path_signatures &&
              std::size(path_instruction_pins) == expected_path_instruction_pins &&
              std::size(path_target_pins) == expected_path_target_pins);

/**
 * @brief Checks the pins of format 2 path support (its own switch inside the portrait layer).
 * @param text `.text` of the game image.
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_path_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), path_signatures,
                              path_instruction_pins, path_target_pins);
}

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
