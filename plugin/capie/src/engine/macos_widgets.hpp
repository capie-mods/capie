/**
 * @file macos_widgets.hpp
 * @brief Offline-ready guards for the pinned macOS CContainerWindow collections.
 *
 * Evidence: docs/macos/editor-map.md, Root guard decision. This replaces the Windows
 * anonymous third-list policy; it does not assert a one-to-one ABI mapping. No runtime
 * hook uses this header. Supply verified, slid vtable addresses and a guarded read
 * policy (core::macos::ProcessMemory or an offline fake). Run on the GUI thread only:
 * individual reads do not make a concurrent snapshot safe, and successful validation
 * does not authorize a later unguarded dereference or native call.
 */
#pragma once
#include "macos_string.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::engine::macos {
/// @brief Verified, ASLR-adjusted address points; zero disables validation.
struct WidgetVtables {
    std::uintptr_t root{};     ///< CContainerWindow primary address point.
    std::uintptr_t edit{};     ///< CEditBox primary address point.
    std::uintptr_t instant{};  ///< CInstantTextBox primary address point.
};
/// @brief Policy extent through the last array header, not CContainerWindow sizeof.
inline constexpr std::size_t root_guard_prefix = 0xeb0;
namespace widget_detail {
/// @brief Copied 24-byte native array header, including its unused allocator word.
struct List {
    std::uintptr_t data{};
    std::int32_t capacity{};
    std::int32_t count{};
    std::uintptr_t allocator{};
};
static_assert(sizeof(List) == 24);
/// @brief Bounds counts and the used range; empty arrays need no readable storage.
inline bool bounded(const List& list, std::size_t stride) noexcept {
    if (list.count < 0 || list.count > 4096 || list.capacity < list.count || list.capacity > 65536) {
        return false;
    }
    // The fixed count bound makes multiplication safe for strides 8 and 24.
    return list.count == 0 || detail::valid_range(list.data, static_cast<std::size_t>(list.count) * stride);
}
/// @brief Validates one pointer/name pair, including each name and widget address point.
template<class M>
bool pair(const M& mem, const List& pointers, const List& names, std::uintptr_t expected) noexcept {
    if (!bounded(pointers, 8) || !bounded(names, 24) || pointers.count != names.count) {
        return false;
    }
    for (std::int32_t i = 0; i < pointers.count; ++i) {
        std::uintptr_t widget = 0, vtable = 0;
        const auto index = static_cast<std::size_t>(i);
        if (!mem.read(pointers.data + index * 8, &widget, sizeof(widget)) ||
            !detail::valid_range(widget, sizeof(vtable)) || !mem.read(widget, &vtable, sizeof(vtable)) ||
            vtable != expected) {
            return false;
        }
        std::array<char, max_name_elements + 1> text{};
        std::size_t size = 0;
        if (!string_bytes(mem, names.data + index * 24, std::span<char>(text), size)) {
            return false;
        }
    }
    return true;
}
}  // namespace widget_detail
/**
 * @brief Checks root identity and all four typed lookup arrays using guarded copies.
 * @param mem Complete-copy memory policy; failed reads must return false.
 * @param root Root widget address, never directly dereferenced.
 * @param expected Verified primary vtable addresses for this pinned game build.
 * @return True only if prefix, signed bounds, paired counts, names and widget vtables pass.
 * @note Name validation uses the existing 128-element policy and rejects embedded NUL.
 *       This is structural validation, not visibility/model/context identity validation.
 */
template<class M>
bool root_guard(const M& mem, std::uintptr_t root, const WidgetVtables& expected) noexcept {
    if (!expected.root || !expected.edit || !expected.instant || !detail::valid_range(root, root_guard_prefix)) {
        return false;
    }
    std::array<unsigned char, root_guard_prefix> snapshot{};
    if (!mem.read(root, snapshot.data(), snapshot.size())) {
        return false;
    }
    std::uintptr_t vtable = 0;
    std::memcpy(&vtable, snapshot.data(), sizeof(vtable));
    if (vtable != expected.root) {
        return false;
    }
    widget_detail::List instant, instant_names, edit, edit_names;
    std::memcpy(&instant, snapshot.data() + 0xe20, sizeof(instant));
    std::memcpy(&instant_names, snapshot.data() + 0xe38, sizeof(instant_names));
    std::memcpy(&edit, snapshot.data() + 0xe80, sizeof(edit));
    std::memcpy(&edit_names, snapshot.data() + 0xe98, sizeof(edit_names));
    return widget_detail::pair(mem, instant, instant_names, expected.instant) &&
           widget_detail::pair(mem, edit, edit_names, expected.edit);
}
}  // namespace capie::engine::macos
