/**
 * @file name_layer_hooks.hpp
 * @brief Hook bodies of the country name layer: priority layer > cosmetic tag > base name.
 *
 * Three hooks, all on the game's logic path, none on the per-frame localisation path:
 *
 * - `update_names(country)` (716580). Reads the country's layer (`name_layer.hpp`), remembers
 *   it for this thread and calls the original once. The original computes the three keys with
 *   `BuildNameKey`, localises them into the country's cached name, adjective and definite name
 *   and marks the map labels dirty when they changed.
 * - `build_name_key(out, tag, ...)` (6ECFF0). While an `update_names` call on this thread has a
 *   valid layer, the original is called with the tag argument replaced by a borrowed string
 *   `RN1|<name>|<adjective>|`; otherwise the arguments pass through unchanged. The original
 *   only copies the tag (report 1.2). The existing localisation hooks answer the resulting
 *   `RN1|...` keys; ideology variants of that tag do not exist, so the game falls back to the
 *   base form exactly as for an RN1 cosmetic tag. Without a layer the original sees the real
 *   cosmetic tag (or the base tag), so the cosmetic tag wins over the base name.
 * - `set_variable(effect, context)` (364C60). After the original, if the effect's variable is
 *   `capie_cn_rev`, every country whose layer differs from what it currently shows is refreshed
 *   through `update_names`. The effect runs at the same step of the same command on every
 *   multiplayer client; the refresh changes only the game's display caches.
 *
 * CAPIE writes no game state: no variable, no cosmetic tag, no `CCountry` field. Fail-closed:
 * an unreadable or invalid layer is "no layer" and the call passes through unchanged. Every
 * hook calls its original exactly once, holds no lock while the game runs and never throws.
 *
 * Logs (debug flavor): the first `UpdateNames`, the first `set_variable`, the first commit and
 * one line per refreshed country with counts only. Release: one line per commit with counts.
 * No line ever contains the text of a name.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../../engine/msvc_string.hpp"
#include "name_layer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace capie::country::layer {

/// @brief `u64 UpdateNames(CCountry*)`.
using UpdateNamesFn = std::uint64_t (*)(uptr);
/// @brief `string* BuildNameKey(string* out, const string* tag, ...)`, 8 arguments.
using BuildNameKeyFn = void* (*)(void*, const void*, void*, void*, void*, void*, void*, char);
/// @brief `void Execute(effect*, context*)`.
using SetVariableFn = void (*)(uptr, uptr);
/// @brief Log sink: one NUL-terminated line without line break.
using LogFn = void (*)(core::logging::Channel, const char*);

/// @brief Most countries a refresh pass visits (sanity bound; the game has a few hundred tags).
inline constexpr std::int32_t max_countries = 4096;
/// @brief Entries kept in the "what each country shows" map before it is cleared.
inline constexpr std::size_t max_shown_entries = 8192;
/// @brief Size of a log line buffer.
inline constexpr std::size_t log_line_bytes = 200;

/// @brief Counters of one refresh pass (logged; no text).
struct RefreshStats {
    std::int32_t countries{};   ///< Countries visited.
    std::int32_t layers{};      ///< Countries with a valid layer.
    std::int32_t refreshed{};   ///< `UpdateNames` calls made.
    std::int32_t invalid{};     ///< Countries with `capie_cn_ver` but invalid values.
    std::int32_t unreadable{};  ///< Countries whose variables could not be read.
    bool list_ok{};             ///< The country list was readable.
};

/**
 * @brief The layer state of one process: originals, memory policy and the shown-key map.
 * @tparam M Memory policy (`core::ProcessMemory` in the game, a fake in tests).
 */
template <class M>
class NameLayer {
public:
    /**
     * @brief Creates an inactive layer.
     * @param mem Memory policy (must outlive the object).
     */
    explicit NameLayer(const M& mem) noexcept : mem_(mem) {}

    /// @brief Trampolines; MinHook writes them when it creates the hooks, before any hook is enabled.
    struct Originals {
        UpdateNamesFn update{};        ///< UpdateNames.
        BuildNameKeyFn build{};        ///< BuildNameKey.
        SetVariableFn set_variable{};  ///< SetVariable::Execute.
    };
    /**
     * @brief The trampolines; `HookSpec` points MinHook at these slots.
     * @return Mutable reference to the slots.
     */
    Originals& originals() noexcept { return originals_; }

    /**
     * @brief Activates the layer (after every pin passed and the hooks were installed).
     * @param image Image base (for the game-state global).
     * @param log Log sink (may be null).
     */
    void activate(uptr image, LogFn log) noexcept {
        image_ = image;
        log_ = log;
        enabled_.store(originals_.update != nullptr && originals_.build != nullptr &&
                       originals_.set_variable != nullptr);
    }

    /// @brief Turns the layer off (hooks then pass everything through). Used by tests and on failure.
    void deactivate() noexcept { enabled_.store(false); }

    /// @brief Whether the layer reads and substitutes. @return State.
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

    /**
     * @brief Hook body of UpdateNames.
     * @param country Country object.
     * @return The original's result.
     */
    std::uint64_t update_names(uptr country) noexcept {
        Frame frame{};
        if (enabled_.load()) {
            frame.layer = read_layer(mem_, country);
            remember(country, frame.layer);
            if (!first_update_logged_.exchange(true)) {
                note(core::logging::Channel::debug, "CAPIE_NAME first UpdateNames on thread %lu",
                     static_cast<unsigned long>(GetCurrentThreadId()));
            }
        }
        Frame* const previous = active_frame;
        active_frame = &frame;
        const auto result = originals_.update(country);
        active_frame = previous;
        return result;
    }

    /**
     * @brief Hook body of BuildNameKey.
     * @param out Result string.
     * @param tag Tag string (cosmetic tag or base tag).
     * @param a3 Passed through.
     * @param a4 Passed through.
     * @param a5 Passed through.
     * @param a6 Passed through.
     * @param a7 Suffix string, passed through.
     * @param a8 Passed through.
     * @return The original's result.
     */
    void* build_name_key(void* out, const void* tag, void* a3, void* a4, void* a5, void* a6, void* a7,
                         char a8) noexcept {
        const Frame* const frame = active_frame;
        if (frame != nullptr && frame->layer.status == Status::valid && enabled_.load()) {
            const auto key = frame->layer.view();
            const auto borrowed = engine::borrowed_string(key.data(), key.size());
            return originals_.build(out, &borrowed, a3, a4, a5, a6, a7, a8);
        }
        return originals_.build(out, tag, a3, a4, a5, a6, a7, a8);
    }

    /**
     * @brief Hook body of SetVariable::Execute.
     * @param effect Effect object.
     * @param context Execution context.
     */
    void set_variable(uptr effect, uptr context) noexcept {
        originals_.set_variable(effect, context);
        if (!enabled_.load()) {
            return;
        }
        if (!first_set_logged_.exchange(true)) {
            note(core::logging::Channel::debug, "CAPIE_NAME first set_variable observed on thread %lu",
                 static_cast<unsigned long>(GetCurrentThreadId()));
        }
        if (!engine::string_equals(mem_, effect + core::off::set_variable_name, commit_variable)) {
            return;
        }
        if (!first_commit_logged_.exchange(true)) {
            note(core::logging::Channel::debug, "CAPIE_NAME first commit (set_variable %s)", commit_variable);
        }
        const auto stats = refresh_changed();
        note(core::logging::Channel::lifecycle,
             "CAPIE_NAME commit list_ok=%u countries=%d layers=%d refreshed=%d invalid=%d unreadable=%d",
             stats.list_ok ? 1U : 0U, stats.countries, stats.layers, stats.refreshed, stats.invalid, stats.unreadable);
    }

    /**
     * @brief Refreshes every country whose layer differs from what it shows.
     * @return Counters.
     */
    RefreshStats refresh_changed() noexcept {
        RefreshStats stats{};
        uptr state = 0;
        uptr list = 0;
        std::int32_t count = 0;
        if (!core::rd(mem_, image_, core::rva::gamestate_global, state) || state == 0 ||
            !core::rd(mem_, state, core::off::gamestate_countries, list) ||
            !core::rd(mem_, state, core::off::gamestate_country_count, count) || list == 0 || count < 0 ||
            count > max_countries) {
            return stats;
        }
        stats.list_ok = true;
        for (std::int32_t index = 0; index < count; ++index) {
            uptr country = 0;
            if (!core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), country) || country == 0) {
                continue;
            }
            ++stats.countries;
            const auto layer = read_layer(mem_, country);
            stats.layers += layer.status == Status::valid ? 1 : 0;
            stats.invalid += layer.status == Status::invalid ? 1 : 0;
            stats.unreadable += layer.status == Status::unreadable ? 1 : 0;
            if (!differs(country, layer)) {
                continue;
            }
            note(core::logging::Channel::debug,
                 "CAPIE_NAME refresh slot=%d status=%s ver_raw=%lld name_points=%zu adjective_points=%zu", index,
                 status_name(layer.status), static_cast<long long>(layer.version_raw), layer.name_points,
                 layer.adjective_points);
            static_cast<void>(update_names(country));
            ++stats.refreshed;
        }
        return stats;
    }

    /**
     * @brief The key a country showed at its last UpdateNames (tests and diagnostics).
     * @param country Country object.
     * @return Key, empty when it showed no layer or was never seen.
     */
    std::string shown_key(uptr country) const {
        const std::scoped_lock lock(shown_mutex_);
        if (!shown_) {
            return {};
        }
        const auto found = shown_->find(country);
        return found == shown_->end() ? std::string{} : found->second;
    }

private:
    /// @brief Country -> the key it shows (absent = no layer).
    using ShownMap = std::unordered_map<uptr, std::string>;

    /// @brief The layer of the UpdateNames call running on this thread.
    struct Frame {
        Layer layer;  ///< Layer read at the start of the call.
    };

    /**
     * @brief Records what a country will show (called before the original runs).
     * @param country Country object.
     * @param layer Its layer.
     */
    void remember(uptr country, const Layer& layer) noexcept {
        try {
            const std::scoped_lock lock(shown_mutex_);
            if (!shown_) {
                shown_ = std::make_unique<ShownMap>();
            }
            if (shown_->size() >= max_shown_entries) {
                shown_->clear();
            }
            if (layer.status == Status::valid) {
                (*shown_)[country].assign(layer.view());
            } else {
                shown_->erase(country);
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure only costs an extra refresh later
        }
    }

    /**
     * @brief Whether a country's layer differs from what it shows.
     * @param country Country object.
     * @param layer Current layer.
     * @return `true` when a refresh is needed.
     */
    bool differs(uptr country, const Layer& layer) const noexcept {
        try {
            const std::scoped_lock lock(shown_mutex_);
            if (!shown_) {
                return layer.status == Status::valid;
            }
            const auto found = shown_->find(country);
            if (layer.status != Status::valid) {
                return found != shown_->end();
            }
            return found == shown_->end() || found->second != layer.view();
        } catch (...) {
            return layer.status == Status::valid;
        }
    }

    /**
     * @brief Formats and writes one log line.
     * @param channel Channel.
     * @param format printf format.
     * @param args Arguments (numbers and fixed strings only, never a name).
     */
    template <class... Args>
    void note(core::logging::Channel channel, const char* format, Args... args) const noexcept {
        if (log_ == nullptr || !core::logging::enabled(channel)) {
            return;
        }
        std::array<char, log_line_bytes> line{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): bounded snprintf with a literal format
        if (std::snprintf(line.data(), line.size(), format, args...) > 0) {
            log_(channel, line.data());
        }
    }

    const M& mem_;                                  ///< Memory policy.
    Originals originals_{};                         ///< Trampolines.
    uptr image_{};                                  ///< Image base.
    LogFn log_{};                                   ///< Log sink.
    std::atomic<bool> enabled_{false};              ///< Layer active.
    std::atomic<bool> first_update_logged_{false};  ///< Debug line written.
    std::atomic<bool> first_set_logged_{false};     ///< Debug line written.
    std::atomic<bool> first_commit_logged_{false};  ///< Debug line written.
    mutable std::mutex shown_mutex_;                ///< Guards `shown_`; never held across a game call.
    std::unique_ptr<ShownMap> shown_;               ///< Country -> key it shows; created on first use.
    /// @brief Frame of the UpdateNames call running on this thread (one per thread, all instances).
    static inline thread_local Frame* active_frame = nullptr;
};

}  // namespace capie::country::layer
