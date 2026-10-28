/**
 * @file portrait_layer_hooks.hpp
 * @brief Hook body and cache of the leader portrait layer (API level 7, CONTRACT.md section 18).
 *
 * One hook: `ResolvePortrait` (`rva::portrait_resolve`), the function the GUI portrait icons call
 * (it has one caller) to copy a character's portrait entry into the caller's buffer, with
 * the game's generic portrait when the character has none. For the civilian portrait (type 0)
 * of a cached ruling leader (same object, same id), the hook builds the entry of the chosen
 * sprite, checks it with the game's own validity function (`PortraitEntryValid`: the sprite exists) and, only
 * when it is valid, fills the caller's buffer itself the way the game's copy does (flag byte,
 * kind 0, copy-constructed name, no texture handle) without calling the original. Otherwise the
 * original runs unchanged (fail-closed: the game's portrait, never the "unknown leader" image).
 * Game data are never written.
 *
 * Cache: per country, its ruling leader with the two names, written on the logic thread: by
 * `update(country)` from the UpdateNames hook (load, government and name changes) and by
 * `refresh_all()` after `set_variable` of `capie_pt_rev` (any country scope; also the mod's daily
 * refresh). `refresh_all` rebuilds the whole cache, so a character that stopped ruling, or a
 * pointer from a previous game, is dropped. The hook only takes a shared lock and copies the
 * names; it reads nothing but the character's vtable and id, which the game passes to it.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../../engine/game_calls.hpp"
#include "../../engine/msvc_string.hpp"
#include "portrait_layer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>

namespace capie::country::portrait {

/// @brief Size of the game's portrait entry.
inline constexpr std::size_t entry_size = 0x30;

/// @brief The game's portrait entry (0x30 bytes; `ResolvePortrait` copies it field by field).
struct Entry {
    std::uint8_t set;                     ///< Non-zero when the entry holds a portrait (`valid.flag_byte`).
    std::array<std::uint8_t, 3> padding;  ///< Unused.
    std::int32_t kind;                    ///< 0 = sprite name, 1 = .dds path (`valid.kind`).
    engine::MsvcString name;              ///< Sprite name or path (`valid.name`).
    std::uint64_t handle;                 ///< Texture handle of a path entry.
};
static_assert(sizeof(Entry) == entry_size && offsetof(Entry, kind) == core::off::portrait_entry_kind &&
              offsetof(Entry, name) == core::off::portrait_entry_name &&
              offsetof(Entry, handle) == core::off::portrait_entry_handle);

/// @brief `entry* ResolvePortrait(map*, entry* out, int type, int size, character*, const char* fallback)`.
using ResolveFn = void* (*)(void*, void*, std::int32_t, std::int32_t, uptr, const void*);
/// @brief `bool PortraitEntryValid(const entry*)`.
using ValidFn = engine::PortraitValid;
/// @brief `string* string_copy(string* out, const string* source)` (copy-construct).
using CopyFn = engine::StringCopy;
/// @brief Log sink.
using LogFn = void (*)(core::logging::Channel, const char*);

/// @brief Most countries visited (sanity bound, as in the other layers).
inline constexpr std::int32_t max_countries = 4096;
/// @brief Debug lines of each kind.
inline constexpr int max_debug_lines = 8;
/// @brief Size of a log line buffer.
inline constexpr std::size_t log_line_bytes = 200;

/// @brief One cached ruling leader with a layer.
struct Shown {
    uptr country{};     ///< Country it rules.
    std::int32_t id{};  ///< Character id at the time it was read.
    Text large;         ///< Large sprite name.
    Text small;         ///< Small sprite name (empty = game's small portrait).
};

/// @brief Counters of one refresh pass.
struct Stats {
    std::int32_t countries{};  ///< Countries visited.
    std::int32_t leaders{};    ///< Ruling leaders found.
    std::int32_t layers{};     ///< Leaders with a valid layer.
    std::int32_t invalid{};    ///< Leaders with an invalid layer.
    bool list_ok{};            ///< The country list was readable.
};

/**
 * @brief The portrait layer of one process.
 * @tparam M Memory policy with `read` (`core::ProcessMemory` in the game, a fake in tests).
 */
template <class M>
class PortraitLayer {
public:
    /// @brief Trampoline and game functions; MinHook writes `resolve` before the hook is enabled.
    struct Originals {
        ResolveFn resolve{};  ///< ResolvePortrait trampoline.
        ValidFn valid{};      ///< The game's entry validity check (called, never hooked).
        CopyFn copy{};        ///< The game's string copy-construct (called).
    };

    /**
     * @brief Creates an inactive layer.
     * @param mem Memory policy (must outlive the object).
     */
    explicit PortraitLayer(const M& mem) noexcept : mem_(mem) {}

    /**
     * @brief The trampoline and function slots.
     * @return Mutable reference.
     */
    Originals& originals() noexcept { return originals_; }

    /**
     * @brief Activates the layer after its pins passed and the hook was installed.
     * @param image Image base.
     * @param log Log sink (may be null).
     */
    void activate(uptr image, LogFn log) noexcept {
        image_ = image;
        log_ = log;
        enabled_.store(originals_.resolve != nullptr && originals_.valid != nullptr && originals_.copy != nullptr);
    }

    /// @brief Turns the layer off (the hook passes through).
    void deactivate() noexcept { enabled_.store(false); }

    /// @brief Whether the layer is active. @return State.
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

    /**
     * @brief Re-reads one country's ruling leader into the cache (logic thread; UpdateNames hook).
     * @param country Country object.
     * @return `true` when its leader shows a layer.
     */
    bool update(uptr country) noexcept {
        if (!enabled_.load() || country == 0) {
            return false;
        }
        Shown shown{};
        uptr leader = 0;
        const bool show = read_leader(country, leader, shown, nullptr);
        try {
            const std::unique_lock lock(mutex_);
            if (!by_character_) {
                if (!show) {
                    return false;
                }
                by_character_ = std::make_unique<Map>();
            }
            std::erase_if(*by_character_, [country](const auto& item) { return item.second.country == country; });
            if (show) {
                (*by_character_)[leader] = shown;
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure leaves the game's portrait
            return false;
        }
        return show;
    }

    /**
     * @brief Rebuilds the cache from every country's ruling leader (logic thread; after `capie_pt_rev`).
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
            auto fresh = std::make_unique<Map>();
            for (std::int32_t index = 0; index < count; ++index) {
                uptr country = 0;
                if (!core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), country) || country == 0) {
                    continue;
                }
                ++stats.countries;
                Shown shown{};
                uptr leader = 0;
                if (read_leader(country, leader, shown, &stats)) {
                    (*fresh)[leader] = shown;
                }
            }
            const std::unique_lock lock(mutex_);
            by_character_.swap(fresh);
        } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure keeps the previous cache
            return stats;
        }
        note(core::logging::Channel::lifecycle,
             "CAPIE_PORTRAIT commit list_ok=1 countries=%d leaders=%d layers=%d invalid=%d", stats.countries,
             stats.leaders, stats.layers, stats.invalid);
        return stats;
    }

    /**
     * @brief Hook body of `ResolvePortrait`.
     * @param map Portrait map of the character (character+0x78).
     * @param out Caller's entry buffer (uninitialised; the function constructs it).
     * @param type Portrait type (0 = civilian).
     * @param size Portrait size (1 = large, 0 = small).
     * @param character Character object.
     * @param fallback Name the caller shows when nothing is valid.
     * @return `out`.
     */
    void* resolve(void* map, void* out, std::int32_t type, std::int32_t size, uptr character,
                  const void* fallback) noexcept {
        Text name{};
        if (enabled_.load() && type == type_civilian && out != nullptr && chosen(character, size, name)) {
            Entry candidate{};
            candidate.set = 1;
            candidate.kind = 0;
            candidate.name = engine::borrowed_string(name.bytes.data(), name.size);
            // Both game calls are guarded (`game_calls.hpp`); a fault latches, turns the layer off and
            // lets the original build the entry from scratch.
            bool valid = false;
            if (!engine::guarded_portrait_valid(originals_.valid, &candidate, valid)) {
                return game_call_failed(map, out, type, size, character, fallback);
            }
            if (valid) {
                auto* entry = static_cast<Entry*>(out);
                entry->set = candidate.set;
                entry->padding = {};
                entry->kind = 0;
                if (!engine::guarded_string_copy(originals_.copy, &entry->name, &candidate.name)) {
                    return game_call_failed(map, out, type, size, character, fallback);
                }
                entry->handle = 0;
                if (applied_lines_.fetch_add(1) < max_debug_lines) {
                    note(core::logging::Channel::debug, "CAPIE_PORTRAIT shown size=%d sprite=%s", size,
                         name.bytes.data());
                }
                return out;
            }
            if (fallback_lines_.fetch_add(1) < max_debug_lines) {
                note(core::logging::Channel::lifecycle,
                     "CAPIE_PORTRAIT sprite not found size=%d sprite=%s; game portrait", size, name.bytes.data());
            }
        }
        return originals_.resolve(map, out, type, size, character, fallback);
    }

    /**
     * @brief The sprite a character shows for a size (tests and diagnostics).
     * @param character Character.
     * @param size 1 = large, 0 = small.
     * @return Name, empty when none.
     */
    [[nodiscard]] std::string_view shown(uptr character, std::int32_t size) const {
        thread_local Text name{};
        name = Text{};
        return chosen(character, size, name) ? name.view() : std::string_view{};
    }

private:
    /**
     * @brief A guarded game call failed or was refused: layer off, the original runs unchanged.
     * @param map Passed through.
     * @param out Passed through (the original constructs it).
     * @param type Passed through.
     * @param size Passed through.
     * @param character Passed through.
     * @param fallback Passed through.
     * @return The original's result.
     */
    void* game_call_failed(void* map, void* out, std::int32_t type, std::int32_t size, uptr character,
                           const void* fallback) noexcept {
        if (enabled_.exchange(false)) {
            note(core::logging::Channel::lifecycle, "CAPIE_PORTRAIT game call failed or refused (fault latch); layer off");
        }
        return originals_.resolve(map, out, type, size, character, fallback);
    }

    /// @brief Ruling leader -> chosen sprites.
    using Map = std::unordered_map<uptr, Shown>;

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
     * @brief Reads a country's ruling leader and its layer.
     * @param country Country.
     * @param leader Receives the leader (0 when none).
     * @param shown Receives the cache entry.
     * @param stats Counters to update (may be null).
     * @return `true` when the leader has a valid layer.
     */
    bool read_leader(uptr country, uptr& leader, Shown& shown, Stats* stats) const noexcept {
        if (!ruling_leader(mem_, image_, country, leader) || leader == 0) {
            return false;
        }
        std::int32_t id = 0;
        if (!core::rd(mem_, leader, core::off::character_id, id)) {
            return false;
        }
        const auto layer = read_portrait_layer(mem_, leader);
        if (stats != nullptr) {
            ++stats->leaders;
            stats->layers += layer.status == Status::valid ? 1 : 0;
            stats->invalid += layer.status == Status::invalid ? 1 : 0;
        }
        if (layer.status != Status::valid) {
            return false;
        }
        shown.country = country;
        shown.id = id;
        shown.large = layer.large;
        shown.small = layer.small;
        if (refresh_lines_.fetch_add(1) < max_debug_lines) {
            note(core::logging::Channel::debug, "CAPIE_PORTRAIT entry id=%d large=%s small=%s", id,
                 layer.large.bytes.data(), layer.small.bytes.data());
        }
        return true;
    }

    /**
     * @brief The chosen sprite of a cached character (any thread; shared lock only).
     * @param character Character the game passed.
     * @param size 1 = large, 0 = small.
     * @param name Receives the name.
     * @return `true` when the character is cached (same object, same id) with a name for that size.
     */
    bool chosen(uptr character, std::int32_t size, Text& name) const noexcept {
        std::int32_t id = 0;
        try {
            const std::shared_lock lock(mutex_);
            if (!by_character_) {
                return false;
            }
            const auto found = by_character_->find(character);
            if (found == by_character_->end()) {
                return false;
            }
            name = size == size_large ? found->second.large : found->second.small;
            id = found->second.id;
        } catch (...) {
            return false;
        }
        std::int32_t current = 0;
        return name.size != 0 && is_character(mem_, image_, character) &&
               core::rd(mem_, character, core::off::character_id, current) && current == id;
    }

    /**
     * @brief Formats and writes one log line.
     * @param channel Channel.
     * @param format printf format.
     * @param args Arguments (numbers and sprite names; never a player's text).
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

    const M& mem_;                                ///< Memory policy.
    Originals originals_{};                       ///< Trampoline and game functions.
    uptr image_{};                                ///< Image base.
    LogFn log_{};                                 ///< Log sink.
    std::atomic<bool> enabled_{false};            ///< Layer active.
    mutable std::shared_mutex mutex_;             ///< Guards `by_character_`; never held across a game call.
    std::unique_ptr<Map> by_character_;           ///< Ruling leader -> chosen sprites (created on first layer).
    mutable std::atomic<int> applied_lines_{0};   ///< Debug line budget.
    mutable std::atomic<int> fallback_lines_{0};  ///< Lifecycle line budget.
    mutable std::atomic<int> refresh_lines_{0};   ///< Debug line budget.
};

}  // namespace capie::country::portrait
