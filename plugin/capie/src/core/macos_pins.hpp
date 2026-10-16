/**
 * @file macos_pins.hpp
 * @brief Read-only validation of required bytes and relocated macOS targets.
 *
 * Supply an Image returned successfully by image::verify_game for the loaded main
 * executable, with its actual slide. This does not verify that provenance itself.
 * Call all_match for the complete required feature profile before any activation.
 * Pin records and expected byte spans are trusted, live caller-owned profile data.
 * No code writes, protection changes or hooks occur here. Reads are not atomic and
 * successful checks do not establish trampoline safety or ABI compatibility.
 */
#pragma once
#include "macos_image.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace capie::core::macos::pins {
/// @brief Exact unmasked bytes at an unslid VA, code by default.
struct BytesPin {
    std::uintptr_t address{};                     ///< Unslid start VA.
    std::span<const unsigned char> expected{};     ///< Nonempty exact byte sequence.
    bool code = true;                             ///< Require executable segment.
};
/// @brief Branch/RIP-relative rel32 field, next instruction and exact unslid target.
struct RelativePin {
    std::uintptr_t displacement{}; ///< VA of four displacement bytes, not opcode.
    std::uintptr_t next_instruction{}; ///< Explicit instruction-end VA, including trailing immediate.
    std::uintptr_t target{};       ///< Expected unslid target VA.
    bool target_code = true;      ///< False permits readable data targets, including BSS.
};
/// @brief Relocated pointer field (including a vtable slot) and expected target.
struct PointerPin {
    std::uintptr_t address{}; ///< Unslid VA of eight pointer bytes.
    std::uintptr_t target{};  ///< Expected unslid pointee VA.
    bool target_code = false; ///< Require executable pointee for function slots.
};
/// @brief Expected DWORD written by game initialization, possibly in BSS.
struct RuntimeU32Pin {
    std::uintptr_t address{}; ///< Unslid VA of a runtime DWORD, not file contents.
    std::uint32_t expected{}; ///< Exact value required after initialization.
};
namespace detail {
/// @brief Rejects absent/obviously invalid descriptors before array-based lookups.
inline bool usable(const image::Image& img) noexcept {
    return img.count != 0 && img.count <= image::max_segments;
}
/// @brief Resolves a nonempty file-backed range with optional executable requirement.
inline bool resolve(const image::Image& img, std::uintptr_t va, std::size_t size,
                    bool code, std::uintptr_t& loaded) noexcept {
    return va != 0 && usable(img) && (!code || img.executable_range(va, size)) &&
           img.runtime_address(va, size, loaded) && loaded != 0;
}
} // namespace detail
/** @brief Compares all required bytes through bounded guarded reads. */
template<class M>
bool bytes_match(const M& mem, const image::Image& img, const BytesPin& pin) noexcept {
    std::uintptr_t loaded = 0;
    if (pin.expected.empty() || !detail::resolve(img, pin.address, pin.expected.size(), pin.code, loaded)) {
        return false;
    }
    std::array<unsigned char, 64> chunk{};
    std::size_t done = 0;
    while (done < pin.expected.size()) {
        const auto remaining = pin.expected.size() - done;
        const auto size = remaining < chunk.size() ? remaining : chunk.size();
        if (!mem.read(loaded + done, chunk.data(), size) ||
            std::memcmp(chunk.data(), pin.expected.data() + done, size) != 0) {
            return false;
        }
        done += size;
    }
    return true;
}
/**
 * @brief Verifies a rel32 using explicit next-instruction VA plus signed displacement.
 * @note Pair with a BytesPin covering the instruction. The explicit next VA must be
 *       decoded from that instruction; four displacement bytes need not be its suffix.
 */
template<class M>
bool relative_matches(const M& mem, const image::Image& img, const RelativePin& pin) noexcept {
    // x86 instructions are at most 15 bytes. The displacement must end before next.
    if (pin.next_instruction < pin.displacement || pin.next_instruction - pin.displacement < 4 ||
        pin.next_instruction - pin.displacement > 15) {
        return false;
    }
    const auto suffix = static_cast<std::size_t>(pin.next_instruction - pin.displacement);
    std::uintptr_t source = 0, expected = 0, actual = 0;
    std::int32_t displacement = 0;
    return detail::resolve(img, pin.displacement, suffix, true, source) && pin.target != 0 &&
           img.runtime_virtual_address(pin.target, 1, expected, pin.target_code) && expected != 0 &&
           mem.read(source, &displacement, sizeof(displacement)) &&
           image::apply_slide(source + suffix, displacement, actual) && actual == expected;
}
/** @brief Verifies an eight-byte live pointer against an ASLR-adjusted target VA. */
template<class M>
bool pointer_matches(const M& mem, const image::Image& img, const PointerPin& pin) noexcept {
    std::uintptr_t source = 0, expected = 0, actual = 0;
    return detail::resolve(img, pin.address, sizeof(actual), false, source) &&
           pin.target != 0 && img.runtime_virtual_address(pin.target, 1, expected, pin.target_code) && expected != 0 &&
           mem.read(source, &actual, sizeof(actual)) && actual == expected;
}
/**
 * @brief Checks initialized runtime values separately from file/code pin validation.
 * @param mem Complete-copy guarded memory policy.
 * @param img Verified main-image descriptor with actual runtime slide.
 * @param values Nonempty required set, read only from readable virtual ranges.
 * @return True only if every full DWORD equals its expected value; BSS is allowed.
 * @note Caller must establish a post-initialization boundary. A matching set proves
 *       only these values, not that all engine initialization has finished. This
 *       function neither waits/retries nor authorizes activation by itself.
 */
template<class M>
bool after_init_values_match(const M& mem, const image::Image& img,
                             std::span<const RuntimeU32Pin> values) noexcept {
    if (!detail::usable(img) || values.empty()) {
        return false;
    }
    for (const auto& pin : values) {
        std::uintptr_t address = 0;
        std::uint32_t actual = 0;
        if (pin.address == 0 || !img.runtime_virtual_address(pin.address, sizeof(actual), address) ||
            address == 0 || !mem.read(address, &actual, sizeof(actual)) || actual != pin.expected) {
            return false;
        }
    }
    return true;
}
/**
 * @brief Validates every supplied required pin; an entirely empty profile fails.
 * @param mem Complete-copy guarded read policy.
 * @param img Successfully verified main-image descriptor and actual runtime slide.
 * @param bytes Required exact byte checks, possibly empty.
 * @param relatives Required branch-target checks, possibly empty.
 * @param pointers Required relocated pointer checks, possibly empty.
 * @return True only when at least one pin exists and every required check succeeds.
 */
template<class M>
bool all_match(const M& mem, const image::Image& img, std::span<const BytesPin> bytes,
               std::span<const RelativePin> relatives, std::span<const PointerPin> pointers) noexcept {
    if (!detail::usable(img) || (bytes.empty() && relatives.empty() && pointers.empty())) {
        return false;
    }
    for (const auto& pin : bytes) {
        if (!bytes_match(mem, img, pin)) return false;
    }
    for (const auto& pin : relatives) {
        if (!relative_matches(mem, img, pin)) return false;
    }
    for (const auto& pin : pointers) {
        if (!pointer_matches(mem, img, pin)) return false;
    }
    return true;
}
} // namespace capie::core::macos::pins
