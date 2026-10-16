/** @file macos_activation_checks.hpp
 * @brief Supplemental allocator instructions and initialized editor token checks.
 * Exact build/profile validation is a separate prerequisite. Stub instructions
 * prove the pinned indirection, not the current dyld pointer destination.
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace capie::core::macos::activation {
/// @brief Unslid allocator entry VA; evidence: re/localisation-evidence.json.
inline constexpr std::uintptr_t allocate_va = 0x102a7fbd0;
/// @brief Unslid deallocator entry VA; evidence: re/localisation-evidence.json.
inline constexpr std::uintptr_t deallocate_va = 0x102a7fc10;
namespace detail {
inline constexpr std::uintptr_t preferred_image = 0x100000000;
/** @brief Compare a bounded exact byte range using the supplied read policy. */
template <class Memory, std::size_t N>
[[nodiscard]] bool matches(const Memory& memory, std::uintptr_t image, std::uintptr_t va,
                           const std::array<unsigned char, N>& expected) noexcept {
    const auto rva = va - preferred_image;
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (!image || image > maximum - rva || N > maximum - (image + rva))
        return false;
    std::array<unsigned char, N> actual{};
    return memory.read(image + rva, actual.data(), N) && actual == expected;
}
}  // namespace detail
/** @brief Verify both allocator entry jumps and their RIP-indirect import stubs.
 * Does not inspect mutable dyld slots or invoke either allocator.
 */
template <class Memory>
[[nodiscard]] bool allocator_matches(const Memory& memory, std::uintptr_t image) noexcept {
    return detail::matches(memory, image, allocate_va,
                           std::array<unsigned char, 10>{0x55, 0x48, 0x89, 0xe5, 0x5d, 0xe9, 0xc6, 0x67, 0x0a, 0}) &&
           detail::matches(memory, image, deallocate_va,
                           std::array<unsigned char, 10>{0x55, 0x48, 0x89, 0xe5, 0x5d, 0xe9, 0x5e, 0x65, 0x0a, 0}) &&
           detail::matches(memory, image, 0x102b263a0, std::array<unsigned char, 6>{0xff, 0x25, 0x42, 0xc3, 0x77, 0}) &&
           detail::matches(memory, image, 0x102b26178, std::array<unsigned char, 6>{0xff, 0x25, 0x8a, 0xc2, 0x77, 0});
}
/** @brief Require the exact initialized politics/player token values, not merely nonzero.
 * Observes two bounded snapshots; it does not establish UI object lifetime.
 */
template <class Memory>
[[nodiscard]] bool tokens_ready(const Memory& memory, std::uintptr_t image) noexcept {
    return detail::matches(memory, image, 0x1035af598, std::array<unsigned char, 4>{0xb2, 0x3a, 0, 0}) &&
           detail::matches(memory, image, 0x1035ae4b8, std::array<unsigned char, 4>{0x70, 0x3a, 0, 0});
}
}  // namespace capie::core::macos::activation
