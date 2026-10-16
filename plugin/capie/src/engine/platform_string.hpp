/** @file platform_string.hpp @brief Explicit native string boundary for shared editor logic. */
#pragma once
#if defined(__APPLE__)
#include "../core/memory.hpp"
#include "macos_owned_string.hpp"
#include "macos_string.hpp"
namespace capie::engine {
using EngineString = macos::NativeString;
using core::guarded_copy;
inline constexpr std::size_t max_name_bytes = 128;
inline constexpr std::size_t sso_capacity = 22;
inline constexpr std::size_t max_string_capacity = macos::max_string_capacity;
/// @brief Read-only argument for one synchronous call; never destroy or retain its long buffer.
inline EngineString borrowed_string(const char* bytes, std::size_t size) noexcept {
    EngineString result;
    if (size <= 22) {
        result.bytes[0] = static_cast<unsigned char>(size * 2);
        if (size != 0) std::memcpy(result.bytes.data() + 1, bytes, size);
    } else if (size < max_string_capacity) {
        const std::uint64_t allocation = ((size + 2) & ~std::size_t{1}) | 1;
        const std::uint64_t length = size;
        std::memcpy(result.bytes.data(), &allocation, 8);
        std::memcpy(result.bytes.data() + 8, &length, 8);
        std::memcpy(result.bytes.data() + 16, &bytes, 8);
    }
    return result;
}
inline bool string_bytes(const void* native, char* out, std::size_t limit, std::size_t& size) noexcept {
    return out != nullptr && limit < max_string_capacity &&
           macos::string_bytes(core::ProcessMemory{}, reinterpret_cast<core::uptr>(native),
                               std::span<char>(out, limit + 1), size);
}
inline bool name_equals(const void* native, const char* expected) noexcept {
    return expected != nullptr && macos::name_equals(core::ProcessMemory{}, reinterpret_cast<core::uptr>(native),
                                                     std::string_view(expected));
}
template<class M>
bool string_equals(const M& mem, core::uptr native, const char* expected) noexcept {
    return expected != nullptr && macos::string_equals(mem, native, std::string_view(expected));
}
} // namespace capie::engine
#else
#include "msvc_string.hpp"
namespace capie::engine { using EngineString = MsvcString; }
#endif
