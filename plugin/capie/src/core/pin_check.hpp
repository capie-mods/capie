/**
 * @file pin_check.hpp
 * @brief The one checker for a feature's pin tables: signatures, instruction pins, RIP and slot pins.
 *
 * A feature that depends on game code lists its evidence as tables (`*_pins.hpp`,
 * `console_wiring.hpp`) and calls `first_failed(text, mem, image, tables...)` once before it
 * goes live. The tables are checked in the order given, each entry in table order, and the
 * id of the first failing entry is returned, so the log names exactly one pin. What a failure
 * means (refuse the DLL or switch one feature off) is the caller's policy, documented in
 * `init_codes.hpp`.
 *
 * Kinds of entry:
 * - `Signature` (`signatures.hpp`): a byte pattern unique in `.text` at its RVA;
 * - `InstructionPin`: exact bytes of one instruction, typically proving a structure offset;
 * - `RipPin` (`signatures.hpp`): a RIP-relative or rel32 operand that must reach a target RVA;
 * - `NamedSlotPin`: a vtable slot that must hold a function RVA, with an id for the log.
 *
 * Reads code and data bytes only, through the memory policy; no allocation.
 */
#pragma once
#include "memory.hpp"
#include "pe_image.hpp"
#include "signatures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace capie::core {

/// @brief Exact instruction bytes at an RVA.
struct InstructionPin {
    const char* id;   ///< Identifier for the log.
    uptr rva;         ///< Instruction RVA.
    const char* hex;  ///< Exact bytes.
};

/// @brief A vtable slot that must point to a given function, with an id for the log.
struct NamedSlotPin {
    const char* id;  ///< Identifier for the log.
    SlotPin pin;     ///< Vtable, slot and target RVAs.
};

/**
 * @brief Compares exact bytes at `image + pin.rva`.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param pin Pin.
 * @return `true` when the bytes match.
 */
template <class M>
bool verify_instruction(const M& mem, uptr image, const InstructionPin& pin) noexcept {
    std::array<unsigned char, max_pattern_bytes> expected{};
    const auto size = decode_hex(pin.hex, expected.data(), expected.size());
    std::array<unsigned char, max_pattern_bytes> actual{};
    return size != 0 && mem.read(image + pin.rva, actual.data(), size) &&
           std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(size), actual.begin());
}

namespace pin_check_detail {

/// @brief Checks a signature (the memory policy is not used: `.text` is read directly).
/// @return `true` when it passes.
template <class M>
bool passes(const TextSection& text, const M& /*mem*/, uptr /*image*/, const Signature& entry) noexcept {
    return verify_signature(text, entry);
}

/// @brief Checks an instruction pin. @return `true` when it passes.
template <class M>
bool passes(const TextSection& /*text*/, const M& mem, uptr image, const InstructionPin& entry) noexcept {
    return verify_instruction(mem, image, entry);
}

/// @brief Checks a RIP/rel32 target pin. @return `true` when it passes.
template <class M>
bool passes(const TextSection& /*text*/, const M& mem, uptr image, const RipPin& entry) noexcept {
    return verify_rip_pin(mem, image, entry);
}

/// @brief Checks a named vtable slot pin. @return `true` when it passes.
template <class M>
bool passes(const TextSection& /*text*/, const M& mem, uptr image, const NamedSlotPin& entry) noexcept {
    return verify_slot_pin(mem, image, entry.pin);
}

/**
 * @brief First failing entry of one table.
 * @return Its id, or null when every entry passes.
 */
template <class M, class Entry, std::size_t N>
const char* first_failed_in(const TextSection& text, const M& mem, uptr image, const Entry (&table)[N]) noexcept {
    for (const auto& entry : table) {
        if (!passes(text, mem, image, entry)) {
            return entry.id;
        }
    }
    return nullptr;
}

}  // namespace pin_check_detail

/**
 * @brief Checks pin tables in order and names the first failure.
 * @tparam M Memory policy (instruction bytes, RIP displacements, vtable slots).
 * @tparam Tables Arrays of `Signature`, `InstructionPin`, `RipPin` or `NamedSlotPin`.
 * @param text `.text` of the game image (signatures).
 * @param mem Memory policy.
 * @param image Image base the RVAs are relative to.
 * @param tables Tables, checked in argument order.
 * @return Null when every entry passes, else the id of the first failed entry.
 */
template <class M, class... Tables>
const char* first_failed(const TextSection& text, const M& mem, uptr image, const Tables&... tables) noexcept {
    const char* failed = nullptr;
    static_cast<void>((((failed = pin_check_detail::first_failed_in(text, mem, image, tables)) != nullptr) || ...));
    return failed;
}

}  // namespace capie::core
