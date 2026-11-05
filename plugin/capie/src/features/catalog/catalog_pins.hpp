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
 * Pins read on 1.19.4.1 with capstone and moved to 1.19.4.2 with the same bytes
 * (`docs/research/port-1.19.4.2.md`, section "API level 9"); none overlaps a DIR64 relocation.
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
    {"scope.state_id", 0xBDE868, "8b9368040000"},           // mov edx,[rbx+468]: context scope +3C0, state id +A8
    {"scope.set_state", 0x5829CF, "89b9a8000000"},          // mov [rcx+A8],edi
    {"state.count", 0x3E904D, "3b81d4020000"},              // cmp eax,[rcx+2D4]: state slots
    {"state.array", 0x3E9058, "488b81c80200004c8b04d0"},    // mov rax,[rcx+2C8]; mov r8,[rax+rdx*8]
    {"state.province_count", 0xA2A1EA, "8b7924"},           // mov edi,[rcx+24]
    {"state.provinces", 0xA2A1FB, "4c8b5918"},              // mov r11,[rcx+18]
    {"province.vp_read", 0xA2A207, "448b4038"},             // mov r8d,[rax+38]
    {"province.vp_write", 0xEF27B6, "895138"},              // mov [rcx+38],edx
    {"province.id", 0xEEEE1B, "8b91a4000000"},              // mov edx,[rcx+A4]
    {"province.custom_name", 0xEEF2F8, "4883795000"},       // cmp qword [rcx+50],0: custom name length
    {"province.custom_string", 0xEEF2FF, "488d5140"},       // lea rdx,[rcx+40]
    {"tls.thread_forbid", 0x1439305, "b810000000833c1800"},  // mov eax,10; cmp dword [rax+rbx],0
    {"string.big_threshold", 0x1438C0F, "4881fa00100000"},  // cmp rdx,1000
    {"string.big_extra", 0x1438C18, "4883c227"},            // add rdx,27
    {"string.big_check", 0x1438C27, "4883f81f"},            // cmp rax,1F
};

/// @brief Call and global targets of the VP view.
inline constexpr core::RipPin vp_target_pins[] = {
    {"scope.calls_set_state", 0xBDE881, 0xBDE885, core::rva::scope_set_state},
    {"tls.index", 0x14392DF, 0x14392E3, core::rva::tls_index},
    {"string.calls_delete", 0x1438C31, 0x1438C35, core::rva::game_delete},
};

/// @brief Slot-sprite functions (spikes and catalogs).
inline constexpr core::Signature sprite_signatures[] = {
    {"sprite.release", core::SignatureGroup::country_hook, core::rva::texture_release,
     "48895C24084889742410574883EC308BDA488BF9", "ReleaseTexture prologue: texmgr, id (texture-release 4.1)"},
};

/// @brief The lazy loader's three writes and its calls (runtime catalogs research 3.6).
inline constexpr core::InstructionPin sprite_instruction_pins[] = {
    {"sprite.lazy_flag", 0x22A8004, "388ed3010000"},           // cmp [rsi+1D3],cl
    {"sprite.texture_id", 0x22A8010, "83bea0010000ff"},        // cmp dword [rsi+1A0],-1
    {"sprite.path_length", 0x22A801D, "48398e80000000"},       // cmp [rsi+80],rcx
    {"sprite.database", 0x22A802A, "488b4648"},                // mov rax,[rsi+48]
    {"sprite.texmgr", 0x22A802E, "488bb870010000"},            // mov rdi,[rax+170]
    {"sprite.options", 0x22A8039, "c744243401000001"},         // the load-flags word of the AddTexture options
    {"sprite.path", 0x22A804D, "488d5670"},                    // lea rdx,[rsi+70]
    {"sprite.store_id", 0x22A8059, "8986a0010000"},            // mov [rsi+1A0],eax
    {"sprite.store_texture", 0x22A8069, "48898670010000"},     // mov [rsi+170],rax
    {"sprite.store_size", 0x22A80B9, "488986a4000000"},        // mov [rsi+A4],rax
    {"sprite.release_nolock", 0x248B65D, "8bd3488bcfe8"},      // mov edx,ebx; mov rcx,rdi; call (under the lock)
};

/// @brief Calls of the lazy loader, the sprite database and FindSprite.
inline constexpr core::RipPin sprite_target_pins[] = {
    {"sprite.calls_add_texture", 0x22A8055, 0x22A8059, core::rva::texture_add},
    {"sprite.calls_get_texture", 0x22A8065, 0x22A8069, core::rva::texture_get},
    {"sprite.calls_texture_size", 0x22A8083, 0x22A8087, core::rva::texture_size},
    {"sprite.database_global", 0x14392A4, 0x14392A8, core::rva::sprite_database_global},
    {"sprite.calls_find", 0x14392A9, 0x14392AD, core::rva::sprite_find},
    {"sprite.set_file_releases", 0x22A789D, 0x22A78A1, core::rva::texture_release},
    {"sprite.set_file_adds", 0x22A78C0, 0x22A78C4, core::rva::texture_add},
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
