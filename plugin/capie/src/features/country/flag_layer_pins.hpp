/**
 * @file flag_layer_pins.hpp
 * @brief Code-byte pins of the country flag layer (`capie_core.dll`), checked before its hooks go live.
 *
 * The flag layer hooks `UpdateFlagAtlas` (`rva::flag_atlas_update`), `LoadCountryFlag`
 * (`rva::load_country_flag`) and `BuildFlagCandidates` (`rva::build_flag_candidates`) and
 * writes one slot key of the flag atlas. Pinned here: the
 * three function prologues (design 1.7, review 1.1), the atlas key array and its count, the loop
 * over the game-state country list that indexes the slots (review 1.7, K9), the cosmetic-tag
 * argument of the candidate call (review K7) and the call sites (the 8-argument
 * `LoadCountryFlag` calls of the incremental and the threaded rebuild, review 1.6).
 *
 * The flag layer also needs the name layer's UpdateNames and set_variable hooks (its cache is
 * filled there), so it is activated only when the name-layer pins matched too. A failed pin
 * turns the flag layer off; the name layers and the localisation hooks keep working.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/signatures.hpp"
#include "name_layer_pins.hpp"

#include <array>
#include <cstddef>

namespace capie::country::flag::pins {

using core::uptr;

/// @brief Hooked functions.
inline constexpr core::Signature signatures[] = {
    {"flag.atlas_update", core::SignatureGroup::country_hook, core::rva::flag_atlas_update,
     "488bc448895808488950105556574154415541564157488d68a14881ecd00000000f2970b84c8bfa",
     "UpdateFlagAtlas prologue (design 1.2, review 1.5)"},
    {"flag.load_country_flag", core::SignatureGroup::country_hook, core::rva::load_country_flag,
     "44894c242048894c2408555356574154", "LoadCountryFlag prologue (design 1.3, review 1.4)"},
    {"flag.build_candidates", core::SignatureGroup::country_hook, core::rva::build_flag_candidates,
     "48895c241048896c2418488974242048894c2408574883ec50498bf9498bf0",
     "BuildFlagCandidates prologue: rdx tag, r8 cosmetic, r9 group (review 1.3)"},
};

/// @brief Offsets and call shapes, pinned in the game's own instructions.
inline constexpr layer::pins::InstructionPin instruction_pins[] = {
    {"atlas.cosmetic_keys", 0x12CE550, "498b86c0000000"},     // mov rax,[r14+C0]
    {"atlas.cosmetic_key_qword", 0x12CE557, "4a393cf8"},      // cmp [rax+r15*8],rdi (int64 per slot)
    {"atlas.cosmetic_count", 0x12CE21A, "413986cc000000"},    // cmp [r14+CC],eax (slot count)
    {"atlas.country_list", 0x12CE342, "488b8010030000"},      // mov rax,[rax+310]
    {"atlas.slot_is_list_index", 0x12CE349, "4e8b2cf8"},      // mov r13,[rax+r15*8]: slot r15 = list index
    {"load.cosmetic_argument", 0x12CD9DD, "4c8d8688140000"},  // lea r8,[rsi+1488] before the candidate call
    {"tag.index_is_int", 0xC08733, "8b01"},                   // tag -> index: mov eax,[rcx]
};

/// @brief Call targets (rel32 displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"load.calls_candidates", 0x12CD9EC, 0x12CD9F0, core::rva::build_flag_candidates},
    {"incremental.calls_load", 0x12CEC01, 0x12CEC05, core::rva::load_country_flag},
    {"threaded.calls_load", 0x12CB87F, 0x12CB883, core::rva::load_country_flag},
};

/// @brief Expected table sizes.
inline constexpr std::size_t expected_signatures = 3;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 7;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 3;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins);

/**
 * @brief Checks every flag-layer signature and pin (code bytes only).
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

}  // namespace capie::country::flag::pins
