/**
 * @file colour_layer_hooks.hpp
 * @brief Hook body, slots and repaint of the country colour layer (API level 8, CONTRACT.md section 19).
 *
 * One hook: `map_colors(country)` = `GetMapColors` (`rva::country_map_colors`), the getter all
 * users of a country's colours call (map modes, map renderer, faction colour, UI). It runs the
 * original (which walks to the overlord when the subject is drawn in its colour), then, for the
 * RESOLVED country (`result - off::country_colors`), returns CAPIE's own colour block when that
 * country's slot is live. Fast path: one relaxed and one acquire load when no country has a
 * layer. The detour takes no lock, makes no game call and does not log; its reads of the
 * resolved country (vtable, tag) are SEH-only copies (`hot_read`) of an object the original has
 * just dereferenced.
 *
 * Slots: one per tag id (`country + off::country_tag`, 1..4095), never freed during the process;
 * each has two blocks (double buffer). A writer fills the inactive block, then publishes it with
 * a release store of `active`; readers on any thread load `active` with acquire. A reader that
 * holds a block across two rapid changes can see a torn colour for one frame (documented risk).
 *
 * Writers: only on the game's logic thread, in the UpdateNames hook (`update`) and after a
 * `capie_cc_rev` commit (`refresh_all`); a mutex serialises them and is never held across the
 * game call. For every country whose shown colour changed, the writer then repaints: when the
 * game is in a campaign (the current idler's in-game slot is the `CInGameIdler` function,
 * compared by address, never called) and the map exists, it calls the game's own
 * `RefreshCountryColor` through `engine::guarded_refresh_colour` (which dirties the political
 * mode), and marks dirty the map modes whose builders read the colour (`colour_map_modes`: the
 * political and claims modes, one guarded byte write each). Never all 23 modes: the continent
 * mode's builder logs "Getting continent for <province>" to error.log for each province, so a
 * rebuild of every mode wrote about 98 lines per Apply (in-game test T6). A refused or faulted
 * game call turns the layer off; the getter then passes everything through (vanilla colours).
 *
 * Determinism: the slot state is a function of the country variables only, updated at the
 * effect step that commits them, on every multiplayer client; nothing is deferred to a frame.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../../engine/game_calls.hpp"
#include "colour_layer.hpp"
#include "flag_layer_hooks.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>

namespace capie::country::colour {

/// @brief `const CCountryColors* GetMapColors(const CCountry*)`.
using MapColorsFn = const void* (*)(const void*);
/// @brief `void RefreshCountryColor(map*, const tag*)`.
using RefreshFn = engine::RefreshColour;
/// @brief Log sink.
using LogFn = void (*)(core::logging::Channel, const char*);

/// @brief Slots (tag ids 0..4095; 0 is never used); shared bound with the flag layer.
inline constexpr std::int32_t max_countries = 4096;
/**
 * @brief The map modes marked dirty after a refresh: the modes whose builders read the country colour.
 *
 * The game's 23-way rebuild switch sends mode 0 to the political builder and mode 19 to the claims
 * builder; both reach `GetMapColors` (the political builder directly, the claims builder through its
 * claimant colour reader), and the political builder also reads the faction colour. No other
 * builder reaches the getter or the faction colour. Mode 21's builder logs the continent of each
 * province and is never marked. Pinned: `rebuild.*`, `political.*`, `claims*` and `continent.*` in
 * `colour_layer_pins.hpp`; evidence in `docs/research/country-colour-2026-10-28.md` (unknown 1).
 */
inline constexpr std::array<std::uint8_t, 2> colour_map_modes = {core::value::political_map_mode,
                                                                core::value::claims_map_mode};
static_assert([] {
    for (const std::uint8_t mode : colour_map_modes) {
        if (mode >= core::value::map_mode_count || mode == core::value::continent_map_mode) {
            return false;
        }
    }
    return true;
}(), "colour_map_modes: valid modes only, never the continent-logging mode");
/// @brief Debug lines of each kind.
inline constexpr int max_debug_lines = 8;
/// @brief Size of a log line buffer.
inline constexpr std::size_t log_line_bytes = 200;
/// @brief Values of `Slot::active`: no block, `blocks[0]`, `blocks[1]`.
inline constexpr std::uint8_t no_block = 0;
/// @brief First block published.
inline constexpr std::uint8_t first_block = 1;
/// @brief Second block published.
inline constexpr std::uint8_t second_block = 2;

/// @brief One country's published colour (process lifetime).
struct Slot {
    std::atomic<uptr> country{0};             ///< `CCountry*` this slot belongs to (0 = unused).
    std::atomic<std::uint8_t> active{no_block};  ///< 0 none, 1 = `blocks[0]`, 2 = `blocks[1]`.
    std::array<ColourBlock, 2> blocks{};      ///< Double buffer.
    std::uint32_t rgb{};                      ///< Last published colour (writer only).
    bool shown{};                             ///< A block is published (writer only).
};

/**
 * @brief Writes a block into the inactive half of a double buffer, then publishes it (writer only).
 * @tparam B Block type (`ColourBlock`; a probe type in the tests).
 * @param active Published half: `no_block`, `first_block` or `second_block`.
 * @param blocks The two halves.
 * @param block New content.
 * @return The half now published.
 */
template <class B>
std::uint8_t publish_block(std::atomic<std::uint8_t>& active, std::array<B, 2>& blocks, const B& block) noexcept {
    const auto next = active.load(std::memory_order_relaxed) == first_block ? second_block : first_block;
    blocks[next - 1U] = block;                     // 1. write the inactive half
    active.store(next, std::memory_order_release);  // 2. then publish it
    return next;
}

/// @brief Counters of one pass.
struct Stats {
    std::int32_t countries{};  ///< Countries visited.
    std::int32_t shown{};      ///< Countries whose layer is shown.
    std::int32_t changed{};    ///< Slots that changed.
    std::int32_t refreshed{};  ///< RefreshCountryColor calls made.
    bool list_ok{};            ///< The country list was readable.
};

/**
 * @brief The colour layer of one process.
 * @tparam M Memory policy with `read`, `hot_read` and `write` (`ColourMemory` in the game, a fake in tests).
 */
template <class M>
class ColourLayer {
public:
    /// @brief Trampoline and game function; MinHook writes `map_colors` before the hook is enabled.
    struct Originals {
        MapColorsFn map_colors{};  ///< GetMapColors trampoline.
        RefreshFn refresh{};       ///< RefreshCountryColor (called, never hooked).
    };

    /**
     * @brief Creates an inactive layer.
     * @param mem Memory policy (must outlive the object).
     */
    explicit ColourLayer(const M& mem) noexcept : mem_(mem) {}

    /**
     * @brief The trampoline and function slots.
     * @return Mutable reference.
     */
    Originals& originals() noexcept { return originals_; }

    /**
     * @brief Activates the layer after its pins and RTTI checks passed and the hook was installed.
     *
     * Allocates the slots and reads the six colour defines once; any failure leaves the layer
     * off (the getter then passes through).
     * @param image Image base.
     * @param log Log sink (may be null).
     */
    void activate(uptr image, LogFn log) noexcept {
        image_ = image;
        log_ = log;
        country_vtable_ = image + core::rva::vt_country;
        if (!slots_) {
            // One guard slot past the last id, never used: a bounds slip shows in tests, not as a heap overrun.
            slots_.reset(new (std::nothrow) Slot[static_cast<std::size_t>(max_countries) + 1U]);
        }
        Defines map_defines{};
        Defines ui_defines{};
        enabled_.store(originals_.map_colors != nullptr && originals_.refresh != nullptr && slots_ != nullptr &&
                       read_defines(mem_, image_, map_defines, ui_defines));
    }

    /// @brief Turns the layer off (the getter passes through).
    void deactivate() noexcept { enabled_.store(false); }

    /// @brief Whether the layer is active. @return State.
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

    /// @brief Number of published slots (tests and diagnostics). @return Count.
    [[nodiscard]] std::uint32_t live() const noexcept { return live_.load(); }

    /**
     * @brief Hook body of `GetMapColors` (any thread, hot).
     * @param country Country the caller asks for.
     * @return The game's block, or CAPIE's block of the resolved country.
     * @note Not `noexcept`: it calls the original, whose exceptions pass through (scan N6).
     */
    const void* map_colors(const void* country) {
        const void* game = originals_.map_colors(country);
        if (!enabled_.load(std::memory_order_relaxed) || live_.load(std::memory_order_acquire) == 0) {
            return game;  // fast path: no layer in this game
        }
        return override_for(game);
    }

    /**
     * @brief Re-evaluates one country (logic thread; UpdateNames hook, after the original).
     * @param country Country object.
     * @return `true` when its shown colour changed.
     */
    bool update(uptr country) noexcept {
        if (!enabled_.load()) {
            return false;
        }
        Stats stats{};
        uptr list = 0;
        std::int32_t count = 0;
        stats.list_ok = country_list(list, count);
        std::int32_t id = 0;
        const bool changed = store(country, id, stats);
        if (changed && stats.list_ok) {
            static_cast<void>(repaint(country, id, count, stats));
        }
        return changed;
    }

    /**
     * @brief Re-evaluates every country (logic thread; after a `capie_cc_rev` commit).
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
        for (std::int32_t index = 0; index < count; ++index) {
            uptr country = 0;
            if (!core::rd(mem_, list, static_cast<uptr>(index) * sizeof(uptr), country) || country == 0) {
                continue;
            }
            ++stats.countries;
            std::int32_t id = 0;
            if (store(country, id, stats) && !repaint(country, id, count, stats)) {
                break;  // refused or faulted: no further game call
            }
        }
        note(core::logging::Channel::lifecycle,
             "CAPIE_COLOUR commit list_ok=1 countries=%d shown=%d changed=%d refreshed=%d", stats.countries,
             stats.shown, stats.changed, stats.refreshed);
        return stats;
    }

    /**
     * @brief The block a country shows (tests and diagnostics).
     * @param id Tag id.
     * @return Block, or null when none is published.
     */
    [[nodiscard]] const ColourBlock* published(std::int32_t id) const noexcept {
        const Slot* const slot = slot_of(id);
        if (slot == nullptr) {
            return nullptr;
        }
        const auto which = slot->active.load(std::memory_order_acquire);
        return which == first_block || which == second_block ? &slot->blocks[which - 1U] : nullptr;
    }

private:
    /**
     * @brief The slot of a tag id.
     * @param id Tag id.
     * @return The slot, or null for an id outside 1..4095 or before activation.
     */
    Slot* slot_of(std::int32_t id) const noexcept {
        return slots_ && id > 0 && id < max_countries ? &slots_[static_cast<std::size_t>(id)] : nullptr;
    }

    /**
     * @brief The override for a block the original returned (the detour's slow path).
     * @param game Block the original returned (`resolved + off::country_colors`).
     * @return CAPIE's block of the resolved country, or `game`.
     */
    const void* override_for(const void* game) const noexcept {
        const auto at = reinterpret_cast<uptr>(game);
        if (at < core::off::country_colors) {
            return game;
        }
        const uptr resolved = at - core::off::country_colors;
        uptr vtable = 0;
        if (!mem_.hot_read(resolved, &vtable, sizeof(vtable)) || vtable != country_vtable_) {
            return game;  // RTTI-pinned vtable: anything else is not a country
        }
        std::int32_t id = 0;
        if (!mem_.hot_read(resolved + core::off::country_tag, &id, sizeof(id))) {
            return game;
        }
        const Slot* const slot = slot_of(id);
        if (slot == nullptr || slot->country.load(std::memory_order_acquire) != resolved) {
            return game;
        }
        const auto which = slot->active.load(std::memory_order_acquire);
        return which == first_block || which == second_block ? static_cast<const void*>(&slot->blocks[which - 1U])
                                                              : game;
    }

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
     * @brief Reads one country's layer and publishes or removes its block (logic thread).
     * @param country Country.
     * @param id Receives its tag id.
     * @param stats Counters.
     * @return `true` when the shown colour changed.
     */
    bool store(uptr country, std::int32_t& id, Stats& stats) noexcept {
        if (!core::rd(mem_, country, core::off::country_tag, id) || slot_of(id) == nullptr) {
            return false;
        }
        const auto layer = read_colour_layer(mem_, country);
        flag::Text cosmetic{};
        bool too_long = false;
        const bool cosmetic_ok = !layer.has_snapshot || flag::read_cosmetic(mem_, country, cosmetic, too_long);
        const auto verdict = cosmetic_ok ? colour_verdict(layer, cosmetic.view(), too_long) : Verdict::unreadable;
        bool show = verdict == Verdict::valid;
        ColourBlock block{};
        if (show) {
            Defines map_defines{};
            Defines ui_defines{};
            show = read_defines(mem_, image_, map_defines, ui_defines);  // unreadable defines: fail closed
            block = make_block(image_, layer.rgb, map_defines, ui_defines);
        }
        stats.shown += show ? 1 : 0;
        bool changed = false;
        {
            const std::scoped_lock lock(mutex_);
            Slot& slot = *slot_of(id);
            if (slot.country.load(std::memory_order_relaxed) != country) {
                // The id was reused by another object (another save): reset before publishing.
                if (slot.active.exchange(no_block, std::memory_order_acq_rel) != no_block) {
                    live_.fetch_sub(1, std::memory_order_acq_rel);
                }
                slot.shown = false;
                slot.rgb = 0;
                slot.country.store(country, std::memory_order_release);
            }
            if (show) {
                if (!slot.shown || slot.rgb != layer.rgb) {
                    static_cast<void>(publish_block(slot.active, slot.blocks, block));
                    if (!slot.shown) {
                        live_.fetch_add(1, std::memory_order_acq_rel);  // 3. adjust the fast path
                    }
                    slot.shown = true;
                    slot.rgb = layer.rgb;
                    changed = true;
                }
            } else if (slot.shown) {
                slot.active.store(no_block, std::memory_order_release);
                live_.fetch_sub(1, std::memory_order_acq_rel);
                slot.shown = false;
                slot.rgb = 0;
                changed = true;
            }
        }
        if (changed) {
            ++stats.changed;
            if (store_lines_.fetch_add(1) < max_debug_lines) {
                note(core::logging::Channel::debug, "CAPIE_COLOUR entry id=%d shown=%u rgb=%06X verdict=%s", id,
                     show ? 1U : 0U, static_cast<unsigned>(show ? layer.rgb : 0U), verdict_name(verdict));
            }
        }
        return changed;
    }

    /**
     * @brief Whether the game is in a campaign with a map (the guard the game uses before its refresh).
     * @param map Receives the map object.
     * @return `true` only for a non-null map and an idler whose in-game slot is the `CInGameIdler` function.
     */
    bool in_game(uptr& map) const noexcept {
        uptr idler = 0;
        uptr vtable = 0;
        uptr slot = 0;
        if (!core::rd(mem_, image_, core::rva::map_singleton, map) || map == 0 ||
            !core::rd(mem_, image_, core::rva::current_idler, idler) || idler == 0 ||
            !core::rd(mem_, idler, 0, vtable) || !core::rd(mem_, vtable, core::off::idler_in_game_slot, slot)) {
            return false;
        }
        // Compared by address, never called: CInGameIdler's slot is `mov al,1; ret`, the base idler's
        // `xor al,al; ret` (both pinned); any other function is treated as "not in game".
        return slot == image_ + core::rva::idler_in_game_true;
    }

    /**
     * @brief Repaints one country whose shown colour changed (logic thread, no lock held).
     * @param country Country.
     * @param id Its tag id.
     * @param count Country count (the game's refresh has no bounds check).
     * @param stats Counters.
     * @return `false` when the call was refused or faulted (the layer is then off).
     */
    bool repaint(uptr country, std::int32_t id, std::int32_t count, Stats& stats) noexcept {
        if (!engine::game_calls_allowed()) {
            game_call_failed();
            return false;
        }
        uptr map = 0;
        if (id < 1 || id >= count || !in_game(map)) {
            return true;  // front end, loading screen or no map: the map is built later from the getter
        }
        const auto* tag = reinterpret_cast<const std::int32_t*>(country + core::off::country_tag);
        if (!engine::guarded_refresh_colour(originals_.refresh, reinterpret_cast<void*>(map), tag)) {
            game_call_failed();
            return false;
        }
        ++stats.refreshed;
        mark_modes(map);
        return true;
    }

    /**
     * @brief Marks the modes of `colour_map_modes` dirty (the byte the game's own mark writes for mode 0).
     * @param map Map object.
     */
    void mark_modes(uptr map) noexcept {
        uptr manager = 0;
        uptr modes = 0;
        if (!core::rd(mem_, map, core::off::map_manager, manager) || manager == 0 ||
            !core::rd(mem_, manager, core::off::map_modes, modes) || modes == 0) {
            return;
        }
        constexpr std::uint8_t dirty = 1;
        unsigned written = 0;
        for (const std::uint8_t mode : colour_map_modes) {
            if (mem_.write(modes + core::off::mode_dirty + mode, &dirty, sizeof(dirty))) {
                ++written;
            }
        }
        if (mark_lines_.fetch_add(1) < max_debug_lines) {
            note(core::logging::Channel::debug, "CAPIE_COLOUR modes marked written=%u of %u", written,
                 static_cast<unsigned>(colour_map_modes.size()));
        }
    }

    /// @brief A guarded game call failed or was refused: layer off, the getter passes through.
    void game_call_failed() noexcept {
        if (enabled_.exchange(false)) {
            note(core::logging::Channel::lifecycle,
                 "CAPIE_COLOUR RefreshCountryColor failed or refused (fault latch); layer off");
        }
    }

    /**
     * @brief Formats and writes one log line.
     * @param channel Channel.
     * @param format printf format.
     * @param args Arguments (numbers and status names only).
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

    const M& mem_;                          ///< Memory policy.
    Originals originals_{};                 ///< Trampoline and game function.
    uptr image_{};                          ///< Image base.
    uptr country_vtable_{};                 ///< `image + rva::vt_country`.
    LogFn log_{};                           ///< Log sink.
    std::atomic<bool> enabled_{false};      ///< Layer active.
    std::atomic<std::uint32_t> live_{0};    ///< Slots with a published block (fast path when 0).
    std::unique_ptr<Slot[]> slots_;         ///< One per tag id; allocated at activation, never freed.
    std::mutex mutex_;                      ///< Serialises writers; never held across a game call.
    std::atomic<int> store_lines_{0};       ///< Debug line budget.
    std::atomic<int> mark_lines_{0};        ///< Debug line budget.
};

/// @brief Memory policy of the colour layer in the game: guarded reads, SEH-only hot reads, the guarded write.
struct ColourMemory : flag::FlagMemory {
    /**
     * @brief Copies bytes of an object the game has just used, under SEH only (the getter's path).
     * @param address Address.
     * @param out Destination.
     * @param size Byte count.
     * @return `true` on a complete copy.
     */
    static bool hot_read(uptr address, void* out, std::size_t size) noexcept {
        return core::seh_copy(out, reinterpret_cast<const void*>(address), size);
    }
};

}  // namespace capie::country::colour
