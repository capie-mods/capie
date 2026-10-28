/**
 * @file flag_layer_hooks.hpp
 * @brief Hook bodies of the country flag layer (API level 6, CONTRACT.md section 17).
 *
 * HOI4 keeps country flags in three texture atlases with one slot per country and reloads a
 * slot only when its key (ideology group, flag tag, cosmetic id) changes. Three hooks:
 *
 * - `load_flag(atlas, country, ...)` = `LoadCountryFlag` (`rva::load_country_flag`). When the cache says the
 *   country shows a flag layer, it sets a thread-local frame {country, stem} and calls the
 *   original. If the original's status is not success (0 or 5), it calls the original a second
 *   time without the frame, which loads the game's own flag (fail-closed; the incremental path
 *   would otherwise mark the slot empty).
 * - `candidates(out, tag, cosmetic, group)` = `BuildFlagCandidates` (`rva::build_flag_candidates`). Inside such a
 *   frame, and only when `cosmetic` is the frame country's own cosmetic tag field (proof that the
 *   call comes from `load_flag` for that country), the original runs with tag = stem, cosmetic =
 *   "" and no ideology group, so the only candidate is `<stem>.tga`.
 * - `atlas_update(atlas, gfx)` = `UpdateFlagAtlas` (`rva::flag_atlas_update`), per frame per atlas. When the cache
 *   generation moved since this atlas last looked, it writes an impossible cosmetic id (all ones)
 *   into the slot key of every country whose layer changed, before the original runs; the
 *   original then reloads those slots through its own path.
 *
 * Threads: the cache is written on the logic thread (`update` from the UpdateNames hook,
 * `refresh_all` after a `capie_cf_rev` commit). `load_flag` and `candidates` also run on TBB
 * worker threads during a full atlas rebuild (review K1), so they only take a shared lock and
 * use thread-local frames; `atlas_update` runs on the idler thread that owns the atlas, before
 * its own work, so the key write does not race with the game. Nothing here reads variables on
 * those threads or writes game state; the only write is the slot key of the local texture atlas.
 * Logging is limited to the first lines of each kind (counts, the stem is a file name).
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/scoped_frame.hpp"
#include "../../core/memory.hpp"
#include "../../engine/msvc_string.hpp"
#include "flag_layer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace capie::country::flag {

/// @brief `u64 UpdateFlagAtlas(atlas*, gfx*)`.
using AtlasUpdateFn = std::uint64_t (*)(uptr, uptr);
/// @brief `int LoadCountryFlag(atlas*, country*, dst*, int, int, int, int, void*)`.
using LoadFlagFn = int (*)(uptr, uptr, void*, int, int, int, int, void*);
/// @brief `vector* BuildFlagCandidates(vector* out, const string* tag, const string* cosmetic, group*)`.
using CandidatesFn = void* (*)(void*, const void*, const void*, void*);
/// @brief Log sink.
using LogFn = void (*)(core::logging::Channel, const char*);

/// @brief The incremental loader's success statuses (`LoadCountryFlag` loop).
inline constexpr int status_loaded = 0;
/// @brief Second success status.
inline constexpr int status_loaded_alt = 5;
/// @brief Impossible cosmetic id written into a slot key to force a reload.
inline constexpr std::uint64_t forced_key = ~std::uint64_t{0};
/// @brief Atlases remembered (the game has three).
inline constexpr std::size_t max_atlases = 4;
/// @brief Most countries visited (sanity bound, as in the name layer).
inline constexpr std::int32_t max_countries = 4096;
/// @brief Debug lines of each kind.
inline constexpr int max_debug_lines = 8;
/// @brief Size of a log line buffer.
inline constexpr std::size_t log_line_bytes = 200;

/// @brief Cache entry of one country.
struct Entry {
    std::int32_t index{};     ///< Index in the game-state country list (= atlas slot).
    bool shown{};             ///< The layer is valid and its snapshot matches.
    std::string stem;         ///< File stem (when shown).
    std::uint64_t changed{};  ///< Generation of the last change.
};

/// @brief Counters of one refresh pass.
struct Stats {
    std::int32_t countries{};  ///< Countries visited.
    std::int32_t shown{};      ///< Countries whose layer is shown.
    std::int32_t changed{};    ///< Entries that changed.
    bool list_ok{};            ///< The country list was readable.
};

/**
 * @brief The flag layer of one process.
 * @tparam M Memory policy with `read` and `write` (`FlagMemory` in the game, a fake in tests).
 */
template <class M>
class FlagLayer {
public:
    /// @brief Trampolines; MinHook writes them before any hook is enabled.
    struct Originals {
        AtlasUpdateFn atlas_update{};  ///< UpdateFlagAtlas.
        LoadFlagFn load_flag{};        ///< LoadCountryFlag.
        CandidatesFn candidates{};     ///< BuildFlagCandidates.
    };

    /**
     * @brief Creates an inactive layer.
     * @param mem Memory policy (must outlive the object).
     */
    explicit FlagLayer(const M& mem) noexcept : mem_(mem) {}

    /**
     * @brief The trampoline slots for `HookSpec`.
     * @return Mutable reference.
     */
    Originals& originals() noexcept { return originals_; }

    /**
     * @brief Activates the layer after its pins passed and the hooks were installed.
     * @param image Image base.
     * @param log Log sink (may be null).
     */
    void activate(uptr image, LogFn log) noexcept {
        image_ = image;
        log_ = log;
        enabled_.store(originals_.atlas_update != nullptr && originals_.load_flag != nullptr &&
                       originals_.candidates != nullptr);
    }

    /// @brief Turns the layer off (hooks pass through).
    void deactivate() noexcept { enabled_.store(false); }

    /// @brief Whether the layer is active. @return State.
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

    /**
     * @brief Re-reads one country's layer into the cache (logic thread; UpdateNames hook).
     * @param country Country object.
     * @return `true` when the layer is shown.
     */
    bool update(uptr country) noexcept {
        if (!enabled_.load()) {
            return false;
        }
        std::int32_t index = 0;
        if (!index_of(country, index)) {
            return false;
        }
        return store(country, index);
    }

    /**
     * @brief Re-reads every country (logic thread; after a `capie_cf_rev` commit).
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
        const auto before = generation_.load();
        for (std::int32_t index = 0; index < count; ++index) {
            uptr country = 0;
            if (!core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), country) || country == 0) {
                continue;
            }
            ++stats.countries;
            stats.shown += store(country, index) ? 1 : 0;
        }
        stats.changed = static_cast<std::int32_t>(generation_.load() - before);
        note(core::logging::Channel::lifecycle, "CAPIE_FLAG commit list_ok=1 countries=%d shown=%d changed=%d",
             stats.countries, stats.shown, stats.changed);
        return stats;
    }

    /**
     * @brief Hook body of `UpdateFlagAtlas`.
     * @param atlas Atlas object.
     * @param gfx Graphics object.
     * @return The original's result.
     * @note Not `noexcept`: it calls the original, whose exceptions pass through (scan N6).
     */
    std::uint64_t atlas_update(uptr atlas, uptr gfx) {
        if (enabled_.load()) {
            const auto now = generation_.load(std::memory_order_acquire);
            auto& seen = seen_slot(atlas);
            if (seen.generation != now) {
                poke_changed(atlas, seen.generation);
                seen.generation = now;
            }
        }
        return originals_.atlas_update(atlas, gfx);
    }

    /**
     * @brief Hook body of `LoadCountryFlag` (also on TBB worker threads).
     * @return The original's status.
     * @note Not `noexcept`: it calls the original, whose exceptions pass through (scan N6); the
     *       frame is restored by a destructor.
     */
    int load_flag(uptr atlas, uptr country, void* dst, int a4, int a5, int a6, int a7, void* a8) {
        Frame frame{};
        if (enabled_.load() && shown_stem(country, frame.stem)) {
            frame.country = country;
            int status = 0;
            {
                const core::ScopedFrame<Frame> scope(active_frame, &frame);
                status = originals_.load_flag(atlas, country, dst, a4, a5, a6, a7, a8);
            }
            if (status == status_loaded || status == status_loaded_alt) {
                if (applied_lines_.fetch_add(1) < max_debug_lines) {
                    note(core::logging::Channel::debug, "CAPIE_FLAG slot loaded from the layer stem=%s status=%d",
                         frame.stem.c_str(), status);
                }
                return status;
            }
            if (fallback_lines_.fetch_add(1) < max_debug_lines) {
                note(core::logging::Channel::lifecycle, "CAPIE_FLAG file not loaded stem=%s status=%d; game flag",
                     frame.stem.c_str(), status);
            }
        }
        return originals_.load_flag(atlas, country, dst, a4, a5, a6, a7, a8);
    }

    /**
     * @brief Hook body of `BuildFlagCandidates`.
     * @param out Candidate vector.
     * @param tag Tag string.
     * @param cosmetic Cosmetic tag string.
     * @param group Ideology group (may be null).
     * @return The original's result.
     * @note Not `noexcept`: it calls the original, whose exceptions pass through (scan N6).
     */
    void* candidates(void* out, const void* tag, const void* cosmetic, void* group) {
        const Frame* const frame = active_frame;
        if (frame != nullptr && frame->country != 0 &&
            reinterpret_cast<uptr>(cosmetic) == frame->country + core::off::country_cosmetic_tag) {
            const auto stem = engine::borrowed_string(frame->stem.data(), frame->stem.size());
            const auto empty = engine::borrowed_string("", 0);
            return originals_.candidates(out, &stem, &empty, nullptr);
        }
        return originals_.candidates(out, tag, cosmetic, group);
    }

    /**
     * @brief Whether this thread is inside a layered LoadCountryFlag call (tests and diagnostics).
     * @return `true` while a load frame is set.
     */
    [[nodiscard]] static bool in_load() noexcept { return active_frame != nullptr; }

    /**
     * @brief Whether a country shows a layer, and its stem (tests and diagnostics).
     * @param country Country.
     * @return Stem, empty when none.
     */
    std::string shown(uptr country) const {
        std::string stem;
        return shown_stem(country, stem) ? stem : std::string{};
    }

private:
    /// @brief The load running on this thread.
    struct Frame {
        uptr country{};    ///< Country whose slot is loaded.
        std::string stem;  ///< Its stem.
    };
    /// @brief What an atlas has seen.
    struct Seen {
        uptr atlas{};                ///< Atlas object (0 = free).
        std::uint64_t generation{};  ///< Cache generation it last handled.
    };
    /// @brief Country -> entry.
    using Map = std::unordered_map<uptr, Entry>;

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
     * @param country Country.
     * @param index Receives the index.
     * @return `false` when the country is not in the list.
     */
    bool index_of(uptr country, std::int32_t& index) const noexcept {
        uptr list = 0;
        std::int32_t count = 0;
        if (!country_list(list, count)) {
            return false;
        }
        std::int32_t tag = 0;
        uptr at = 0;
        if (core::rd(mem_, country, core::off::country_tag, tag) && tag > 0 && tag < count &&
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
     * @brief Reads one country's layer and gate and stores the entry (logic thread).
     * @param country Country.
     * @param index Its list index.
     * @return `true` when shown.
     */
    bool store(uptr country, std::int32_t index) noexcept {
        const auto layer = read_flag_layer(mem_, country);
        Text cosmetic{};
        bool too_long = false;
        const bool cosmetic_ok = layer.status != Status::valid || read_cosmetic(mem_, country, cosmetic, too_long);
        const bool show = cosmetic_ok && flag_shown(layer, cosmetic.view(), too_long);
        try {
            const std::unique_lock lock(mutex_);
            if (!map_) {
                if (!show) {
                    return false;
                }
                map_ = std::make_unique<Map>();
            }
            auto found = map_->find(country);
            if (found == map_->end() && !show) {
                return false;
            }
            auto& entry = (*map_)[country];
            const bool same = entry.changed != 0 && entry.index == index && entry.shown == show &&
                              (!show || entry.stem == layer.stem.view());
            if (!same) {
                entry.index = index;
                entry.shown = show;
                entry.stem.assign(show ? layer.stem.view() : std::string_view{});
                entry.changed = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (refresh_lines_.fetch_add(1) < max_debug_lines) {
                    note(core::logging::Channel::debug,
                         "CAPIE_FLAG entry idx=%d shown=%u stem=%s status=%s snapshot=%u", index, show ? 1U : 0U,
                         entry.stem.c_str(), layer::status_name(layer.status), layer.has_snapshot ? 1U : 0U);
                }
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure leaves the game's flag
            return false;
        }
        return show;
    }

    /**
     * @brief The stem a country shows, from the cache (any thread; shared lock only).
     * @param country Country.
     * @param stem Receives the stem.
     * @return `true` when the country shows a layer and still sits at its list index.
     */
    bool shown_stem(uptr country, std::string& stem) const noexcept {
        std::int32_t index = 0;
        try {
            const std::shared_lock lock(mutex_);
            if (!map_) {
                return false;
            }
            const auto found = map_->find(country);
            if (found == map_->end() || !found->second.shown || found->second.stem.empty()) {
                return false;
            }
            stem = found->second.stem;
            index = found->second.index;
        } catch (...) {
            return false;
        }
        uptr list = 0;
        std::int32_t count = 0;
        uptr at = 0;
        return country_list(list, count) && index >= 0 && index < count &&
               core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), at) && at == country;
    }

    /**
     * @brief The remembered generation of an atlas (idler thread).
     * @param atlas Atlas.
     * @return Its slot (the last slot is shared when more than `max_atlases` appear).
     */
    Seen& seen_slot(uptr atlas) noexcept {
        for (auto& seen : seen_) {
            if (seen.atlas == atlas) {
                return seen;
            }
        }
        for (auto& seen : seen_) {
            if (seen.atlas == 0) {
                seen.atlas = atlas;
                return seen;
            }
        }
        return seen_.back();
    }

    /**
     * @brief Writes the forced key into the slots of countries changed after `since`.
     * @param atlas Atlas.
     * @param since Generation the atlas has handled.
     */
    void poke_changed(uptr atlas, std::uint64_t since) noexcept {
        std::vector<std::pair<uptr, std::int32_t>> changed;
        try {
            const std::shared_lock lock(mutex_);
            if (!map_) {
                return;
            }
            for (const auto& [country, entry] : *map_) {
                if (entry.changed > since) {
                    changed.emplace_back(country, entry.index);
                }
            }
        } catch (...) {
            return;
        }
        uptr keys = 0;
        std::int32_t slots = 0;
        uptr list = 0;
        std::int32_t count = 0;
        if (!core::rd(mem_, atlas, core::off::flag_atlas_cosmetic_keys, keys) || keys == 0 ||
            !core::rd(mem_, atlas, core::off::flag_atlas_cosmetic_count, slots) || !country_list(list, count)) {
            return;
        }
        for (const auto& [country, index] : changed) {
            uptr at = 0;
            if (index <= 0 || index >= slots || index >= count ||
                !core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), at) || at != country) {
                continue;
            }
            const auto key = forced_key;
            const bool written =
                mem_.write(keys + (static_cast<uptr>(index) * sizeof(std::uint64_t)), &key, sizeof(key));
            if (poke_lines_.fetch_add(1) < max_debug_lines) {
                note(core::logging::Channel::debug, "CAPIE_FLAG reload idx=%d written=%u", index, written ? 1U : 0U);
            }
        }
    }

    /**
     * @brief Formats and writes one log line.
     * @param channel Channel.
     * @param format printf format.
     * @param args Arguments (numbers, status names and file stems; never a player's name).
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

    const M& mem_;                              ///< Memory policy.
    Originals originals_{};                     ///< Trampolines.
    uptr image_{};                              ///< Image base.
    LogFn log_{};                               ///< Log sink.
    std::atomic<bool> enabled_{false};          ///< Layer active.
    std::atomic<std::uint64_t> generation_{0};  ///< Bumped on every entry change.
    std::array<Seen, max_atlases> seen_{};      ///< Per atlas (idler thread only).
    mutable std::shared_mutex mutex_;           ///< Guards `map_`; never held across a game call.
    std::unique_ptr<Map> map_;                  ///< Country -> entry; created on first layer.
    std::atomic<int> applied_lines_{0};         ///< Debug line budget.
    std::atomic<int> fallback_lines_{0};        ///< Lifecycle line budget.
    std::atomic<int> refresh_lines_{0};         ///< Debug line budget.
    std::atomic<int> poke_lines_{0};            ///< Debug line budget.
    /// @brief Frame of the load running on this thread.
    static inline thread_local Frame* active_frame = nullptr;
};

/**
 * @brief Writes bytes into a committed, writable page under an SEH guard.
 * @param target Address.
 * @param source Bytes.
 * @param size Byte count.
 * @return `true` when written.
 */
inline bool guarded_write(uptr target, const void* source, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (target == 0 || VirtualQuery(reinterpret_cast<void*>(target), &region, sizeof(region)) == 0 ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = region.Protect & core::page_access_mask;
    if (access != PAGE_READWRITE && access != PAGE_EXECUTE_READWRITE) {
        return false;
    }
    const auto end = reinterpret_cast<uptr>(region.BaseAddress) + region.RegionSize;
    if (size > end - target) {
        return false;
    }
    __try {
        std::memcpy(reinterpret_cast<void*>(target), source, size);
        return true;
    } __except (core::memory_exception(GetExceptionCode())) {
        return false;
    }
}

/// @brief Memory policy of the flag layer in the game: guarded reads and the one guarded write.
struct FlagMemory : core::ProcessMemory {
    /**
     * @brief Writes bytes (the atlas slot key only).
     * @param address Target.
     * @param data Bytes.
     * @param size Byte count.
     * @return `true` when written.
     */
    static bool write(uptr address, const void* data, std::size_t size) noexcept {
        return guarded_write(address, data, size);
    }
};

}  // namespace capie::country::flag
