/**
 * @file subideology_layer_hooks.hpp
 * @brief Hook body and cache of the ruling-party ideology name layer (API level 8, CONTRACT.md section 20).
 *
 * One hook: `panel(view)` = the politics view's ruling-party writer (`rva::politics_panel`, one
 * caller: the view's observer refresh, on the GUI thread). The writer localises the ruling
 * party's ideology token for the "ideology" text through the formatter, which calls the hooked
 * `lookup_n`. When the shown country (the in-game idler's tag, read the way its slot 0xA0 computes
 * it) has a cache entry whose binding equals its current government, the original runs with this
 * thread's `country::ideology_frame` set to {token, layer text}; `lookup_n` answers exactly that
 * key with the text while the frame is set, and every other key passes through. No game call is
 * added and nothing is written.
 *
 * Cache: per country with a valid layer, {country, list index, tag, text, revision, binding},
 * written on the logic thread by `update(country)` (UpdateNames hook: load, government and name
 * changes) and `refresh_all()` (after `set_variable` of `capie_sn_rev`). An entry keeps its
 * binding while its text and revision are unchanged, so a later change of the ruling party or its
 * leader lapses the name; a new revision binds it to the current government (CONTRACT 20.3).
 * The hook takes a shared lock to copy one entry; with no entry it costs one atomic load.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../../core/scoped_frame.hpp"
#include "country_names.hpp"
#include "subideology_layer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace capie::country::subideology {

/// @brief `void PoliticsRulingPartyPanel(CCountryPoliticsView*)`.
using PanelFn = void (*)(void*);
/// @brief Log sink.
using LogFn = void (*)(core::logging::Channel, const char*);

/// @brief Most countries visited (sanity bound, as in the other layers).
inline constexpr std::int32_t max_countries = 4096;
/// @brief Debug lines of each kind.
inline constexpr int max_debug_lines = 8;
/// @brief Size of a log line buffer.
inline constexpr std::size_t log_line_bytes = 200;

/// @brief One country's bound name.
struct Entry {
    uptr country{};        ///< Country object.
    std::int32_t index{};  ///< Its index in the game-state country list.
    std::int32_t tag{};    ///< Its tag id (`country + off::country_tag`).
    std::string text;      ///< Layer text (UTF-8).
    std::int64_t rev{};    ///< `capie_sn_rev` it was bound with.
    Binding binding;       ///< Government it was bound to.
};

/// @brief Counters of one pass.
struct Stats {
    std::int32_t countries{};  ///< Countries visited.
    std::int32_t layers{};     ///< Countries with a valid layer.
    std::int32_t bound{};      ///< Entries (re)bound in this pass.
    std::int32_t kept{};       ///< Entries that kept their binding.
    bool list_ok{};            ///< The country list was readable.
};

/**
 * @brief The ruling-party ideology name layer of one process.
 * @tparam M Memory policy with `read` (`core::ProcessMemory` in the game, a fake in tests).
 */
template <class M>
class IdeologyLayer {
public:
    /// @brief Trampoline; MinHook writes it before the hook is enabled.
    struct Originals {
        PanelFn panel{};  ///< Ruling-party panel writer.
    };

    /**
     * @brief Creates an inactive layer.
     * @param mem Memory policy (must outlive the object).
     */
    explicit IdeologyLayer(const M& mem) noexcept : mem_(mem) {}

    /**
     * @brief The trampoline slot for `HookSpec`.
     * @return Mutable reference.
     */
    Originals& originals() noexcept { return originals_; }

    /**
     * @brief Activates the layer after its pins and RTTI checks passed and the hook was installed.
     * @param image Image base.
     * @param log Log sink (may be null).
     */
    void activate(uptr image, LogFn log) noexcept {
        image_ = image;
        log_ = log;
        enabled_.store(originals_.panel != nullptr);
    }

    /// @brief Turns the layer off (the hook passes through).
    void deactivate() noexcept { enabled_.store(false); }

    /// @brief Whether the layer is active. @return State.
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

    /// @brief Cached entries (tests and diagnostics). @return Count.
    [[nodiscard]] std::size_t entries() const noexcept { return count_.load(); }

    /**
     * @brief Re-evaluates one country (logic thread; UpdateNames hook, after the original).
     * @param country Country object.
     * @return `true` when it has an entry afterwards.
     */
    bool update(uptr country) noexcept {
        if (!enabled_.load() || country == 0) {
            return false;
        }
        uptr list = 0;
        std::int32_t count = 0;
        std::int32_t index = 0;
        if (!country_list(list, count) || !index_of(list, count, country, index)) {
            return false;
        }
        Stats stats{};
        try {
            const std::unique_lock lock(mutex_);
            if (!entries_) {
                entries_ = std::make_unique<std::vector<Entry>>();
            }
            std::vector<Entry>& all = *entries_;
            const auto found =
                std::find_if(all.begin(), all.end(), [country](const Entry& entry) { return entry.country == country; });
            Entry entry{};
            const bool has = evaluate(country, index, found != all.end() ? &*found : nullptr, entry, stats);
            if (found != all.end()) {
                if (has) {
                    *found = std::move(entry);
                } else {
                    all.erase(found);
                }
            } else if (has) {
                all.push_back(std::move(entry));
            }
            count_.store(all.size(), std::memory_order_release);
            return has;
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure leaves the game's name
            return false;
        }
    }

    /**
     * @brief Re-evaluates every country (logic thread; after a `capie_sn_rev` commit).
     * @return Counters.
     */
    Stats refresh_all() noexcept {
        Stats stats{};
        uptr list = 0;
        std::int32_t count = 0;
        if (!enabled_.load() || !country_list(list, count)) {
            return stats;
        }
        stats.list_ok = true;
        try {
            auto fresh = std::make_unique<std::vector<Entry>>();
            {
                const std::shared_lock lock(mutex_);
                for (std::int32_t index = 0; index < count; ++index) {
                    uptr country = 0;
                    if (!core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), country) || country == 0) {
                        continue;
                    }
                    ++stats.countries;
                    const Entry* old = nullptr;
                    if (entries_) {
                        const auto found = std::find_if(entries_->begin(), entries_->end(),
                                                        [country](const Entry& entry) { return entry.country == country; });
                        old = found != entries_->end() ? &*found : nullptr;
                    }
                    Entry entry{};
                    if (evaluate(country, index, old, entry, stats)) {
                        fresh->push_back(std::move(entry));
                    }
                }
            }
            const std::unique_lock lock(mutex_);
            entries_.swap(fresh);
            count_.store(entries_->size(), std::memory_order_release);
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure keeps the previous cache
            return stats;
        }
        note(core::logging::Channel::lifecycle,
             "CAPIE_SUBIDEOLOGY commit list_ok=1 countries=%d layers=%d bound=%d kept=%d", stats.countries,
             stats.layers, stats.bound, stats.kept);
        return stats;
    }

    /**
     * @brief Hook body of the ruling-party panel writer.
     * @param view `CCountryPoliticsView`.
     * @note Not `noexcept`: it calls the original, whose exceptions pass through (scan N6); the
     *       thread's frame is restored by a destructor.
     */
    void panel(void* view) {
        if (count_.load(std::memory_order_acquire) != 0 && enabled_.load()) {
            Prepared prepared{};
            if (prepare(reinterpret_cast<uptr>(view), prepared)) {
                const IdeologyFrame frame{prepared.binding.key.data(), prepared.binding.key_size,
                                          prepared.text.c_str()};
                const core::ScopedFrame<const IdeologyFrame> scope(ideology_frame, &frame);
                originals_.panel(view);
                if (shown_lines_.fetch_add(1) < max_debug_lines) {
                    note(core::logging::Channel::debug, "CAPIE_SUBIDEOLOGY panel answered tag=%d key=%s",
                         prepared.tag, prepared.binding.key.data());
                }
                return;
            }
        }
        originals_.panel(view);
    }

    /**
     * @brief The text a country's entry holds (tests and diagnostics).
     * @param country Country.
     * @return Text, empty when there is no entry.
     */
    std::string text_of(uptr country) const {
        const std::shared_lock lock(mutex_);
        if (!entries_) {
            return {};
        }
        for (const auto& entry : *entries_) {
            if (entry.country == country) {
                return entry.text;
            }
        }
        return {};
    }

private:
    /// @brief What the hook needs for one panel write.
    struct Prepared {
        Binding binding;       ///< Current government (equals the entry's).
        std::string text;      ///< Layer text.
        std::int32_t tag{};    ///< Shown tag (diagnostics).
    };

    /**
     * @brief Reads the country list.
     * @param list Receives the array.
     * @param count Receives the count.
     * @return `false` when unreadable or implausible.
     */
    bool country_list(uptr& list, std::int32_t& count) const noexcept {
        uptr state = 0;
        return core::rd(mem_, image_, core::rva::gamestate_global, state) && state != 0 &&
               core::rd(mem_, state, core::off::gamestate_countries, list) &&
               core::rd(mem_, state, core::off::gamestate_country_count, count) && list != 0 && count >= 0 &&
               count <= max_countries;
    }

    /**
     * @brief Index of a country in the list: its tag int when that slot holds it, else a scan.
     * @param list Country array.
     * @param count Count.
     * @param country Country.
     * @param index Receives the index.
     * @return `false` when the country is not in the list.
     */
    bool index_of(uptr list, std::int32_t count, uptr country, std::int32_t& index) const noexcept {
        std::int32_t tag = 0;
        uptr at = 0;
        if (core::rd(mem_, country, core::off::country_tag, tag) && tag >= 0 && tag < count &&
            core::rd(mem_, list, static_cast<uptr>(tag) * sizeof(uptr), at) && at == country) {
            index = tag;
            return true;
        }
        for (std::int32_t i = 0; i < count; ++i) {
            if (core::rd(mem_, list, static_cast<uptr>(i) * sizeof(uptr), at) && at == country) {
                index = i;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief The entry a country has after this evaluation (CONTRACT 20.3).
     * @param country Country.
     * @param index Its list index.
     * @param old Its current entry (may be null).
     * @param out Receives the entry.
     * @param stats Counters.
     * @return `false` when the country has no entry (no valid layer, no ruling party or an unreadable chain).
     */
    bool evaluate(uptr country, std::int32_t index, const Entry* old, Entry& out, Stats& stats) const {
        const auto layer = read_ideology_layer(mem_, country);
        if (layer.text.status != Status::valid) {
            return false;
        }
        ++stats.layers;
        std::int32_t tag = 0;
        if (!core::rd(mem_, country, core::off::country_tag, tag)) {
            return false;
        }
        const auto text = layer.text.view();
        if (old != nullptr && old->text == text && old->rev == layer.rev && old->index == index && old->tag == tag) {
            out = *old;  // unchanged: keep the government it was bound to
            ++stats.kept;
            return true;
        }
        Binding binding{};
        if (!read_binding(mem_, image_, country, binding)) {
            return false;
        }
        out.country = country;
        out.index = index;
        out.tag = tag;
        out.text.assign(text);
        out.rev = layer.rev;
        out.binding = binding;
        ++stats.bound;
        if (bind_lines_.fetch_add(1) < max_debug_lines) {
            note(core::logging::Channel::debug, "CAPIE_SUBIDEOLOGY bound idx=%d key=%s leader_id=%d points=%zu",
                 index, binding.key.data(), binding.leader_id, layer.text.name_points);
        }
        return true;
    }

    /**
     * @brief The shown tag, as the in-game idler's slot 0xA0 computes it.
     * @param view Politics view.
     * @param tag Receives the tag id.
     * @return `false` when the view's idler is not the `CInGameIdler` or the game state is unreadable.
     */
    bool shown_tag(uptr view, std::int32_t& tag) const noexcept {
        uptr idler = 0;
        uptr state = 0;
        std::int32_t shown = 0;
        if (!core::rd(mem_, view, core::off::view_idler, idler) ||
            !has_vtable(mem_, idler, image_ + core::rva::vt_ingame_idler) ||
            !core::rd(mem_, image_, core::rva::gamestate_global, state) || state == 0 ||
            !core::rd(mem_, state, core::off::gamestate_shown_tag, shown)) {
            return false;
        }
        if (shown > 0) {
            tag = shown;
            return true;
        }
        return core::rd(mem_, state, core::off::gamestate_player_tag, tag);
    }

    /**
     * @brief Finds the shown country's entry and checks it against the current government (GUI thread).
     * @param view Politics view.
     * @param out Receives the binding and text.
     * @return `true` when the panel should show the layer.
     */
    bool prepare(uptr view, Prepared& out) const noexcept {
        std::int32_t tag = 0;
        if (!shown_tag(view, tag)) {
            return false;
        }
        Entry entry{};
        try {
            const std::shared_lock lock(mutex_);
            if (!entries_) {
                return false;
            }
            const auto found = std::find_if(entries_->begin(), entries_->end(),
                                            [tag](const Entry& candidate) { return candidate.tag == tag; });
            if (found == entries_->end()) {
                return false;
            }
            entry = *found;
        } catch (...) {
            return false;
        }
        uptr list = 0;
        std::int32_t count = 0;
        uptr at = 0;
        std::int32_t current = 0;
        if (!country_list(list, count) || entry.index < 0 || entry.index >= count ||
            !core::rd(mem_, list, static_cast<uptr>(entry.index) * sizeof(uptr), at) || at != entry.country ||
            !core::rd(mem_, entry.country, core::off::country_tag, current) || current != tag) {
            return false;  // another game, or the country left its slot
        }
        if (!read_binding(mem_, image_, entry.country, out.binding) || !out.binding.same(entry.binding)) {
            if (lapsed_lines_.fetch_add(1) < max_debug_lines) {
                note(core::logging::Channel::debug, "CAPIE_SUBIDEOLOGY lapsed tag=%d", tag);
            }
            return false;  // the ruling party or its leader changed: the game's name
        }
        try {
            out.text = std::move(entry.text);
        } catch (...) {
            return false;
        }
        out.tag = tag;
        return true;
    }

    /**
     * @brief Formats and writes one log line.
     * @param channel Channel.
     * @param format printf format.
     * @param args Arguments (numbers and ideology tokens; never the layer text).
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

    const M& mem_;                                   ///< Memory policy.
    Originals originals_{};                          ///< Trampoline.
    uptr image_{};                                   ///< Image base.
    LogFn log_{};                                    ///< Log sink.
    std::atomic<bool> enabled_{false};               ///< Layer active.
    std::atomic<std::size_t> count_{0};              ///< Entries (fast path when 0).
    mutable std::shared_mutex mutex_;                ///< Guards `entries_`; never held across a game call.
    std::unique_ptr<std::vector<Entry>> entries_;    ///< Countries with a bound name (created on first use).
    mutable std::atomic<int> bind_lines_{0};         ///< Debug line budget.
    mutable std::atomic<int> lapsed_lines_{0};       ///< Debug line budget.
    mutable std::atomic<int> shown_lines_{0};        ///< Debug line budget.
};

}  // namespace capie::country::subideology
