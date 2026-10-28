/**
 * @file window_pins.hpp
 * @brief Code-byte pins of the scripted-window layout (`capie_editor.dll`), checked before any hook goes live.
 *
 * Scan finding N10, part 2: the two window offsets that were only runtime-proven are now pinned
 * in the game's own stores (Ghidra on the 1.19.4.0 file, image base 0x140000000):
 * - Tick (`rva::window_tick`) stores the visible-trigger result at `off::window_desired`; it is the
 *   only store to that byte in Tick;
 * - the window construction, inlined in `CScriptedWindowManager::Rebuild`
 *   (`rva::window_manager_rebuild`, Ghidra review G2; there is no separate constructor), allocates
 *   the window, 0xC10 bytes, and builds it in place: it stores the idler, the model (the
 *   scripted-GUI template), the owning manager (`off::window_manager`) and zeroes root, parent and
 *   parent scripted GUI, then constructs the embedded context at `off::window_context`, sets the
 *   parent-seen word and calls Bind (`rva::window_bind`). The two calls are pinned by their rel32
 *   targets, which ties the stores to this loop body. The pin ids keep the old `window_ctor` prefix.
 * The caption signatures already cover the reads of the same offsets in Tick and Bind; these pins
 * prove the writes. None of the ranges holds a DIR64 relocation (no absolute address in them).
 *
 * A failed pin refuses the editor DLL (`InitCode::signature_mismatch`, logged with the pin id):
 * every editor feature reads these offsets.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::editor::window_pins {

using core::uptr;

/// @brief Window field stores in Tick and in the inlined window construction of the rebuild.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"tick.window_desired", 0xBDB7E4, "8883F90B0000"},               // Tick: mov [rbx+BF9],al (visible trigger)
    {"window_ctor.idler", 0xBDBEAA, "4889BBE0000000"},               // mov [rbx+E0],rdi
    {"window_ctor.model", 0xBDBEB1, "4889B3E8000000"},               // mov [rbx+E8],rsi (scripted-GUI template)
    {"window_ctor.manager", 0xBDBEB8, "4C89B3F0000000"},             // mov [rbx+F0],r14 (the manager)
    {"window_ctor.root_zero", 0xBDBEBF, "33F64889B3F8000000"},       // xor esi,esi; mov [rbx+F8],rsi
    {"window_ctor.parent_zero", 0xBDBEC8, "4889B300010000"},         // mov [rbx+100],rsi
    {"window_ctor.parent_sg_zero", 0xBDBECF, "4889B308010000"},      // mov [rbx+108],rsi
    {"window_ctor.context", 0xBDBED6, "488D8B10010000"},             // lea rcx,[rbx+110]: this of the context ctor
    {"window_ctor.parent_seen", 0xBDBEEC, "66C783F80B00000100"},     // mov word [rbx+BF8],1 (parent seen, desired 0)
};

/// @brief The construction's two calls: the context constructor, then Bind on the new window.
inline constexpr core::RipPin target_pins[] = {
    {"window_ctor.context_ctor_call", 0xBDBEE7, 0xBDBEEB, core::rva::window_context_ctor},
    {"window_ctor.bind_call", 0xBDBF0E, 0xBDBF12, core::rva::window_bind},
};

/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 9;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 2;
static_assert(std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins);

/**
 * @brief Checks every pin of the window layout.
 * @tparam M Memory policy (instruction bytes and rel32 displacements).
 * @param text `.text` of the game image.
 * @param mem Memory policy.
 * @param image Image base the RVAs are relative to.
 * @return Null when all pass, else the id of the first failed pin.
 */
template <class M>
const char* first_failed_pin(const core::TextSection& text, const M& mem, uptr image) noexcept {
    return core::first_failed(text, mem, image, instruction_pins, target_pins);
}

}  // namespace capie::editor::window_pins
