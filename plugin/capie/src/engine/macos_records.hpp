/**
 * @file macos_records.hpp
 * @brief Native temp-record layout and scoped SysV constructor argument adapter.
 * Evidence: docs/macos/records-transport.md. No game address is bound or invoked by
 * default. This boundary consumes a frame already authorized by editor lifecycle,
 * identity, visibility and snapshot guards; it does not replace those guards.
 */
#pragma once
#include "../core/memory.hpp"
#include "macos_owned_string.hpp"
#include "macos_string.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace capie::engine::macos::records {
/// @brief Native pair: libc++ key followed by signed fixed-point raw value.
struct Record {
    NativeString key{};        ///< Native string; ownership belongs to the originating frame/game.
    std::int64_t raw_value{};  ///< Value at +0x18, copied without conversion.
};
/// @brief Native record vector header; constructor reads data and count synchronously.
struct TempVector {
    Record* data{};           ///< Records, 32-byte stride.
    std::int32_t capacity{};  ///< Allocated record slots at +8.
    std::int32_t count{};     ///< Valid records at +12.
    void* allocator{};        ///< Allocator at +16, never owned by CAPIE.
};
static_assert(sizeof(Record) == 0x20 && offsetof(Record, raw_value) == 0x18);
static_assert(sizeof(TempVector) == 0x18 && offsetof(TempVector, count) == 0xc &&
              offsetof(TempVector, allocator) == 0x10);
static_assert(std::is_trivially_copyable_v<Record> && std::is_trivially_copyable_v<TempVector>);
/// @brief Shared protocol sanity limit, not an inferred game allocation limit.
inline constexpr std::int32_t max_capacity = 65536;

/** @brief Copies a strict terminated record key; output length changes only on success.
 * @param mem Memory reader. @param record Local record snapshot. @param out Destination.
 * @param length Receives key length. @return True for a bounded valid key.
 */
template <class M>
bool read_key(const M& mem, const Record& record, std::span<char> out, std::size_t& length) noexcept {
    if (out.size() > max_name_elements + 1)
        out = out.first(max_name_elements + 1);
    return string_bytes(mem, reinterpret_cast<std::uintptr_t>(&record.key), out, length);
}
/** @brief Reads and bounds a native vector, without retaining ownership or copying records.
 * @param mem Memory reader. @param address Header address. @param out Receives header on success.
 * @return False for unreadable header, negative bounds, null/nonempty data or address overflow.
 */
template <class M>
bool read_vector(const M& mem, std::uintptr_t address, TempVector& out) noexcept {
    TempVector value{};
    if (!core::rd(mem, address, 0, value) || value.count < 0 || value.capacity < value.count ||
        value.capacity > max_capacity)
        return false;
    const auto data = reinterpret_cast<std::uintptr_t>(value.data);
    const auto bytes = static_cast<std::size_t>(value.count) * sizeof(Record);
    if (value.count && !detail::valid_range(data, bytes))
        return false;
    out = value;
    return true;
}
/** @brief Reads both tagged parent names and accepts only valid empty strings.
 * @param mem Memory reader. @param model Native model address.
 * @return True only for empty parent window (+0x40) and scripted-GUI (+0x58) names.
 */
template <class M>
bool parent_names_empty(const M& mem, std::uintptr_t model) noexcept {
    return model && model <= core::uptr_max - 0x58 && name_equals<char>(mem, model + 0x40, {}) &&
           name_equals<char>(mem, model + 0x58, {});
}
/** @brief Reads a native edit UTF-32 string; tagged size/capacity are decoded by macos_string.
 * @param mem Memory reader. @param edit Native edit address. @param out UTF-32 storage.
 * @param length Receives element count. @param capacity_limit Maximum usable capacity in elements.
 * @return True for a complete terminated snapshot.
 */
template <class M>
bool read_edit_text(const M& mem, std::uintptr_t edit, std::span<char32_t> out, std::size_t& length,
                    std::size_t capacity_limit = max_string_capacity) noexcept {
    if (edit == 0 || edit > core::uptr_max - 0xa0)
        return false;
    detail::StringView header{};
    if (!detail::decode<char32_t>(mem, edit + 0xa0, header) || header.size >= out.size())
        return false;
    std::uint64_t capacity = 4;
    if (header.long_mode) {
        std::memcpy(&capacity, header.raw.data(), sizeof(capacity));
        capacity = (capacity & ~std::uint64_t{1}) - 1;
    }
    if (capacity > capacity_limit || !detail::copy(mem, header, out.data(), true) || out[header.size] != 0)
        return false;
    for (std::size_t index = 0; index < header.size; ++index)
        if (out[index] == 0)
            return false;
    length = header.size;
    return true;
}
/// @brief SysV constructor: self RDI, model RSI, selector RDX, scope RCX, records R8.
using Constructor = void (*)(void*, const NativeString*, const NativeString*, const void*, const TempVector*);
/// @brief Stack-only replacement description; upstream must authorize lifecycle and payload.
struct ConstructorFrame {
    const NativeString* model{};      ///< Exact model argument identity.
    const NativeString* selector{};   ///< Exact selector identity.
    const void* scope{};              ///< Exact scope identity.
    const TempVector* source{};       ///< Exact original vector identity.
    const TempVector* replacement{};  ///< Stable records valid until original returns.
    std::string_view model_name;      ///< Expected immutable model name.
    std::string_view selector_name;   ///< Expected immutable selector name.
    unsigned calls{};                 ///< Replacement calls observed.
};
/** @brief Performs exactly one constructor call, replacing records only for a matching authorized frame.
 * @param mem Memory reader. @param original Verified non-null native trampoline.
 * @param frame Current thread's authorized stack frame, or null.
 * @param self Fresh native command storage. @param gui Native model argument.
 * @param selector Native selector argument. @param scope Native scope argument.
 * @param source Original records argument. @note Native constructor has no result contract; inspect self after the
 * call, never RAX.
 * @note No ownership transfers to the original: it deep-copies the replacement. Frame payload
 * filtering and semantic authorization remain responsibilities of the shared editor transport.
 */
template <class M>
void forward_constructor(const M& mem, Constructor original, ConstructorFrame* frame, void* self,
                         const NativeString* gui, const NativeString* selector, const void* scope,
                         const TempVector* source) noexcept {
    const TempVector* selected = source;
    TempVector checked{};
    if (frame && frame->replacement && gui == frame->model && selector == frame->selector && scope == frame->scope &&
        source == frame->source && name_equals<char>(mem, reinterpret_cast<std::uintptr_t>(gui), frame->model_name) &&
        name_equals<char>(mem, reinterpret_cast<std::uintptr_t>(selector), frame->selector_name) &&
        read_vector(mem, reinterpret_cast<std::uintptr_t>(frame->replacement), checked)) {
        ++frame->calls;
        selected = frame->replacement;
    }
    return original(self, gui, selector, scope, selected);
}
}  // namespace capie::engine::macos::records
