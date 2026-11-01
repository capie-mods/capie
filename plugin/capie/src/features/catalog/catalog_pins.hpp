/**
 * @file catalog_pins.hpp
 * @brief Code-byte pins of the runtime catalogs (API level 9, `capie_editor.dll`), checked before use.
 *
 * Two groups, each with its own switch:
 *
 * - **VP view** (`vp_*`): the scope's state id as the GUI updater copies it into a new scope, the
 *   state array by id, a state's province array and count, a province's VP value, id and custom
 *   name, the game's `ProvinceName` (called), the game's `_tls_index` and the `ThreadForbidCount`
 *   offset (read), and the inlined string release of `MakeIconEntry` that `guarded_string_release`
 *   copies (big-block threshold, extra bytes, sanity check, and the call of the game's
 *   `operator delete`).
 * - **Slot sprites** (`sprite_*`, used by the spikes S1/S2 and later by the flag and portrait
 *   catalogs): the `CSpriteType` lazy loader writes the texture id, texture and size with
 *   AddTexture/GetTexture/TextureSize (runtime catalogs research 3.6), the sprite database global
 *   and FindSprite, and `ReleaseTexture` (texture-release research 2026-11, section 4.1).
 *
 * The RTTI classes the code compares objects against are in `core::catalog_vtable_classes`.
 * Pins read on 1.19.4.1 with capstone; none overlaps a DIR64 relocation.
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

using core::uptr;

/// @brief Functions the VP view calls.
inline constexpr core::Signature vp_signatures[] = {
    {"vp.province_name", core::SignatureGroup::country_hook, core::rva::province_name,
     "48895C24084889742410574883EC50498BF0488BDA488BF9", "ProvinceName prologue: province, out, is_vp"},
};

/// @brief State, province, scope and thread-guard layout in the game's instructions.
inline constexpr core::InstructionPin vp_instruction_pins[] = {
    {"scope.state_id", 0xBDE7F8, "8b9368040000"},           // mov edx,[rbx+468]: context scope +3C0, state id +A8
    {"scope.set_state", 0x58295F, "89b9a8000000"},          // mov [rcx+A8],edi
    {"state.count", 0x3E8FDD, "3b81d4020000"},              // cmp eax,[rcx+2D4]: state slots
    {"state.array", 0x3E8FE8, "488b81c80200004c8b04d0"},    // mov rax,[rcx+2C8]; mov r8,[rax+rdx*8]
    {"state.province_count", 0xA2A17A, "8b7924"},           // mov edi,[rcx+24]
    {"state.provinces", 0xA2A18B, "4c8b5918"},              // mov r11,[rcx+18]
    {"province.vp_read", 0xA2A197, "448b4038"},             // mov r8d,[rax+38]
    {"province.vp_write", 0xEF2746, "895138"},              // mov [rcx+38],edx
    {"province.id", 0xEEEDAB, "8b91a4000000"},              // mov edx,[rcx+A4]
    {"province.custom_name", 0xEEF288, "4883795000"},       // cmp qword [rcx+50],0: custom name length
    {"province.custom_string", 0xEEF28F, "488d5140"},       // lea rdx,[rcx+40]
    {"tls.thread_forbid", 0x1439295, "b810000000833c1800"},  // mov eax,10; cmp dword [rax+rbx],0
    {"string.big_threshold", 0x1438B9F, "4881fa00100000"},  // cmp rdx,1000
    {"string.big_extra", 0x1438BA8, "4883c227"},            // add rdx,27
    {"string.big_check", 0x1438BB7, "4883f81f"},            // cmp rax,1F
};

/// @brief Call and global targets of the VP view.
inline constexpr core::RipPin vp_target_pins[] = {
    {"scope.calls_set_state", 0xBDE811, 0xBDE815, core::rva::scope_set_state},
    {"tls.index", 0x143926F, 0x1439273, core::rva::tls_index},
    {"string.calls_delete", 0x1438BC1, 0x1438BC5, core::rva::game_delete},
};

/// @brief Slot-sprite functions (spikes and catalogs).
inline constexpr core::Signature sprite_signatures[] = {
    {"sprite.release", core::SignatureGroup::country_hook, core::rva::texture_release,
     "48895C24084889742410574883EC308BDA488BF9", "ReleaseTexture prologue: texmgr, id (texture-release 4.1)"},
};

/// @brief The lazy loader's three writes and its calls (runtime catalogs research 3.6).
inline constexpr core::InstructionPin sprite_instruction_pins[] = {
    {"sprite.lazy_flag", 0x22A7F94, "388ed3010000"},           // cmp [rsi+1D3],cl
    {"sprite.texture_id", 0x22A7FA0, "83bea0010000ff"},        // cmp dword [rsi+1A0],-1
    {"sprite.path_length", 0x22A7FAD, "48398e80000000"},       // cmp [rsi+80],rcx
    {"sprite.database", 0x22A7FBA, "488b4648"},                // mov rax,[rsi+48]
    {"sprite.texmgr", 0x22A7FBE, "488bb870010000"},            // mov rdi,[rax+170]
    {"sprite.options", 0x22A7FC9, "c744243401000001"},         // the load-flags word of the AddTexture options
    {"sprite.path", 0x22A7FDD, "488d5670"},                    // lea rdx,[rsi+70]
    {"sprite.store_id", 0x22A7FE9, "8986a0010000"},            // mov [rsi+1A0],eax
    {"sprite.store_texture", 0x22A7FF9, "48898670010000"},     // mov [rsi+170],rax
    {"sprite.store_size", 0x22A8049, "488986a4000000"},        // mov [rsi+A4],rax
    {"sprite.release_nolock", 0x248B51D, "8bd3488bcfe8"},      // mov edx,ebx; mov rcx,rdi; call (under the lock)
};

/// @brief Calls of the lazy loader, the sprite database and FindSprite.
inline constexpr core::RipPin sprite_target_pins[] = {
    {"sprite.calls_add_texture", 0x22A7FE5, 0x22A7FE9, core::rva::texture_add},
    {"sprite.calls_get_texture", 0x22A7FF5, 0x22A7FF9, core::rva::texture_get},
    {"sprite.calls_texture_size", 0x22A8013, 0x22A8017, core::rva::texture_size},
    {"sprite.database_global", 0x1439234, 0x1439238, core::rva::sprite_database_global},
    {"sprite.calls_find", 0x1439239, 0x143923D, core::rva::sprite_find},
    {"sprite.set_file_releases", 0x22A782D, 0x22A7831, core::rva::texture_release},
    {"sprite.set_file_adds", 0x22A7850, 0x22A7854, core::rva::texture_add},
};

/// @brief Expected counts (a table edit must update them).
inline constexpr std::size_t expected_vp_pins = 1 + 15 + 3;
/// @brief Expected sprite pins.
inline constexpr std::size_t expected_sprite_pins = 1 + 11 + 7;
static_assert(std::size(vp_signatures) + std::size(vp_instruction_pins) + std::size(vp_target_pins) ==
                  expected_vp_pins &&
              std::size(sprite_signatures) + std::size(sprite_instruction_pins) + std::size(sprite_target_pins) ==
                  expected_sprite_pins);

/**
 * @brief Checks the VP view pins.
 * @param text `.text` of the game image.
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_vp_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), vp_signatures,
                              vp_instruction_pins, vp_target_pins);
}

/**
 * @brief Checks the slot-sprite pins (spikes, catalogs).
 * @param text `.text` of the game image.
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_sprite_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), sprite_signatures,
                              sprite_instruction_pins, sprite_target_pins);
}

}  // namespace capie::catalog::pins
