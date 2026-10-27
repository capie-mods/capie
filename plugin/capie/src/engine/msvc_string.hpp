/**
 * @file msvc_string.hpp
 * @brief Reading and building MSVC `std::string` objects that live in game memory.
 *
 * The game is built with MSVC, so its strings are 32-byte objects: a 16-byte buffer that
 * holds short strings inline (small-string optimisation, capacity 15) or a heap pointer,
 * then size and capacity. CAPIE compares widget, model and selector names this way and
 * hands short-lived strings to `SetText` and the console (widget lookups take an `engine::NameView`).
 *
 * Two comparisons exist on purpose and must not be merged:
 *   - `name_equals` (editor bridge) copies `size + 1` bytes and requires the terminating
 *     NUL and no embedded NUL;
 *   - `string_equals` (caption and focus cores) compares `size` bytes only.
 * Both were runtime-accepted with their own rule; merging them would change which windows
 * the caption core accepts.
 *
 * `borrowed_string` builds a string that points at caller memory for the duration of one
 * call. The game must not keep it; every game function it is passed to copies its
 * argument (SetText copies it). It is never stored by CAPIE.
 */
#pragma once
#include "../core/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::engine {

using core::guarded_copy;
using core::u64;
using core::uptr;

/// @brief Inline capacity of the MSVC small-string buffer.
inline constexpr std::size_t sso_capacity = 15;
/// @brief Sanity limit on a string capacity read from game memory (1 MiB).
inline constexpr u64 max_string_capacity = 1048576;
/// @brief Longest name CAPIE compares (model, selector, widget, record key).
inline constexpr std::size_t max_name_bytes = 128;

/// @brief Layout of an MSVC `std::string` (x64, release CRT).
struct MsvcString {
    /// @brief Inline bytes (capacity <= 15) or heap pointer.
    union {
        unsigned char small[16];
        const char* pointer;
    } data;
    std::uint64_t size;      ///< Length in bytes, without terminator.
    std::uint64_t capacity;  ///< Allocated capacity; <= 15 means inline.
};
/// @brief Size of an MSVC `std::string` (x64 release CRT).
inline constexpr std::size_t msvc_string_bytes = 0x20;
/// @brief Offset of `size` in an MSVC string.
inline constexpr std::size_t msvc_string_size_offset = 0x10;
/// @brief Offset of `capacity` in an MSVC string.
inline constexpr std::size_t msvc_string_capacity_offset = 0x18;
static_assert(sizeof(MsvcString) == msvc_string_bytes && offsetof(MsvcString, size) == msvc_string_size_offset &&
              offsetof(MsvcString, capacity) == msvc_string_capacity_offset);

/**
 * @brief Builds a string view-like object over caller bytes for a single game call.
 * @param bytes Caller-owned bytes (must outlive the call).
 * @param size Length.
 * @return MSVC string; short strings are copied inline, longer ones point at `bytes`.
 */
inline MsvcString borrowed_string(const char* bytes, std::size_t size) noexcept {
    MsvcString result{};
    result.size = size;
    if (size <= sso_capacity) {
        if (size != 0) {
            std::memcpy(result.data.small, bytes, size);
        }
        result.capacity = sso_capacity;
    } else {
        result.data.pointer = bytes;
        result.capacity = size;
    }
    return result;
}

/**
 * @brief Copies the bytes of a game string, requiring a terminator and no embedded NUL.
 * @param native Address of the game `MsvcString`.
 * @param out Destination (at least `limit + 1` bytes).
 * @param limit Maximum accepted length.
 * @param size Receives the length.
 * @return `true` when the whole string plus terminator was copied.
 */
inline bool string_bytes(const void* native, char* out, std::size_t limit, std::size_t& size) noexcept {
    MsvcString value{};
    if (!guarded_copy(&value, native, sizeof(value)) || value.size > limit || value.size > value.capacity ||
        value.capacity > max_string_capacity) {
        return false;
    }
    const void* bytes = value.capacity <= sso_capacity ? static_cast<const void*>(value.data.small)
                                                       : static_cast<const void*>(value.data.pointer);
    size = static_cast<std::size_t>(value.size);
    return guarded_copy(out, bytes, size + 1) && out[size] == '\0' && std::memchr(out, '\0', size) == nullptr;
}

/**
 * @brief Strict name comparison used by the editor bridge (terminator required).
 * @param native Address of the game string.
 * @param expected Expected NUL-terminated name (<= 128 bytes).
 * @return `true` on an exact match.
 */
inline bool name_equals(const void* native, const char* expected) noexcept {
    char value[max_name_bytes + 1]{};
    std::size_t size = 0;
    const auto wanted = std::strlen(expected);
    return wanted <= max_name_bytes && string_bytes(native, value, max_name_bytes, size) && size == wanted &&
           std::memcmp(value, expected, size) == 0;
}

/**
 * @brief Byte comparison used by the caption and focus cores (no terminator check).
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param native Address of the game string.
 * @param expected Expected NUL-terminated name.
 * @return `true` when size and bytes match.
 */
template <class M>
bool string_equals(const M& mem, uptr native, const char* expected) noexcept {
    struct Raw {
        union {
            unsigned char small[16];
            uptr pointer;
        } data;
        u64 size;
        u64 capacity;
    } raw{};
    const auto wanted = std::strlen(expected);
    if (!mem.read(native, &raw, sizeof(raw)) || raw.size != wanted || raw.size > raw.capacity ||
        raw.capacity > max_string_capacity || wanted > max_name_bytes) {
        return false;
    }
    char bytes[max_name_bytes + 1]{};
    if (raw.capacity <= sso_capacity) {
        std::memcpy(bytes, raw.data.small, sizeof(raw.data.small));
    } else if (!mem.read(raw.data.pointer, bytes, static_cast<std::size_t>(raw.size))) {
        return false;
    }
    return std::memcmp(bytes, expected, wanted) == 0;
}

}  // namespace capie::engine
