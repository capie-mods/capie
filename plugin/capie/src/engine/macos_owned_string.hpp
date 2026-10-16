/**
 * @file macos_owned_string.hpp
 * @brief Constructs a bounded native char string for a fresh macOS game return object.
 *
 * Mirrors CFaction::GetName at 0x100e527e0: inline capacity 22; long allocation
 * (length | 15) + 1, tagged allocation + 1, length at +8 and pointer at +16.
 * Instructions 0x100e52865..0x100e52891 prove the allocation and header layout.
 * The pinned allocator 0x102a7fbd0 tail-jumps to malloc; 0x102a7fc10 to free.
 * No game address is invoked here: integration must supply a verified allocator.
 * This constructs a return value only. It never assigns to an existing game string
 * or changes stored faction data. The receiving game caller owns the result.
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace capie::engine::macos {
/// @brief Trivial wire representation; deliberately has no owning C++ destructor.
struct alignas(8) NativeString {
    std::array<unsigned char, 24> bytes{}; ///< Pinned libc++ representation.
};
static_assert(sizeof(NativeString) == 24);
/// @brief Verified game allocation callback; returns null on failure.
using StringAllocate = void* (*)(std::size_t) noexcept;

/**
 * @brief Constructs a name, transferring long-buffer ownership to the receiver.
 * @param text Trusted readable input, at most MaxBytes bytes (default 128) and without embedded NUL.
 * @param out Fresh return storage; must not contain a live owning string.
 * @param allocate Game-compatible allocation function, needed only for long strings.
 * @return True on success; out is unchanged on failure and no allocation is retained.
 * @note Alphabet validation belongs to the feature. No assignment/free is performed.
 */
template <std::size_t MaxBytes = 128>
inline bool construct_name(std::string_view text, NativeString& out, StringAllocate allocate) noexcept {
    static_assert(MaxBytes > 0 && MaxBytes <= 4096);
    if (text.size() > MaxBytes || text.find('\0') != std::string_view::npos) {
        return false;
    }
    NativeString result;
    if (text.size() <= 22) {
        result.bytes[0] = static_cast<unsigned char>(text.size() * 2);
        if (!text.empty()) {
            std::memcpy(result.bytes.data() + 1, text.data(), text.size());
        }
    } else {
        if (allocate == nullptr) {
            return false;
        }
        const std::uint64_t allocation = (text.size() | std::size_t{15}) + 1;
        void* buffer = allocate(allocation);
        if (buffer == nullptr) {
            return false;
        }
        std::memcpy(buffer, text.data(), text.size());
        static_cast<char*>(buffer)[text.size()] = '\0';
        const std::uint64_t tagged = allocation + 1;
        const std::uint64_t length = text.size();
        std::memcpy(result.bytes.data(), &tagged, 8);
        std::memcpy(result.bytes.data() + 8, &length, 8);
        std::memcpy(result.bytes.data() + 16, &buffer, 8);
    }
    out = result;
    return true;
}
}  // namespace capie::engine::macos
