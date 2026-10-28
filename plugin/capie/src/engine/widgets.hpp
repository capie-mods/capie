/**
 * @file widgets.hpp
 * @brief Checked reads of game widgets: edit boxes, instantTextBox carriers and child lists.
 *
 * Every function takes a widget address that the caller obtained in the same call (from
 * a fresh FindWidget or from a window it just inspected) and returns copied values. An
 * edit box is "ours" only if its primary vtable, its text-buffer vtable, its definition
 * vtable and its definition name all match; anything else is rejected before a single
 * byte of its text is read.
 *
 * Text is copied, never referenced: `InputSnapshot` holds up to 128 UTF-32 code points
 * from an edit box, `Utf8Snapshot` holds a carrier's UTF-8 bytes and, after
 * `decode_utf8`, the same text as code points. Nothing is trimmed, normalised or
 * truncated, because each of those could change the name the player typed.
 *
 * Thread: UI thread (the functions read GUI objects). No locks, no allocation.
 */
#pragma once
#include "../core/game_layout.hpp"
#include "../core/memory.hpp"
#include "game_calls.hpp"
#include "msvc_string.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::engine {

using core::member;
using core::readable;
namespace rva = core::rva;
namespace off = core::off;
namespace flag = core::flag;

/// @brief Largest edit-box text CAPIE copies (UTF-32 code points).
inline constexpr std::size_t max_codepoints = 128;
/// @brief Largest carrier text in UTF-8 bytes (4 bytes per code point).
inline constexpr std::size_t max_utf8 = max_codepoints * 4;
/// @brief Sanity limit on an edit-box UTF-32 capacity.
inline constexpr std::uint64_t max_edit_capacity = 65536;
/// @brief Sanity limit on a child list's element count.
inline constexpr int max_list_count = 4096;
/// @brief Sanity limit on a child list's capacity.
inline constexpr int max_list_capacity = 65536;

/// @brief Highest Unicode scalar value.
inline constexpr std::uint32_t max_scalar = 0x10FFFF;
/// @brief First UTF-16 high surrogate.
inline constexpr std::uint32_t high_surrogate_first = 0xD800;
/// @brief Last UTF-16 high surrogate.
inline constexpr std::uint32_t high_surrogate_last = 0xDBFF;
/// @brief First UTF-16 low surrogate.
inline constexpr std::uint32_t low_surrogate_first = 0xDC00;
/// @brief Last UTF-16 low surrogate.
inline constexpr std::uint32_t low_surrogate_last = 0xDFFF;
/// @brief First code point outside the BMP.
inline constexpr std::uint32_t supplementary_first = 0x10000;
/// @brief First printable ASCII code point.
inline constexpr std::uint32_t first_printable = 0x20;
/// @brief DEL.
inline constexpr std::uint32_t delete_char = 0x7F;
/// @brief Last C1 control code point.
inline constexpr std::uint32_t last_c1_control = 0x9F;
/// @brief Section sign, a HOI4 colour/format control.
inline constexpr std::uint32_t section_sign = 0xA7;
/// @brief Pound sign, a HOI4 icon control.
inline constexpr std::uint32_t pound_sign = 0xA3;

/// @brief Copied UTF-32 text of an edit box.
struct InputSnapshot {
    std::uint32_t points[max_codepoints]{};  ///< Code points.
    std::size_t size{};                      ///< Number of valid code points.
};

/// @brief Copied UTF-8 carrier text plus its decoded code points.
struct Utf8Snapshot {
    char bytes[max_utf8 + 1]{};  ///< UTF-8 bytes, NUL-terminated.
    std::size_t size{};          ///< Byte count.
    InputSnapshot decoded{};     ///< Filled by `decode_utf8`.
};

/**
 * @brief Checks a bounded child list (data pointer, capacity, count) inside a container.
 * @param container Container object.
 * @param offset Offset of the list.
 * @return `true` when count/capacity are sane and the element array is readable.
 */
inline bool bounded_list(const void* container, std::size_t offset) noexcept {
    void* data = nullptr;
    int capacity = 0;
    int count = 0;
    return member(container, offset + off::list_data, data) &&
           member(container, offset + off::list_capacity, capacity) &&
           member(container, offset + off::list_count, count) && count >= 0 && count <= max_list_count &&
           capacity >= count && capacity <= max_list_capacity &&
           readable(data, static_cast<std::size_t>(count) * sizeof(void*));
}

/// @brief Largest bucket mask accepted for a child name map (65536 buckets).
inline constexpr int max_map_mask = 0xFFFF;

/**
 * @brief Checks a child name hash map (entries pointer, bucket mask) inside a container.
 * @param container Container object.
 * @param offset Offset of the map.
 * @return `true` when the mask is sane and the bucket array is readable.
 */
inline bool bounded_map(const void* container, std::size_t offset) noexcept {
    void* entries = nullptr;
    int mask = -1;
    return member(container, offset + off::map_entries, entries) && member(container, offset + off::map_mask, mask) &&
           mask >= 0 && mask <= max_map_mask &&
           readable(entries, (static_cast<std::size_t>(mask) + 1) * off::map_entry_size);
}

/**
 * @brief Reads whether `edit` currently receives keyboard input.
 * @param edit CEditBox.
 * @param result Receives `true` when the manager's receiver is this edit and edit+281 bit 0 is set.
 * @return `false` when the manager or flags are unreadable.
 */
inline bool read_input_active(void* edit, bool& result) noexcept {
    void* manager = nullptr;
    void* receiver = nullptr;
    unsigned char flags = 0;
    if (!member(reinterpret_cast<void*>(image_base), rva::manager_global, manager) ||
        !member(manager, off::manager_receiver, receiver) || !member(edit, off::edit_flags, flags)) {
        return false;
    }
    result = (flags & flag::edit_active) != 0 &&
             reinterpret_cast<std::uintptr_t>(receiver) == reinterpret_cast<std::uintptr_t>(edit) + off::edit_receiver;
    return true;
}

/**
 * @brief Convenience: readable and active.
 * @param edit CEditBox.
 * @return `true` only when the state is readable and active.
 */
inline bool input_active(void* edit) noexcept {
    bool result = false;
    return read_input_active(edit, result) && result;
}

/**
 * @brief Widget-level visibility of an edit box (flag byte at +75).
 * @param edit CEditBox.
 * @return `true` when shown and not hidden.
 */
inline bool visible_edit(void* edit) noexcept {
    unsigned char flags = 0;
    return member(edit, off::edit_visibility_flags, flags) && flag::shown_not_hidden(flags);
}

/**
 * @brief Identity check of one of our edit boxes: three vtables plus the definition name.
 * @param edit Candidate widget.
 * @param expected Exact definition name.
 * @return `true` when every check passes.
 */
inline bool edit_named(void* edit, const char* expected) noexcept {
    std::uintptr_t edit_vt = 0;
    std::uintptr_t tb_vt = 0;
    std::uintptr_t definition_vt = 0;
    void* definition = nullptr;
    return member(edit, 0, edit_vt) && edit_vt == image_base + rva::vt_edit && member(edit, off::edit_buffer, tb_vt) &&
           tb_vt == image_base + rva::vt_text_buffer && member(edit, off::edit_definition, definition) &&
           member(definition, 0, definition_vt) && definition_vt == image_base + rva::vt_edit_definition &&
           name_equals(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(definition) + off::definition_name),
                       expected);
}

/**
 * @brief Name-independent part of `edit_named`: returns the definition-name address.
 * @param edit Candidate widget.
 * @param name_address Receives the address of the definition name string.
 * @return `true` when the three vtables match.
 * @note Used to classify an edit box against several names with one set of reads; the
 *       result equals calling `edit_named` for each name in turn.
 */
inline bool edit_identity(void* edit, const void*& name_address) noexcept {
    std::uintptr_t edit_vt = 0;
    std::uintptr_t tb_vt = 0;
    std::uintptr_t definition_vt = 0;
    void* definition = nullptr;
    const bool identified = member(edit, 0, edit_vt) && edit_vt == image_base + rva::vt_edit &&
                            member(edit, off::edit_buffer, tb_vt) && tb_vt == image_base + rva::vt_text_buffer &&
                            member(edit, off::edit_definition, definition) && member(definition, 0, definition_vt) &&
                            definition_vt == image_base + rva::vt_edit_definition;
    if (identified) {
        name_address =
            reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(definition) + off::definition_name);
    }
    return identified;
}

/**
 * @brief Copies an own edit box's UTF-32 text.
 * @param edit CEditBox.
 * @param expected Definition name it must have.
 * @param result Receives the text.
 * @return `false` on identity mismatch, oversize text or unreadable storage.
 */
inline bool snapshot_edit(void* edit, const char* expected, InputSnapshot& result) noexcept {
    std::uint64_t size = 0;
    std::uint64_t capacity = 0;
    if (!edit_named(edit, expected) || !member(edit, off::edit_size, size) ||
        !member(edit, off::edit_capacity, capacity) || size > max_codepoints || size > capacity ||
        capacity > max_edit_capacity) {
        return false;
    }
    const void* text = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit) + off::edit_text);
    if (capacity > core::value::inline_utf32_capacity && !member(edit, off::edit_text, text)) {
        return false;
    }
    if (!guarded_copy(result.points, text, static_cast<std::size_t>(size) * sizeof(result.points[0]))) {
        return false;
    }
    result.size = static_cast<std::size_t>(size);
    return true;
}

/**
 * @brief Compares two snapshots.
 * @param a First.
 * @param b Second.
 * @return `true` when equal.
 */
inline bool same_text(const InputSnapshot& a, const InputSnapshot& b) noexcept {
    return a.size == b.size && std::memcmp(a.points, b.points, a.size * sizeof(a.points[0])) == 0;
}

/**
 * @brief Decodes carrier UTF-8 into code points, rejecting controls and markup characters.
 * @param value Snapshot whose `bytes`/`size` are set; `decoded` is filled.
 * @return `false` on invalid UTF-8, unpaired surrogates, C0/C1 controls, `[`, `]`, `$`,
 *         `§`, `£`, empty input or more than 128 code points.
 * @note No trimming, normalization, substitution or truncation of the game name.
 */
inline bool decode_utf8(Utf8Snapshot& value) noexcept {
    wchar_t utf16[max_utf8]{};
    if (value.size == 0 || value.size > max_utf8) {
        return false;
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.bytes, static_cast<int>(value.size),
                                          utf16, static_cast<int>(max_utf8));
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        std::uint32_t cp = static_cast<std::uint16_t>(utf16[i]);
        if (cp >= high_surrogate_first && cp <= high_surrogate_last) {
            if (++i >= count) {
                return false;
            }
            const auto low = static_cast<std::uint32_t>(static_cast<std::uint16_t>(utf16[i]));
            if (low < low_surrogate_first || low > low_surrogate_last) {
                return false;
            }
            cp = supplementary_first + ((cp - high_surrogate_first) << 10) + (low - low_surrogate_first);
        } else if (cp >= low_surrogate_first && cp <= low_surrogate_last) {
            return false;
        }
        // Also reject unresolved bracket expressions and common render controls.
        if (cp < first_printable || (cp >= delete_char && cp <= last_c1_control) || cp == '[' || cp == ']' ||
            cp == '$' || cp == section_sign || cp == pound_sign) {
            return false;
        }
        if (value.decoded.size == max_codepoints) {
            return false;
        }
        value.decoded.points[value.decoded.size++] = cp;
    }
    return true;
}

/**
 * @brief Reads an instantTextBox carrier's text by widget name under `root`.
 * @param root Root widget.
 * @param name Carrier widget name.
 * @param result Receives bytes (and code points when `decode`).
 * @param decode Also run `decode_utf8`.
 * @return `false` when the widget is missing, of another type, unreadable or invalid.
 */
inline bool carrier(void* root, const char* name, Utf8Snapshot& result, bool decode) noexcept {
    void* widget = guarded_find(find_instant, root, name);
    std::uintptr_t vt = 0;
    if (!member(widget, 0, vt) || vt != image_base + rva::vt_instant_text) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(widget);
    if (address > core::uptr_max - off::instant_text ||
        !string_bytes(reinterpret_cast<void*>(address + off::instant_text), result.bytes, max_utf8, result.size)) {
        return false;
    }
    return !decode || decode_utf8(result);
}

}  // namespace capie::engine
