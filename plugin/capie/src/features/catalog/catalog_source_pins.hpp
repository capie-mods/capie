/**
 * @file catalog_source_pins.hpp
 * @brief Pins of the flag and portrait catalog sources (`catalog_sources.hpp`), checked before use.
 *
 * - `GetFiles` (`rva::vfs_get_files`) and one of its callers, which shows the argument order and the
 *   result array: data `+0`, count `+0xC`, the shared allocator `+0x10` (`rva::gui_structural`), and
 *   0x40-byte entries;
 * - the sprite map of FindSprite: the thunk adds 0x100 to the database, bucket count `+0x4`,
 *   buckets `+0x8`, node `{object, next +0x8}`, sprite name `+0x10`;
 * - the scope's country (`+0x8`, written by the scope's country setter, read by the GUI updater at
 *   context `+0x3C8`) and the tag text array `[gamestate + 0x358]`, stride 0x20 (`TagText`).
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::catalog::pins {

/// @brief The listing function.
inline constexpr core::Signature source_signatures[] = {
    {"files.get_files", core::SignatureGroup::country_hook, core::rva::vfs_get_files,
     "48895C24084889742418" "48897C2420554154415541564157488BEC4881EC80000000" "4D8BF94D8BE0488BF24C8BF18B420C",
     "GetFiles prologue: dir, out, ends_with, contains, depth (runtime catalogs research 5)"},
};

/// @brief Layouts in the game's instructions.
inline constexpr core::InstructionPin source_instruction_pins[] = {
    {"files.allocator_slot", 0x146808, "4889442440"},      // mov [rsp+40],rax: array +0x10 = allocator
    {"files.array", 0x14682E, "488d542430"},               // lea rdx,[rsp+30]: the array
    {"files.depth", 0x146823, "44897c2420"},               // mov [rsp+20],r15d: fifth argument
    {"files.count", 0x14683D, "486344243c"},               // movsxd rax,[rsp+3C]: array +0xC = count
    {"files.stride", 0x146845, "49c1e606"},                // shl r14,6: 0x40-byte entries
    {"files.clears", 0x2560AF5, "44896e0c"},               // mov [rsi+C],r13d: count reset
    {"map.thunk", 0x22A0ED0, "4881c100010000"},            // add rcx,100: the map inside the database
    {"map.bucket_count", 0xA88D5A, "f77304"},              // div dword [rbx+4]
    {"map.buckets", 0xA88D5D, "488b5b08488b1cd3"},         // mov rbx,[rbx+8]; mov rbx,[rbx+rdx*8]
    {"map.node_object", 0xA88D80, "488b33"},               // mov rsi,[rbx]
    {"map.sprite_name", 0xA88D86, "488d4e10"},             // lea rcx,[rsi+10]
    {"map.node_next", 0xA88DB4, "488b5b08"},               // mov rbx,[rbx+8]
    {"scope.country_read", 0xBDF353, "488d97c8030000"},    // lea rdx,[rdi+3C8]: context scope +0x8
    {"scope.country_write", 0x58191A, "8b07894308"},       // mov eax,[rdi]; mov [rbx+8],eax
    {"tag.text_array", 0xC081BA, "48630348c1e00548038158030000"},  // movsxd; shl 5; add rax,[rcx+358]
};

/// @brief Call and global targets.
inline constexpr core::RipPin source_target_pins[] = {
    {"files.allocator", 0x146804, 0x146808, core::rva::gui_structural},
    {"files.calls_get_files", 0x146834, 0x146838, core::rva::vfs_get_files},
    {"map.thunk_jumps", 0x22A0ED8, 0x22A0EDC, core::rva::sprite_map_find},
    {"scope.calls_set_country", 0xBDF365, 0xBDF369, core::rva::scope_set_country},
};

/// @brief Expected count.
inline constexpr std::size_t expected_source_pins = 1 + 15 + 4;
static_assert(std::size(source_signatures) + std::size(source_instruction_pins) + std::size(source_target_pins) ==
              expected_source_pins);

/**
 * @brief Checks the catalog source pins.
 * @param text `.text` of the game image.
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_source_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<core::uptr>(text.base), source_signatures,
                              source_instruction_pins, source_target_pins);
}

}  // namespace capie::catalog::pins
