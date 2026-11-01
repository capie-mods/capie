/**
 * @file catalog_dispatch.hpp
 * @brief Catalog views in `capie_editor.dll` (API level 9): registry, the `vp` action, labels, refresh.
 *
 * A mod names a scripted GUI `capie_v1_cat__<mod>__<view>` (`catalog_names.hpp`). CAPIE registers
 * it at Bind (at most 16 views per process, 4 per mod id; slots are never freed) and publishes its
 * text keys empty, so they answer from then on. A click of `…__<action>_click` is handled in the
 * Dispatch hook, on the UI thread, before the command is sent, and is always forwarded once:
 *
 * - `vp`: checks `ThreadForbidCount == 0` (else sentinel -5), reads the scope state's VP provinces
 *   (`vp_view.hpp`), asks the game's `ProvinceName` for each shown name, publishes the labels
 *   (`CAPIE_V1_T__<MOD>__<VIEW>__<k>`, `__COUNT`, `__PAGE`, `__SEL`) to `capie_core.dll`, and adds
 *   the records of `catalog_records.hpp`. Every 500 ms while the window is visible the Tick hook
 *   reads the names of the same provinces again (at most 32 calls), so a rename shows without a
 *   click.
 * - Every other action (the flag and portrait catalogs, paging, pick): forwarded with
 *   `capie_catalog = 0` until those catalogs ship (pending the spikes S1/S2).
 *
 * A window that is not visible or fails CAPIE's chain check gets sentinel -1, a full registry -2.
 * Without the catalog pins, RTTI or the core's publish export, every action carries
 * `capie_catalog = 0`. A fault in a game call latches and turns game calls off for both DLLs.
 * CAPIE writes no game state: only its records and its own label store.
 */
#pragma once
#include "../../core/rtti.hpp"
#include "../editor/public_editor.hpp"
#include "catalog_names.hpp"
#include "catalog_pins.hpp"
#include "catalog_records.hpp"
#include "catalog_text.hpp"
#include "vp_view.hpp"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace capie::catalog {

using editor::Origin;
using engine::MsvcString;
using names::Action;
using names::EditorId;

/// @brief Most views per process.
inline constexpr std::size_t max_views = 16;
/// @brief Most views per mod id.
inline constexpr std::size_t max_views_per_mod = 4;
/// @brief Label refresh interval while a VP view is visible.
inline constexpr unsigned long long refresh_ms = 500;
/// @brief Longest label CAPIE copies from the game.
inline constexpr std::size_t max_label = text::max_text;

/// @brief One registered view (immutable names; the VP part is UI-thread state).
struct View {
    EditorId id{};                 ///< Mod and view ids.
    editor::grammar::Name model;   ///< Model name.
    std::int32_t state{};          ///< State of the last `vp` click (0 = none).
    std::array<std::int32_t, vp::max_rows> rows{};  ///< Province ids of the last `vp` click.
    std::size_t row_count{};       ///< Rows.
    unsigned long long refreshed{};  ///< Last label refresh (GetTickCount64).
};

/// @brief Function slots and switches (set at initialization).
struct Runtime {
    bool ok{};                         ///< Pins, RTTI and the publish export are in place.
    engine::ProvinceName name{};       ///< The game's ProvinceName.
    engine::GameDelete release{};      ///< The game's operator delete (string release).
    text::PublishFn publish{};         ///< `RNCatalogPublish` of capie_core.dll.
};

/// @brief Runtime of this process.
inline Runtime runtime{};
/// @brief Registered views; slots below `view_count` have immutable names.
inline View views[max_views]{};
/// @brief Published slots.
inline std::atomic<std::size_t> view_count{0};
/// @brief Serialises registrations.
inline SRWLOCK view_lock = SRWLOCK_INIT;
/// @brief Some view holds VP rows (Tick fast path).
inline std::atomic<bool> any_vp{false};

/**
 * @brief Writes one `CAPIE_CATALOG` line (lifecycle; ids, counts and names CAPIE built, never a label).
 * @param format printf format.
 * @param args Arguments.
 */
template <class... Args>
void note(const char* format, Args... args) noexcept {
    std::array<char, 320> line{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): bounded snprintf with a literal format
    const int count = std::snprintf(line.data(), line.size() - 2, format, args...);
    if (count <= 0) {
        return;
    }
    auto size = static_cast<std::size_t>(count) < line.size() - 2 ? static_cast<std::size_t>(count) : line.size() - 3;
    line[size++] = '\r';
    line[size++] = '\n';
    editor::write_log(line.data(), size);
}

/**
 * @brief Arms the catalog views (editor initialization, after the window pins).
 * @param text `.text` of the game.
 * @param image Image base.
 */
inline void prepare(const core::TextSection& text, std::uintptr_t image) noexcept {
    const char* failed = pins::first_failed_vp_pin(text);
    if (failed == nullptr) {
        failed = core::first_wrong_class(core::ProcessMemory{}, image, core::catalog_vtable_classes);
    }
    HMODULE core_module = nullptr;
    text::PublishFn publish = nullptr;
    if (failed == nullptr) {
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, core::core_dll_name, &core_module) != 0 &&
            core_module != nullptr) {
            publish = reinterpret_cast<text::PublishFn>(GetProcAddress(core_module, text::publish_export));
        }
        if (publish == nullptr) {
            failed = "core.publish_export";
        }
    }
    if (failed != nullptr) {
        note("CAPIE_CATALOG off %s; catalog actions carry capie_catalog = 0", failed);
        return;
    }
    runtime.name = reinterpret_cast<engine::ProvinceName>(image + core::rva::province_name);
    runtime.release = reinterpret_cast<engine::GameDelete>(image + core::rva::game_delete);
    runtime.publish = publish;
    runtime.ok = true;
    editor::public_catalog_ready.store(true, std::memory_order_release);
    note("CAPIE_CATALOG on: vp view (flag and portrait catalogs pending the spikes)");
}

/**
 * @brief Whether the catalog feature is on (also the `capie_catalog` record of a grammar Apply).
 * @return `true` while armed and game calls are allowed.
 */
inline bool ready() noexcept {
    return runtime.ok && engine::game_calls_allowed();
}

/**
 * @brief Publishes one label to the core (UI thread).
 * @param key Key.
 * @param label Text.
 */
inline void publish(const names::Key& key, const char* label) noexcept {
    if (runtime.publish == nullptr || key.size == 0) {
        return;
    }
    __try {
        static_cast<void>(runtime.publish(key.c_str(), label));
    } __except (core::memory_exception(GetExceptionCode())) {
        runtime.publish = nullptr;
    }
}

/**
 * @brief Publishes the empty labels of a fresh view (its keys answer from now on).
 * @param id Ids.
 */
inline void publish_empty(const EditorId& id) noexcept {
    for (std::size_t k = 0; k < vp::max_rows; ++k) {
        publish(names::row_key(id, k), "");
    }
    publish(names::text_key(id, "COUNT"), "0");
    publish(names::text_key(id, "PAGE"), "0 / 0");
    publish(names::text_key(id, "SEL"), "");
}

/**
 * @brief Finds or registers a view (limits of spec 3.3).
 * @param model Model name.
 * @param index Receives the slot.
 * @return `true` when registered (now or before).
 */
inline bool register_view(std::string_view model, std::size_t& index) noexcept {
    index = max_views;
    const auto parsed = names::parse_model(model);
    if (!parsed) {
        return false;
    }
    auto count = view_count.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < count; ++i) {
        if (views[i].model.view() == model) {
            index = i;
            return true;
        }
    }
    bool added = false;
    AcquireSRWLockExclusive(&view_lock);
    count = view_count.load(std::memory_order_relaxed);
    std::size_t same_mod = 0;
    for (std::size_t i = 0; i < count && index == max_views; ++i) {
        if (views[i].model.view() == model) {
            index = i;
        }
        same_mod += views[i].id.mod_view() == parsed->mod_view() ? 1U : 0U;
    }
    if (index == max_views && count < max_views && same_mod < max_views_per_mod) {
        views[count].id = *parsed;
        views[count].model = names::view_model(*parsed);
        index = count;
        view_count.store(count + 1, std::memory_order_release);
        added = true;
    }
    ReleaseSRWLockExclusive(&view_lock);
    if (added) {
        note("CAPIE_CATALOG registered view=%s slot=%zu", views[index].model.c_str(), index);
        publish_empty(views[index].id);
    }
    return index != max_views;
}

/**
 * @brief Reads the model object and name of a context.
 * @param context Window context.
 * @param model Receives the model object.
 * @param name Receives the name.
 * @param size Receives its length.
 * @return `true` when the context has a model whose name may be a catalog view.
 */
inline bool context_model(void* context, void*& model, char (&name)[editor::public_name_buffer],
                          std::size_t& size) noexcept {
    std::uintptr_t vtable = 0;
    std::uintptr_t model_vt = 0;
    if (!editor::member(context, 0, vtable) || vtable != engine::image_base + core::rva::vt_sg_context ||
        !editor::member(context, core::off::context_model, model) || !editor::member(model, 0, model_vt) ||
        model_vt != engine::image_base + core::rva::vt_sg_model) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(model);
    return address <= core::uptr_max - core::off::model_name &&
           engine::string_bytes(reinterpret_cast<void*>(address + core::off::model_name), name, sizeof(name) - 1,
                                size) &&
           names::may_be_model(std::string_view(name, size));
}

/**
 * @brief Reads the calling thread's `ThreadForbidCount`.
 * @return The counter, or -1 when unreadable.
 */
inline std::int32_t thread_forbid() noexcept {
    std::int32_t count = -1;
    const auto tls = static_cast<std::uintptr_t>(__readgsqword(0x58));  // NOLINT(readability-magic-numbers): TEB ThreadLocalStoragePointer
    return vp::thread_forbid_count(core::ProcessMemory{}, engine::image_base, tls, count) ? count : -1;
}

/**
 * @brief The shown name of a province (the game's ProvinceName), copied and released.
 * @param province Province object.
 * @param out Receives the bytes, NUL-terminated (empty on failure).
 * @return `false` when the game call failed (the latch is then set).
 */
inline bool province_label(std::uintptr_t province, std::array<char, max_label + 1>& out) noexcept {
    out[0] = '\0';
    MsvcString name{};
    if (!engine::guarded_province_name(runtime.name, province, &name)) {
        return false;
    }
    const auto size = name.size < max_label ? static_cast<std::size_t>(name.size) : max_label;
    if (name.capacity <= engine::sso_capacity) {
        std::memcpy(out.data(), name.data.small, size < sizeof(name.data.small) ? size : sizeof(name.data.small) - 1);
        out[size < sizeof(name.data.small) ? size : sizeof(name.data.small) - 1] = '\0';
    } else if (name.capacity <= engine::max_string_capacity && name.size <= name.capacity &&
               core::ProcessMemory::read(reinterpret_cast<std::uintptr_t>(name.data.pointer), out.data(), size)) {
        out[size] = '\0';
    }
    if (!engine::guarded_string_release(runtime.release, &name)) {
        note("CAPIE_CATALOG a province name string was leaked (release refused)");
    }
    return true;
}

/**
 * @brief Publishes the labels of a VP view (UI thread).
 * @param view Registered view.
 * @param list Rows.
 * @return `false` when a game call failed.
 */
inline bool publish_vp(const View& view, const vp::List& list) noexcept {
    std::array<char, max_label + 1> label{};
    bool ok = true;
    for (std::size_t k = 0; k < vp::max_rows; ++k) {
        label[0] = '\0';
        if (k < list.count && ok) {
            ok = province_label(list.rows[k].province, label);
        }
        publish(names::row_key(view.id, k), label.data());
    }
    std::array<char, 16> number{};
    static_cast<void>(std::snprintf(number.data(), number.size(), "%zu", list.count));
    publish(names::text_key(view.id, "COUNT"), number.data());
    publish(names::text_key(view.id, "PAGE"), list.count != 0 ? "1 / 1" : "0 / 0");
    return ok;
}

/**
 * @brief Builds the game records of an action and dispatches the click once.
 * @param current Frame (context, names set).
 * @param opaque_selector Selector argument.
 * @param list Records to add.
 * @return `false` when the click's records could not be copied (then forwarded unchanged).
 * @note Not `noexcept`: it calls the original dispatch.
 */
inline bool forward(Origin& current, void* opaque_selector, const records::List& list) {
    editor::TempVector source{};
    std::size_t kept = 0;
    std::size_t sanitized = 0;
    bool built = !list.overflow && editor::original_ctor != nullptr &&
                 editor::copy_original_records(current, source, kept, sanitized) == editor::CopyResult::ok;
    for (std::size_t i = 0; built && i < list.count; ++i) {
        const auto& item = list.items[i];
        const auto magnitude = static_cast<std::uint64_t>(item.value < 0 ? -item.value : item.value);
        if (kept >= editor::max_original_records + editor::max_frame_records ||
            !editor::transport_record(current.records[kept], item.name.data(), magnitude)) {
            built = false;
            break;
        }
        if (item.value < 0) {
            current.records[kept].raw_value = -current.records[kept].raw_value;
        }
        ++kept;
    }
    if (!built) {
        note("CAPIE_CATALOG records not built for view=%s; click forwarded unchanged", current.public_model);
        editor::original_dispatch(current.context, opaque_selector);
        return false;
    }
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    {
        const editor::OriginFrame frame(&current);
        editor::original_dispatch(current.context, opaque_selector);
    }
    if (current.ctor_calls != 1) {
        note("CAPIE_CATALOG view=%s constructor calls=%u (records not carried)", current.public_model,
             current.ctor_calls);
    }
    return true;
}

/**
 * @brief The `vp` action (UI thread, ThreadForbidCount already 0).
 * @param slot View slot.
 * @param context Window context.
 * @return Records.
 */
inline records::List vp_action(std::size_t slot, void* context) noexcept {
    std::int32_t state = 0;
    vp::List list{};
    const auto scope = reinterpret_cast<std::uintptr_t>(context) + core::off::context_scope;
    if (!vp::scope_state(core::ProcessMemory{}, scope, state)) {
        note("CAPIE_CATALOG vp scope unreadable view=%s", views[slot].model.c_str());
        return records::unavailable();
    }
    const auto read = vp::read_state(core::ProcessMemory{}, engine::image_base, state, list);
    if (read == vp::Read::unreadable) {
        note("CAPIE_CATALOG vp state %d unreadable view=%s", state, views[slot].model.c_str());
        return records::unavailable();
    }
    if (read == vp::Read::no_state) {
        list = vp::List{};
    }
    if (!publish_vp(views[slot], list)) {
        note("CAPIE_CATALOG ProvinceName failed (fault latch); catalog off");
        runtime.ok = false;
        return records::unavailable();
    }
    auto& view = views[slot];
    view.state = list.state;
    view.row_count = list.count;
    for (std::size_t k = 0; k < list.count; ++k) {
        view.rows[k] = list.rows[k].id;
    }
    view.refreshed = GetTickCount64();
    any_vp.store(list.count != 0 || any_vp.load(), std::memory_order_release);
    note("CAPIE_CATALOG vp view=%s state=%d rows=%zu dropped=%zu", view.model.c_str(), list.state, list.count,
         list.dropped);
    return records::vp_view(list);
}

/**
 * @brief Dispatch hook entry for catalog views.
 * @param context Window context.
 * @param opaque_selector Selector.
 * @return `false` when the click is not a catalog action (the caller continues); `true` when it was
 *         forwarded exactly once here.
 * @note Not `noexcept`: it calls the original dispatch.
 */
inline bool dispatch(void* context, void* opaque_selector) {
    void* model = nullptr;
    char name[editor::public_name_buffer]{};
    std::size_t size = 0;
    if (!context_model(context, model, name, size)) {
        return false;
    }
    const auto id = names::parse_model(std::string_view(name, size));
    char selector[editor::grammar::name_capacity]{};
    std::size_t selector_size = 0;
    if (!id || !engine::string_bytes(opaque_selector, selector, sizeof(selector) - 1, selector_size)) {
        return false;
    }
    const auto action = names::parse_action(*id, std::string_view(selector, selector_size));
    if (!action) {
        return false;  // another button of the window: plain script
    }
    std::size_t slot = max_views;
    const bool registered = register_view(std::string_view(name, size), slot);
    const auto selector_name = names::view_action(*id, *action);
    Origin current{};
    current.context = context;
    current.model_name =
        reinterpret_cast<const MsvcString*>(reinterpret_cast<std::uintptr_t>(model) + core::off::model_name);
    current.selector = static_cast<const MsvcString*>(opaque_selector);
    current.id = editor::next_observation_id();
    current.public_model = registered ? views[slot].model.c_str() : name;
    current.public_apply = selector_name.c_str();
    editor::View window{};
    records::List list{};
    if (editor::public_view(context, model, window) != editor::ViewResult::visible) {
        list = records::sentinel(records::Sentinel::window);
    } else if (!registered) {
        list = records::sentinel(records::Sentinel::registry_full);
    } else if (!ready() || *action != Action::vp) {
        list = records::unavailable();  // off, or a catalog pending the spikes
    } else if (thread_forbid() != 0) {
        list = records::sentinel(records::Sentinel::thread);
    } else {
        list = vp_action(slot, context);
    }
    std::int64_t catalog = 0;
    static_cast<void>(list.find("capie_catalog", catalog));
    note("CAPIE_CATALOG action=%s view=%.*s capie_catalog=%lld records=%zu",
         names::action_names[static_cast<std::size_t>(*action)].data(), static_cast<int>(size), name,
         static_cast<long long>(catalog), list.count);
    static_cast<void>(forward(current, opaque_selector, list));
    return true;
}

/**
 * @brief Bind hook entry: registers a catalog view and publishes its empty keys.
 * @param window Bound window.
 */
inline void bind(void* window) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    if (address == 0 || address > core::uptr_max - core::off::window_context) {
        return;
    }
    void* model = nullptr;
    char name[editor::public_name_buffer]{};
    std::size_t size = 0;
    std::size_t slot = max_views;
    if (context_model(reinterpret_cast<void*>(address + core::off::window_context), model, name, size)) {
        static_cast<void>(register_view(std::string_view(name, size), slot));
    }
}

/**
 * @brief Tick hook entry: refreshes the labels of a visible VP view at most every 500 ms.
 * @param window Ticking window (pre-filtered as possibly ours).
 */
inline void tick(void* window) noexcept {
    if (!any_vp.load(std::memory_order_acquire) || !ready()) {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    if (address == 0 || address > core::uptr_max - core::off::window_context) {
        return;
    }
    void* context = reinterpret_cast<void*>(address + core::off::window_context);
    void* model = nullptr;
    char name[editor::public_name_buffer]{};
    std::size_t size = 0;
    std::size_t slot = max_views;
    if (!context_model(context, model, name, size)) {
        return;
    }
    const auto count = view_count.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < count; ++i) {
        if (views[i].model.view() == std::string_view(name, size)) {
            slot = i;
        }
    }
    const auto now = GetTickCount64();
    if (slot == max_views || views[slot].row_count == 0 || now < views[slot].refreshed + refresh_ms) {
        return;
    }
    editor::View shown{};
    if (editor::inspect_chain(window, model, shown) != editor::ViewResult::visible || thread_forbid() != 0) {
        return;
    }
    auto& view = views[slot];
    view.refreshed = now;
    vp::List fresh{};
    if (vp::read_state(core::ProcessMemory{}, engine::image_base, view.state, fresh) != vp::Read::ok) {
        return;
    }
    // The same provinces in the same order as the click's records; a province that left the list
    // (its VP was removed) keeps an empty label until the next click.
    vp::List ordered{};
    ordered.state = view.state;
    ordered.count = view.row_count;
    for (std::size_t k = 0; k < view.row_count; ++k) {
        ordered.rows[k].id = view.rows[k];
        for (std::size_t j = 0; j < fresh.count; ++j) {
            if (fresh.rows[j].id == view.rows[k]) {
                ordered.rows[k] = fresh.rows[j];
            }
        }
    }
    std::array<char, max_label + 1> label{};
    for (std::size_t k = 0; k < ordered.count; ++k) {
        label[0] = '\0';
        if (ordered.rows[k].province != 0 && !province_label(ordered.rows[k].province, label)) {
            runtime.ok = false;
            note("CAPIE_CATALOG ProvinceName failed in refresh (fault latch); catalog off");
            return;
        }
        publish(names::row_key(view.id, k), label.data());
    }
}

}  // namespace capie::catalog
