/**
 * @file macos_string.hpp
 * @brief Read-only adapters for the pinned game's 24-byte libc++ char and UTF-32 strings.
 *
 * Evidence: docs/macos/editor-map.md and console-map.md. Short length is byte[0] >> 1;
 * data starts at +1 for char (capacity 22), +4 for char32_t (capacity 4). Long objects
 * store tagged allocation count at +0, element count at +8, data pointer at +16.
 * Clearing allocation bit 0 and subtracting one gives capacity in elements, not bytes.
 * UTF-32 evidence: 0x102793c81 checks length 4; 0x102793c87 forms inline +4;
 * 0x102793cbb allocates 4*(capacity+1); 0x102793ccf tags the element allocation count.
 *
 * Strict reads require a terminator and reject embedded NUL. Nonterminator comparison
 * deliberately reads only content, retaining the Windows caption/focus distinction.
 * This adapter neither constructs writable game strings nor validates Unicode scalars;
 * character/alphabet validation remains the feature's responsibility. Separate kernel
 * reads are not an atomic snapshot of a concurrently modified string.
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace capie::engine::macos {

/// @brief Sanity limit for usable long-string capacity, in elements.
inline constexpr std::size_t max_string_capacity = 1048576;
/// @brief Maximum number of elements compared as a game name.
inline constexpr std::size_t max_name_elements = 128;

namespace detail {
/// @brief Copied header plus validated data location and element length.
struct StringView {
    /// @brief All header bytes; short content is read from this snapshot.
    std::array<unsigned char, 24> raw{};
    /// @brief Long-buffer address, zero for a short string.
    std::uintptr_t data{};
    /// @brief Number of content elements, excluding terminator.
    std::size_t size{};
    /// @brief True when the header selects heap storage.
    bool long_mode{};
};

/**
 * @brief Checks a nonempty address range without wrapping.
 * @param address First byte. @param bytes Byte count.
 * @return True for a non-null non-wrapping range.
 */
inline bool valid_range(std::uintptr_t address, std::size_t bytes) noexcept {
    return address != 0 && bytes <= std::numeric_limits<std::uintptr_t>::max() - address;
}

/**
 * @brief Copies and validates the string header without touching its long buffer.
 * @tparam Char char or char32_t. @tparam M Memory policy.
 * @param mem Memory policy. @param native Header address. @param view Receives decoded metadata.
 * @return True if the header and lengths are valid.
 */
template <class Char, class M>
bool decode(const M& mem, std::uintptr_t native, StringView& view) noexcept {
    static_assert(std::is_same_v<Char, char> || std::is_same_v<Char, char32_t>);
    static_assert(sizeof(char32_t) == 4 && sizeof(std::uintptr_t) == 8);
    if (!valid_range(native, view.raw.size()) || !mem.read(native, view.raw.data(), view.raw.size())) {
        return false;
    }
    view.long_mode = (view.raw[0] & 1) != 0;
    if (!view.long_mode) {
        view.size = view.raw[0] >> 1;
        return view.size <= (sizeof(Char) == 1 ? 22U : 4U);
    }
    std::uint64_t allocation = 0;
    std::uint64_t length = 0;
    std::memcpy(&allocation, view.raw.data(), sizeof(allocation));
    std::memcpy(&length, view.raw.data() + 8, sizeof(length));
    std::memcpy(&view.data, view.raw.data() + 16, sizeof(view.data));
    allocation &= ~std::uint64_t{1};
    if (allocation == 0 || allocation - 1 > max_string_capacity || length > allocation - 1 || view.data == 0) {
        return false;
    }
    view.size = static_cast<std::size_t>(length);
    return true;
}

/**
 * @brief Copies content from a validated view, optionally including the terminator.
 * @tparam Char Element type. @tparam M Memory policy.
 * @param mem Memory policy. @param view Decoded header. @param out Writable buffer.
 * @param terminated Whether to include one terminating element.
 * @return True when the complete requested range was copied.
 */
template <class Char, class M>
bool copy(const M& mem, const StringView& view, Char* out, bool terminated) noexcept {
    const auto count = view.size + static_cast<std::size_t>(terminated);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Char)) {
        return false;
    }
    const auto bytes = count * sizeof(Char);
    if (!view.long_mode) {
        if (bytes != 0) {
            std::memcpy(out, view.raw.data() + sizeof(Char), bytes);
        }
        return true;
    }
    return valid_range(view.data, bytes) && (bytes == 0 || mem.read(view.data, out, bytes));
}
}  // namespace detail

/**
 * @brief Copies a char or UTF-32 string, requiring NUL termination and no embedded NUL.
 * @tparam Char char or char32_t. @tparam M Memory policy.
 * @param mem Memory policy. @param native String header address.
 * @param out Writable element span, including space for the terminator.
 * @param size Receives content length only on success; unchanged on failure.
 * @return True on a complete valid copy; on failure discard the output contents.
 */
template <class Char, class M>
bool string_bytes(const M& mem, std::uintptr_t native, std::span<Char> out, std::size_t& size) noexcept {
    detail::StringView view;
    if (!detail::decode<Char>(mem, native, view) || view.size >= out.size() ||
        !detail::copy(mem, view, out.data(), true) || out[view.size] != Char{}) {
        return false;
    }
    for (std::size_t i = 0; i < view.size; ++i) {
        if (out[i] == Char{}) {
            return false;
        }
    }
    size = view.size;
    return true;
}

/**
 * @brief Compares a name with strict termination and embedded-NUL checks.
 * @tparam Char char or char32_t. @tparam M Memory policy.
 * @param mem Memory policy. @param native String header address. @param expected Trusted expected elements.
 * @return True for an exact valid name of at most max_name_elements elements.
 */
template <class Char, class M>
bool name_equals(const M& mem, std::uintptr_t native, std::basic_string_view<Char> expected) noexcept {
    if (expected.size() > max_name_elements) {
        return false;
    }
    std::array<Char, max_name_elements + 1> value{};
    std::size_t size = 0;
    return string_bytes(mem, native, std::span<Char>(value), size) && size == expected.size() &&
           std::basic_string_view<Char>(value.data(), size) == expected;
}

/**
 * @brief Compares exactly the content elements without requiring a readable terminator.
 * @tparam Char char or char32_t. @tparam M Memory policy.
 * @param mem Memory policy. @param native String header address. @param expected Trusted expected elements.
 * @return True for equal content of at most max_name_elements elements, including literal NULs.
 */
template <class Char, class M>
bool string_equals(const M& mem, std::uintptr_t native, std::basic_string_view<Char> expected) noexcept {
    detail::StringView view;
    if (expected.size() > max_name_elements || !detail::decode<Char>(mem, native, view) || view.size != expected.size()) {
        return false;
    }
    std::array<Char, max_name_elements> value{};
    return detail::copy(mem, view, value.data(), false) &&
           std::basic_string_view<Char>(value.data(), view.size) == expected;
}

}  // namespace capie::engine::macos
