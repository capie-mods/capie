/**
 * @file window_view.hpp
 * @brief Fresh inspection of a scripted window: is it one of our editors, and is it visible?
 *
 * `inspect_window` is the editor bridge's identity and visibility guard. A window is ours
 * only when its model has the scripted-GUI model vtable and one of the three exact editor
 * model names. It is valid only when its embedded context points back to the same window,
 * model and root, the root has the root vtable, the first 0x1360 bytes of the root are
 * readable and its three child lists have sane bounds. It is visible when the visible
 * trigger byte is set and the root's flags say shown-not-hidden. Each call reads
 * everything again; the returned root pointer is valid for the current call only.
 *
 * `read_epoch` reads the opening counter that the script writes into the editor's epoch
 * carrier (decimal digits, 1..max, no sign).
 *
 * `window_may_be_own` is the per-frame pre-filter added for performance. The Tick hook
 * runs for every scripted window of the game, every frame, and four observers used to
 * repeat their own model check for each one. The pre-filter reads the model pointer, its
 * vtable and its name once and compares the name with every model name any observer
 * accepts (three editors and two caption launchers). If it says "no", every observer
 * would have returned "unrelated" without side effects, so they are skipped. It uses the
 * looser byte comparison of the caption/focus cores, which accepts a superset of what the
 * strict bridge comparison accepts, so it can never hide a window an observer wants. If
 * it says "maybe", each observer still runs its full fresh guard.
 */
#pragma once
#include "../../engine/game_calls.hpp"
#include "../../engine/msvc_string.hpp"
#include "../../engine/widgets.hpp"
#include "schema.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

namespace capie::editor {

using engine::bounded_list;
using engine::carrier;
using engine::image_base;
using engine::member;
using engine::name_equals;
using engine::readable;
using engine::Utf8Snapshot;
namespace rva = core::rva;
namespace off = core::off;
namespace flag = core::flag;

/// @brief Result of `inspect_window`.
enum class ViewResult {
    unrelated,  ///< Not one of our editor windows.
    invalid,    ///< Ours by name, but the context/root chain is inconsistent.
    hidden,     ///< Ours and valid, not visible.
    visible,    ///< Ours, valid and visible.
};

/// @brief Copied result of a window inspection (pointer valid within the current call only).
struct View {
    void* root{};         ///< Root widget.
    std::size_t model{};  ///< Model index (valid unless `unrelated`).
};

/**
 * @brief Inspects a window freshly.
 * @param window Scripted window passed to the hook.
 * @param out Receives the model index (when recognized) and root (when valid).
 * @return Classification; see `ViewResult`.
 */
inline ViewResult inspect_window(void* window, View& out) noexcept {
    void* model = nullptr;
    std::uintptr_t model_vt = 0;
    if (!member(window, off::window_model, model) || !member(model, 0, model_vt) ||
        model_vt != image_base + rva::vt_sg_model) {
        return ViewResult::unrelated;
    }
    const auto model_address = reinterpret_cast<std::uintptr_t>(model);
    if (model_address > core::uptr_max - off::model_name) {
        return ViewResult::unrelated;
    }
    bool recognized = false;
    for (std::size_t i = 0; i < model_count; ++i) {
        if (name_equals(reinterpret_cast<void*>(model_address + off::model_name), models[i].name)) {
            out.model = i;
            recognized = true;
            break;
        }
    }
    if (!recognized) {
        return ViewResult::unrelated;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    if (address > core::uptr_max - off::window_context) {
        return ViewResult::invalid;
    }
    void* context = reinterpret_cast<void*>(address + off::window_context);
    std::uintptr_t ctx_vt = 0;
    std::uintptr_t root_vt = 0;
    void* ctx_window = nullptr;
    void* ctx_model = nullptr;
    void* ctx_root = nullptr;
    unsigned char flags = 0;
    unsigned char desired = 0;
    if (!member(context, 0, ctx_vt) || ctx_vt != image_base + rva::vt_sg_context ||
        !member(context, off::context_window, ctx_window) || ctx_window != window ||
        !member(context, off::context_model, ctx_model) || ctx_model != model ||
        !member(context, off::context_root, ctx_root) || !member(window, off::window_root, out.root) ||
        ctx_root != out.root || !member(out.root, 0, root_vt) || root_vt != image_base + rva::vt_sg_root ||
        !member(out.root, off::root_flags, flags) || !member(window, off::window_desired, desired) ||
        !readable(out.root, off::root_readable_span) || !bounded_list(out.root, off::root_edit_list) ||
        !bounded_list(out.root, off::root_instant_list) || !bounded_list(out.root, off::root_third_list)) {
        return ViewResult::invalid;
    }
    return desired != 0 && flag::shown_not_hidden(flags) ? ViewResult::visible : ViewResult::hidden;
}

/**
 * @brief Reads the opening epoch from a model's epoch carrier.
 * @param root Root widget of a freshly inspected visible window.
 * @param model Model index.
 * @param result Receives the epoch (>= 1).
 * @return `false` when the carrier is missing, empty, too long, non-decimal, zero or
 *         too large to scale by `fixed_point`.
 */
inline bool read_epoch(void* root, std::size_t model, std::uint64_t& result) noexcept {
    Utf8Snapshot text{};
    if (!carrier(root, models[model].epoch, text, false) || text.size == 0 || text.size > max_epoch_digits) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < text.size; ++i) {
        const auto c = static_cast<unsigned char>(text.bytes[i]);
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10) + c - '0';  // NOLINT(readability-magic-numbers): decimal base
    }
    if (value == 0 || value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)() / fixed_point)) {
        return false;
    }
    result = value;
    return true;
}

/// @brief Every model name any Tick observer accepts (editor bridge, caption, focus).
inline constexpr const char* tick_model_names[] = {
    models[0].name, models[1].name, models[2].name, caption_launcher_models[0], caption_launcher_models[1],
};

/**
 * @brief Per-frame pre-filter: could any Tick observer treat this window as its own?
 * @param window Ticking window.
 * @return `false` only when every observer would classify the window as unrelated.
 * @note One model-pointer read, one vtable read, one string header read and at most one
 *       name read; no lock, no allocation, no formatting.
 */
inline bool window_may_be_own(void* window) noexcept {
    void* model = nullptr;
    std::uintptr_t model_vt = 0;
    if (!member(window, off::window_model, model) || !member(model, 0, model_vt) ||
        model_vt != image_base + rva::vt_sg_model) {
        return false;
    }
    const auto model_address = reinterpret_cast<std::uintptr_t>(model);
    if (model_address > core::uptr_max - off::model_name) {
        return true;  // let the observers' own guards decide
    }
    // Same acceptance rule as engine::string_equals, with the header and bytes read once.
    engine::MsvcString header{};
    if (!engine::guarded_copy(&header, reinterpret_cast<const void*>(model_address + off::model_name), sizeof(header)) ||
        header.size > header.capacity || header.capacity > engine::max_string_capacity ||
        header.size > engine::max_name_bytes) {
        return false;
    }
    char bytes[engine::max_name_bytes + 1]{};
    bool bytes_read = false;
    for (const char* candidate : tick_model_names) {
        const auto wanted = std::strlen(candidate);
        if (header.size != wanted) {
            continue;
        }
        if (!bytes_read) {
            if (header.capacity <= engine::sso_capacity) {
                std::memcpy(bytes, header.data.small, sizeof(header.data.small));
            } else if (!engine::guarded_copy(bytes, header.data.pointer, static_cast<std::size_t>(header.size))) {
                return false;
            }
            bytes_read = true;
        }
        if (std::memcmp(bytes, candidate, wanted) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace capie::editor
