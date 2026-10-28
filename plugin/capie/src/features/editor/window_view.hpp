/**
 * @file window_view.hpp
 * @brief Fresh inspection of a scripted window: is it one of our editors, and is it visible?
 *
 * `inspect_window` is the editor bridge's identity and visibility guard. A window is ours
 * only when its model has the scripted-GUI model vtable and one of the three exact editor
 * model names. It is valid only when its embedded context points back to the same window,
 * model and root, the root has the root vtable and is not being destroyed, the first 0x17D8
 * bytes of the root are readable, and its child vector and two name maps have sane bounds. It
 * is visible when the visible trigger byte is set and the root's flags say shown-not-hidden.
 * Each call reads everything again; the returned root pointer is valid for the current call only.
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
 * it says "maybe", each observer still runs its full fresh guard. Names that start with the
 * public grammar prefix (`capie_v1_ed__`, see `public_names.hpp`) also pass, so the
 * grammar-editor observer can parse them; every other observer rejects them by name.
 *
 * `tick_window_may_be_own` is the form the Tick hook uses (Ghidra review P1/P2): it reads the
 * model pointer with an SEH-only copy and looks the model's verdict up in `model_verdicts`, so
 * a foreign window costs one read and a few compares instead of three `VirtualQuery` calls.
 * The Bind hook refreshes the verdict of every model it binds (`remember_bound_model`).
 */
#pragma once
#include "../../engine/game_calls.hpp"
#include "../../engine/msvc_string.hpp"
#include "../../engine/widgets.hpp"
#include "public_names.hpp"
#include "schema.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace capie::editor {

using engine::bounded_list;
using engine::bounded_map;
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
 * @brief Checks the window/context/root chain of a recognized window and its visibility.
 * @param window Scripted window passed to the hook.
 * @param model Its model object (already identified by name).
 * @param out Receives the root (when valid); `out.model` is left to the caller.
 * @return `invalid`, `hidden` or `visible`.
 */
inline ViewResult inspect_chain(void* window, void* model, View& out) noexcept {
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
    void* parent = nullptr;
    unsigned char flags = 0;
    unsigned char desired = 0;
    // The window factory returns the PARENT when it cannot create the window (Bind, 1.19.4.0): a
    // root equal to the parent has the right class but is not this window's root. A root with the
    // destroyed bit is being dropped by the game (review G3): invalid, so no game call is made.
    if (!member(context, 0, ctx_vt) || ctx_vt != image_base + rva::vt_sg_context ||
        !member(context, off::context_window, ctx_window) || ctx_window != window ||
        !member(context, off::context_model, ctx_model) || ctx_model != model ||
        !member(context, off::context_root, ctx_root) || !member(window, off::window_root, out.root) ||
        ctx_root != out.root || !member(window, off::window_parent, parent) || parent == out.root ||
        !member(out.root, 0, root_vt) || root_vt != image_base + rva::vt_sg_root ||
        !member(out.root, off::root_flags, flags) || flag::is_destroyed(flags) ||
        !member(window, off::window_desired, desired) ||
        !readable(out.root, off::root_readable_span) || !bounded_list(out.root, off::root_children) ||
        !bounded_map(out.root, off::root_edit_map) || !bounded_map(out.root, off::root_instant_map)) {
        return ViewResult::invalid;
    }
    return desired != 0 && flag::shown_not_hidden(flags) ? ViewResult::visible : ViewResult::hidden;
}

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
    return inspect_chain(window, model, out);
}

/**
 * @brief Reads an opening epoch from a named carrier.
 * @param root Root widget of a freshly inspected visible window.
 * @param name Epoch carrier name.
 * @param result Receives the epoch (>= 1).
 * @return `false` when the carrier is missing, empty, too long, non-decimal, zero or
 *         too large to scale by `fixed_point`.
 */
inline bool read_epoch_carrier(void* root, const char* name, std::uint64_t& result) noexcept {
    Utf8Snapshot text{};
    if (!carrier(root, name, text, false) || text.size == 0 || text.size > max_epoch_digits) {
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
    if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / fixed_point)) {
        return false;
    }
    result = value;
    return true;
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
    return read_epoch_carrier(root, models[model].epoch, result);
}

/// @brief Every model name any Tick observer accepts (editor bridge, caption, focus).
inline constexpr const char* tick_model_names[] = {
    models[model_index::leader].name,
    models[model_index::party].name,
    models[model_index::country].name,
    leader_launcher_model,
    party_launcher_model,
};

/// @brief Fresh verdict on a window's model (template) for the Tick pre-filter.
enum class ModelClass {
    foreign,     ///< Read completely: no observer can treat a window of this model as its own.
    maybe_own,   ///< An observer may own it; the observers run their full guards.
    unreadable,  ///< A read failed: "not ours" for this call, never remembered.
};

/**
 * @brief Classifies a model (template) object freshly, with guarded reads.
 * @param model Model pointer read from the window.
 * @return See `ModelClass`.
 * @note One vtable read, one string header read and at most one name read; no lock, no
 *       allocation, no formatting.
 */
inline ModelClass classify_model(void* model) noexcept {
    std::uintptr_t model_vt = 0;
    if (!member(model, 0, model_vt)) {
        return ModelClass::unreadable;
    }
    if (model_vt != image_base + rva::vt_sg_model) {
        return ModelClass::foreign;
    }
    const auto model_address = reinterpret_cast<std::uintptr_t>(model);
    if (model_address > core::uptr_max - off::model_name) {
        return ModelClass::maybe_own;  // let the observers' own guards decide
    }
    // Same acceptance rule as engine::string_equals, with the header and bytes read once.
    engine::MsvcString header{};
    if (!engine::guarded_copy(&header, reinterpret_cast<const void*>(model_address + off::model_name),
                              sizeof(header))) {
        return ModelClass::unreadable;
    }
    if (header.size > header.capacity || header.capacity > engine::max_string_capacity ||
        header.size > engine::max_name_bytes) {
        return ModelClass::foreign;
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
                return ModelClass::unreadable;
            }
            bytes_read = true;
        }
        if (std::memcmp(bytes, candidate, wanted) == 0) {
            return ModelClass::maybe_own;
        }
    }
    // Grammar editors of other mods (public contract): prefix and length only; the public
    // observer parses the whole name with its own fresh read.
    if (header.size < grammar::min_model_name || header.size > grammar::max_model_name) {
        return ModelClass::foreign;
    }
    if (!bytes_read) {
        if (header.capacity <= engine::sso_capacity) {
            std::memcpy(bytes, header.data.small, sizeof(header.data.small));
        } else if (!engine::guarded_copy(bytes, header.data.pointer, static_cast<std::size_t>(header.size))) {
            return ModelClass::unreadable;
        }
    }
    return grammar::may_be_model(std::string_view(bytes, static_cast<std::size_t>(header.size))) ? ModelClass::maybe_own
                                                                                                  : ModelClass::foreign;
}

/**
 * @brief Per-frame pre-filter, fresh form: could any Tick observer treat this window as its own?
 * @param window Ticking window.
 * @return `false` only when every observer would classify the window as unrelated.
 */
inline bool window_may_be_own(void* window) noexcept {
    void* model = nullptr;
    return member(window, off::window_model, model) && classify_model(model) == ModelClass::maybe_own;
}

/**
 * @brief Verdicts of the Tick pre-filter, remembered per model (template) pointer (review P2/O2).
 *
 * Why a model pointer is a safe key: the game builds every scripted window in the manager's
 * rebuild, which calls Bind on the new window before it puts the window into the array Tick
 * walks, and a rebind also goes through Bind. A window never changes its model pointer, and a
 * model's name never changes. So a verdict can only go stale when a model is freed and a new
 * model is allocated at the same address, and then every window of the new model is bound
 * before its first Tick. The Bind hook therefore classifies the model again and overwrites the
 * entry (`remember`). Windows bound before CAPIE was active are classified at their first Tick.
 *
 * The key is compared, never dereferenced. Each slot is one atomic word, `model | verdict` in
 * the low two bits (a model is 8-byte aligned; any other pointer is never remembered), so a
 * reader on another thread sees either an old or a new entry, never a torn one. Only `foreign`
 * and `maybe_own` are remembered; a failed read is decided again on the next Tick. When a
 * probe run is full the table is cleared, which only costs fresh classifications.
 */
class ModelVerdicts {
public:
    /// @brief Slots (power of two); vanilla plus large overhaul mods define a few hundred models.
    static constexpr std::size_t capacity = 1024;
    /// @brief Slots probed from the home slot before the table is treated as full.
    static constexpr std::size_t max_probe = 16;

    /**
     * @brief The remembered verdict of a model.
     * @param model Model pointer (compared only).
     * @param maybe_own Receives the verdict when one is remembered.
     * @return `false` when no verdict is remembered.
     */
    bool find(std::uintptr_t model, bool& maybe_own) const noexcept {
        if (!cacheable(model)) {
            return false;
        }
        for (std::size_t i = 0; i < max_probe; ++i) {
            const auto entry = slots_[slot(model, i)].load(std::memory_order_relaxed);
            if (entry == 0) {
                return false;
            }
            if ((entry & ~tag_mask) == model) {
                maybe_own = (entry & tag_mask) == tag_own;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Remembers (or replaces) the verdict of a model.
     * @param model Model pointer (compared only).
     * @param verdict Fresh verdict; `unreadable` removes nothing and remembers nothing.
     */
    void remember(std::uintptr_t model, ModelClass verdict) noexcept {
        if (!cacheable(model) || verdict == ModelClass::unreadable) {
            return;
        }
        const std::uintptr_t entry = model | (verdict == ModelClass::maybe_own ? tag_own : tag_foreign);
        for (std::size_t i = 0; i < max_probe; ++i) {
            auto& cell = slots_[slot(model, i)];
            const auto current = cell.load(std::memory_order_relaxed);
            if (current == 0 || (current & ~tag_mask) == model) {
                cell.store(entry, std::memory_order_relaxed);
                return;
            }
        }
        clear();
        slots_[slot(model, 0)].store(entry, std::memory_order_relaxed);
    }

    /// @brief Forgets every verdict.
    void clear() noexcept {
        for (auto& cell : slots_) {
            cell.store(0, std::memory_order_relaxed);
        }
    }

private:
    /// @brief Low bits that carry the verdict.
    static constexpr std::uintptr_t tag_mask = 3;
    /// @brief Verdict bits of `maybe_own`.
    static constexpr std::uintptr_t tag_own = 1;
    /// @brief Verdict bits of `foreign`.
    static constexpr std::uintptr_t tag_foreign = 2;
    /// @brief Fibonacci hashing multiplier (2^64 / golden ratio).
    static constexpr std::uint64_t hash_multiplier = 0x9E3779B97F4A7C15ULL;
    /// @brief Right shift that leaves log2(capacity) bits of the product.
    static constexpr unsigned hash_shift = 54;
    static_assert((std::size_t{1} << (64 - hash_shift)) == capacity);

    /**
     * @brief Whether a model pointer can be a key (non-null, low verdict bits free).
     * @param model Model pointer.
     * @return `true` when it can be remembered.
     */
    static bool cacheable(std::uintptr_t model) noexcept { return model != 0 && (model & tag_mask) == 0; }

    /**
     * @brief Slot index of the `i`-th probe for a model.
     * @param model Model pointer.
     * @param i Probe number.
     * @return Index below `capacity`.
     */
    static std::size_t slot(std::uintptr_t model, std::size_t i) noexcept {
        const auto home = static_cast<std::size_t>((static_cast<std::uint64_t>(model) * hash_multiplier) >> hash_shift);
        return (home + i) & (capacity - 1);
    }

    std::atomic<std::uintptr_t> slots_[capacity]{};  ///< `model | verdict`, 0 = empty.
};

/// @brief Pre-filter verdicts of this process (Tick and Bind hooks).
inline ModelVerdicts model_verdicts{};

/**
 * @brief Per-frame pre-filter used by the Tick hook: remembered verdict, or a fresh one.
 *
 * The model pointer is read with `seh_copy` (review P1): the original Tick has just read the
 * same window object, so the `VirtualQuery` of a guarded read buys nothing here. A fault means
 * "not ours". Only a model never seen before is classified with guarded reads.
 * @param window Ticking window (the game's live object).
 * @return `false` only when every observer would classify the window as unrelated.
 */
inline bool tick_window_may_be_own(void* window) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    std::uintptr_t model = 0;
    if (address > core::uptr_max - off::window_model ||
        !core::seh_copy(&model, reinterpret_cast<const void*>(address + off::window_model), sizeof(model)) ||
        model == 0) {
        return false;
    }
    bool maybe_own = false;
    if (model_verdicts.find(model, maybe_own)) {
        return maybe_own;
    }
    const auto verdict = classify_model(reinterpret_cast<void*>(model));
    model_verdicts.remember(model, verdict);
    return verdict == ModelClass::maybe_own;
}

/**
 * @brief Bind hook: classifies the bound window's model again and replaces its verdict.
 * @param window Window the original Bind has just bound.
 */
inline void remember_bound_model(void* window) noexcept {
    void* model = nullptr;
    if (!member(window, off::window_model, model) || model == nullptr) {
        return;  // no model pointer: Tick finds none either
    }
    const auto verdict = classify_model(model);
    if (verdict == ModelClass::unreadable) {
        model_verdicts.clear();  // an old verdict for this address must not survive (rare)
        return;
    }
    model_verdicts.remember(reinterpret_cast<std::uintptr_t>(model), verdict);
}

}  // namespace capie::editor
