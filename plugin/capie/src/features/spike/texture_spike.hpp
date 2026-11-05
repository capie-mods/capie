/**
 * @file texture_spike.hpp
 * @brief THROWAWAY in-game experiments S1 and S2 of the runtime catalogs (spec 12.1). Never in a release.
 *
 * Compiled only with `CAPIE_SPIKE=1` (`build.ps1 -Spike`, output `plugin/build/capie-spike`); the
 * build refuses any other output folder and the release flavours never include this file.
 *
 * The demo mod `plugin/capie/spikes/capie_spike_mod` declares a scripted GUI `capie_spike` with
 * slot sprites and buttons. CAPIE intercepts its clicks in the Dispatch hook (UI thread):
 *
 * - S1 `capie_spike_s1_click`: re-point `GFX_capie_spike_s1` to the next flag of a fixed list
 *   (`gfx/flags/<stem>.tga`; a vanilla list, or the KaiserreduX list after `capie_spike_list_click`)
 *   and forward the click unchanged.
 * - S2 `capie_spike_s2_click`: load the next flag into the hidden set (`GFX_capie_spike_s2_<1-p>`),
 *   then forward the click with the records `capie_sp_par` (the set to show) and `capie_sp_n`
 *   (the flip number); the mod stores them, so the image text switches sets.
 *   `capie_spike_auto_click` runs 200 such flips from the Tick hook (one every 250 ms while the
 *   window is visible); `capie_spike_stop_click` stops them.
 * - `capie_spike_reset_click`: re-point every spike sprite to its own placeholder file again.
 *
 * Re-point protocol (texture-release research 2026-11, section 4.1): FindSprite and the vtable
 * check before each write; `new = AddTexture(path)`; `tex = GetTexture(new)` (0: release `new`,
 * keep the slot); write `+0x1A0 = new`, `+0x170 = tex`, `+0xA4 = size` in that order; the old id
 * (owned by the sprite) is released by `ReleaseTexture` at least 3 window updates and 300 ms later,
 * on the UI thread. Every id CAPIE obtains is released exactly once. Portrait-path textures are
 * never touched. Each step is logged with `CAPIE_SPIKE` lines (texture manager live count,
 * outstanding references) in `capie_editor.log`.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../catalog/catalog_pins.hpp"
#include "../editor/public_editor.hpp"
#include "../editor/transport.hpp"
#include "../editor/window_view.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#if !CAPIE_SPIKE
#error "texture_spike.hpp is the throwaway spike build only (CAPIE_SPIKE=1)"
#endif

namespace capie::spike {

using core::uptr;
using editor::Origin;
using engine::MsvcString;

/// @brief `sprite* FindSprite(database*, const string* name)`.
using FindFn = void* (*)(void*, const MsvcString*);
/// @brief `int AddTexture(texmgr*, const string* path, const options*, bool load_now)`.
using AddFn = int (*)(void*, const MsvcString*, const void*, bool);
/// @brief `texture* GetTexture(texmgr*, int id)`.
using GetFn = void* (*)(void*, int);
/// @brief `int* TextureSize(texmgr*, int out[2], int id)`.
using SizeFn = std::int32_t* (*)(void*, std::int32_t*, int);
/// @brief `void ReleaseTexture(texmgr*, int id)`.
using ReleaseFn = void (*)(void*, int);

/// @brief The lazy loader's AddTexture options (`sprite.options`: zero, the load flags, zero).
struct Options {
    std::int32_t zero{0};            ///< First word.
    std::int32_t flags{0x1000001};   ///< Load flags.
    std::uint8_t tail{0};            ///< Last byte.
    std::array<std::uint8_t, 3> padding{};  ///< Unused.
};
static_assert(sizeof(Options) == 12);

/// @brief Spike model and selectors.
inline constexpr char model_name[] = "capie_spike";
inline constexpr char select_s1[] = "capie_spike_s1_click";          ///< S1 re-point.
inline constexpr char select_s2[] = "capie_spike_s2_click";          ///< S2 one flip.
inline constexpr char select_auto[] = "capie_spike_auto_click";      ///< S2 200 flips.
inline constexpr char select_stop[] = "capie_spike_stop_click";      ///< Stop the flips.
inline constexpr char select_reset[] = "capie_spike_reset_click";    ///< Back to the placeholders.
inline constexpr char select_list[] = "capie_spike_list_click";      ///< Toggle the vanilla / KaiserreduX flag list.
inline constexpr char sprite_s1[] = "GFX_capie_spike_s1";           ///< S1 slot.
inline constexpr std::array<const char*, 2> sprite_s2 = {"GFX_capie_spike_s2_0", "GFX_capie_spike_s2_1"};
/// @brief Record: the S2 set to show.
inline constexpr char record_par[] = "capie_sp_par";
/// @brief Record: the S2 flip number.
inline constexpr char record_n[] = "capie_sp_n";
/// @brief Flips of one auto run.
inline constexpr int auto_flips = 200;
/// @brief Milliseconds between two auto flips.
inline constexpr unsigned long long auto_interval_ms = 250;
/// @brief Window updates before a deferred release.
inline constexpr unsigned release_ticks = 3;
/// @brief Milliseconds before a deferred release.
inline constexpr unsigned long long release_ms = 300;
/// @brief Fail-closed bound of outstanding CAPIE references.
inline constexpr int max_outstanding = 1024;
/// @brief Deferred releases held at most.
inline constexpr std::size_t max_pending = 64;
/// @brief Longest path or sprite name handled.
inline constexpr std::size_t max_path = 128;

/// @brief Vanilla flag stems the spike cycles through (each exists in vanilla `gfx/flags`).
inline constexpr std::array<const char*, 43> vanilla_flags = {
    "GER_neutrality", "SOV_neutrality", "ENG_neutrality", "FRA_neutrality", "ITA_neutrality", "JAP_neutrality",
    "USA_neutrality", "POL_neutrality", "CZE_neutrality", "AUS_neutrality", "HUN_neutrality", "ROM_neutrality",
    "YUG_neutrality", "BUL_neutrality", "GRE_neutrality", "TUR_neutrality", "SPR_neutrality", "POR_neutrality",
    "SWE_neutrality", "NOR_neutrality", "DEN_neutrality", "FIN_neutrality", "EST_neutrality", "LAT_neutrality",
    "LIT_neutrality", "HOL_neutrality", "BEL_neutrality", "LUX_neutrality", "SWI_neutrality", "CAN_neutrality",
    "AST_neutrality", "NZL_neutrality", "SAF_neutrality", "RAJ_fascism",    "CHI_neutrality", "PRC_neutrality",
    "MAN_neutrality", "SIA_neutrality", "MEX_neutrality", "BRA_neutrality", "ARG_neutrality", "CHL_neutrality",
    "IRE_neutrality"};
/// @brief KaiserreduX flag stems (each exists in KRX `gfx/flags`, which replaces the vanilla folder).
inline constexpr std::array<const char*, 48> krx_flags = {
    "GER", "LUX", "SIK", "RUS_chern", "WHR", "AOG", "NFM", "ALB_Enver", "NFE_AF", "ITA_Shimoi", "FIN_kingdom",
    "MOL_syndicalist", "SOM_MAJEERTEEN", "MTR_soviet", "MEX", "MLT_OUTMERE", "SER_totalist", "ARZ_fun", "TTR_BKN",
    "ABK_kingdom", "AFG_OTT", "TZN", "GLC_anationalismo", "SAF_republic", "YUC_CRUZOOB", "VEN", "TRM_TRB",
    "TXS_texaco", "AFG_syndicalist", "NEE_HAROLD", "SWE_darnand", "MAD_BOURBON", "FRP_GER", "MZB_totalist",
    "SLO_totalist", "XSM_QIE", "SRI_green", "BAY", "TAN_HAM", "BOL_wiphala", "COR_SWAMPFOLK", "SWE_radsoc",
    "DSR_Dominion", "NST", "RVA", "JAP_real", "KTG_reformist", "AFG_PAKISTAN"};
/// @brief A release waiting for the window to have drawn the swap.
struct Pending {
    int id{-1};                    ///< Id to release.
    unsigned ticks_left{};         ///< Window updates still to wait.
    unsigned long long due_ms{};   ///< Earliest time.
};

/// @brief Spike state (UI thread only; the Dispatch and Tick hooks run there).
struct State {
    bool ok{};                                        ///< Pins and RTTI passed at init.
    FindFn find{};                                    ///< FindSprite.
    AddFn add{};                                      ///< AddTexture.
    GetFn get{};                                      ///< GetTexture.
    SizeFn size{};                                    ///< TextureSize.
    ReleaseFn release{};                              ///< ReleaseTexture.
    std::size_t next_flag{};                          ///< Next index into the flag list.
    bool krx{};                                       ///< Use the KaiserreduX list.
    int shown_set{};                                  ///< S2 set CAPIE believes is shown.
    int flips{};                                      ///< S2 flips done.
    int auto_left{};                                  ///< Auto flips still to run.
    unsigned long long last_flip_ms{};                ///< Time of the last auto flip.
    int outstanding{};                                ///< AddTexture results not yet released.
    std::array<Pending, max_pending> pending{};       ///< Deferred releases.
    std::array<std::array<char, max_path>, 3> placeholder{};  ///< Placeholder path per sprite (s1, s2_0, s2_1).
    std::uintptr_t model{};                           ///< Spike model object seen at the last click (Tick filter).
};

/// @brief The spike of this process.
inline State state{};

/**
 * @brief Writes one `CAPIE_SPIKE` line.
 * @param format printf format.
 * @param args Arguments.
 */
template <class... Args>
void note(const char* format, Args... args) noexcept {
    std::array<char, 400> line{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): bounded snprintf with a literal format
    int count = std::snprintf(line.data(), line.size() - 2, format, args...);
    if (count <= 0) {
        return;
    }
    const auto size = static_cast<std::size_t>(count) < line.size() - 2 ? static_cast<std::size_t>(count) : line.size() - 3;
    line[size] = '\r';
    line[size + 1] = '\n';
    editor::write_log(line.data(), size + 2);
}

/**
 * @brief Arms the spike after the pins (editor initialization).
 * @param text `.text` of the game.
 * @param image Image base.
 */
inline void prepare(const core::TextSection& text, uptr image) noexcept {
    const char* failed = catalog::pins::first_failed_sprite_pin(text);
    if (failed == nullptr) {
        failed = core::first_wrong_class(core::ProcessMemory{}, image, core::catalog_vtable_classes);
    }
    if (failed != nullptr) {
        note("CAPIE_SPIKE off: pin %s failed", failed);
        return;
    }
    state.find = reinterpret_cast<FindFn>(image + core::rva::sprite_find);
    state.add = reinterpret_cast<AddFn>(image + core::rva::texture_add);
    state.get = reinterpret_cast<GetFn>(image + core::rva::texture_get);
    state.size = reinterpret_cast<SizeFn>(image + core::rva::texture_size);
    state.release = reinterpret_cast<ReleaseFn>(image + core::rva::texture_release);
    state.ok = true;
    note("CAPIE_SPIKE armed (THROWAWAY BUILD, S1 and S2; model %s)", model_name);
}

/// @brief A resolved slot sprite.
struct Slot {
    uptr sprite{};   ///< CSpriteType.
    uptr texmgr{};   ///< Its texture manager.
};

/**
 * @brief FindSprite plus the checks of spec 4.3 (vtable, texture manager of the database).
 * @param name Sprite name.
 * @param slot Receives the sprite.
 * @return `false` when missing or not a CSpriteType.
 */
inline bool resolve(const char* name, Slot& slot) noexcept {
    slot = Slot{};
    uptr db = 0;
    uptr db_texmgr = 0;
    if (!engine::game_calls_allowed() ||
        !core::rd(core::ProcessMemory{}, engine::image_base, core::rva::sprite_database_global, db) || db == 0 ||
        !core::rd(core::ProcessMemory{}, db, core::off::db_texture_manager, db_texmgr)) {
        return false;
    }
    const auto argument = engine::borrowed_string(name, std::strlen(name));
    void* found = nullptr;
    __try {
        found = state.find(reinterpret_cast<void*>(db), &argument);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        return false;
    }
    const auto sprite = reinterpret_cast<uptr>(found);
    uptr vtable = 0;
    uptr sprite_db = 0;
    uptr texmgr = 0;
    if (sprite == 0 || !core::rd(core::ProcessMemory{}, sprite, 0, vtable) ||
        vtable != engine::image_base + core::rva::vt_sprite_type ||
        !core::rd(core::ProcessMemory{}, sprite, core::off::sprite_db, sprite_db) ||
        !core::rd(core::ProcessMemory{}, sprite_db, core::off::db_texture_manager, texmgr) || texmgr != db_texmgr ||
        texmgr == 0) {
        note("CAPIE_SPIKE sprite %s not usable (found=%p vtable_ok=%d)", name, found,
             vtable == engine::image_base + core::rva::vt_sprite_type ? 1 : 0);
        return false;
    }
    slot = {sprite, texmgr};
    return true;
}

/**
 * @brief The texture manager's live path count (diagnostics only).
 * @param texmgr Texture manager.
 * @return Count, or -1.
 */
inline int live_count(uptr texmgr) noexcept {
    std::int32_t live = -1;
    return core::rd(core::ProcessMemory{}, texmgr, core::off::texmgr_live, live) ? live : -1;
}

/**
 * @brief Drops one CAPIE-owned reference now (UI thread).
 * @param texmgr Texture manager.
 * @param id Id.
 */
inline void release_now(uptr texmgr, int id) noexcept {
    if (id < 0 || !engine::game_calls_allowed()) {
        return;
    }
    __try {
        state.release(reinterpret_cast<void*>(texmgr), id);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        note("CAPIE_SPIKE ReleaseTexture faulted id=%d; game calls off", id);
        return;
    }
    --state.outstanding;
}

/**
 * @brief Queues the release of an id the sprite owned (after the swap).
 * @param id Old id.
 * @return `false` when the queue is full (the id is then leaked, logged).
 */
inline bool defer_release(int id) noexcept {
    for (auto& slot : state.pending) {
        if (slot.id < 0) {
            slot = {id, release_ticks, GetTickCount64() + release_ms};
            return true;
        }
    }
    note("CAPIE_SPIKE release queue full; id %d leaked", id);
    return false;
}

/**
 * @brief Re-points one slot sprite (the protocol in the file comment).
 * @param name Sprite name.
 * @param path Texture path.
 * @return `true` when the slot now shows `path`.
 */
inline bool repoint(const char* name, const char* path) noexcept {
    Slot slot{};
    if (!state.ok || !resolve(name, slot)) {
        return false;
    }
    if (state.outstanding >= max_outstanding) {
        note("CAPIE_SPIKE outstanding cap %d reached; %s keeps its texture", max_outstanding, name);
        return false;
    }
    const auto argument = engine::borrowed_string(path, std::strlen(path));
    const Options options{};
    int id = -1;
    void* texture = nullptr;
    std::array<std::int32_t, 2> size{};
    const auto texmgr = reinterpret_cast<void*>(slot.texmgr);
    __try {
        id = state.add(texmgr, &argument, &options, true);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        note("CAPIE_SPIKE AddTexture faulted; game calls off");
        return false;
    }
    ++state.outstanding;  // the sprite will own it, or it is released below
    __try {
        texture = state.get(texmgr, id);
        if (texture != nullptr) {
            const std::int32_t* got = state.size(texmgr, size.data(), id);
            if (got != nullptr) {
                size = {got[0], got[1]};
            }
        }
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        note("CAPIE_SPIKE GetTexture/TextureSize faulted; game calls off");
        return false;
    }
    if (texture == nullptr) {
        release_now(slot.texmgr, id);
        note("CAPIE_SPIKE missing texture path=%s id=%d: released, %s unchanged", path, id, name);
        return false;
    }
    std::int32_t old = -1;
    if (!core::rd(core::ProcessMemory{}, slot.sprite, core::off::sprite_texture_id, old)) {
        release_now(slot.texmgr, id);
        return false;
    }
    // Order of the game's lazy loader: id, texture, size.
    const bool written =
        core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_texture_id), &id, sizeof(id)) &&
        core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_texture), &texture, sizeof(texture)) &&
        core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_size), size.data(), sizeof(size));
    if (!written) {
        note("CAPIE_SPIKE write to %s faulted (partial write possible); spike off", name);
        state.ok = false;
        return false;
    }
    if (old >= 0 && old != id) {
        static_cast<void>(defer_release(old));
    } else if (old == id) {
        release_now(slot.texmgr, id);  // same file again: AddTexture added a second reference
    }
    note("CAPIE_SPIKE repoint sprite=%s path=%s id=%d old=%d tex=%p size=%dx%d live=%d outstanding=%d", name, path, id,
         old, texture, size[0], size[1], live_count(slot.texmgr), state.outstanding);
    return true;
}

/**
 * @brief Saves a sprite's own texture file once, as its placeholder.
 * @param index 0 = s1, 1/2 = s2 sets.
 * @param name Sprite name.
 */
inline void remember_placeholder(std::size_t index, const char* name) noexcept {
    auto& saved = state.placeholder[index];
    if (saved[0] != '\0') {
        return;
    }
    Slot slot{};
    std::size_t size = 0;
    if (resolve(name, slot) &&
        engine::string_bytes(reinterpret_cast<void*>(slot.sprite + core::off::sprite_path), saved.data(),
                             saved.size() - 1, size)) {
        saved[size] = '\0';
        note("CAPIE_SPIKE placeholder sprite=%s path=%s", name, saved.data());
    } else {
        saved[0] = '\0';
    }
}

/**
 * @brief The next flag path of the cycle.
 * @param out Receives `gfx/flags/<TAG>.tga`.
 */
inline void next_flag(std::array<char, max_path>& out) noexcept {
    const char* stem = state.krx ? krx_flags[state.next_flag % krx_flags.size()]
                                 : vanilla_flags[state.next_flag % vanilla_flags.size()];
    ++state.next_flag;
    static_cast<void>(std::snprintf(out.data(), out.size(), "gfx/flags/%s.tga", stem));
}

/**
 * @brief Re-points a sprite to the next flag of the cycle that loads in this mod set.
 * @param name Sprite name.
 * @param out Receives the path that was shown (or the last one tried).
 * @return `true` when a flag was shown; flags missing in this mod set are skipped (at most one
 *         pass over the list), so a wrong list choice cannot stall the cycle.
 */
inline bool repoint_next(const char* name, std::array<char, max_path>& out) noexcept {
    const std::size_t tries = state.krx ? krx_flags.size() : vanilla_flags.size();
    for (std::size_t i = 0; i < tries && state.ok; ++i) {
        next_flag(out);
        if (repoint(name, out.data())) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Forwards an S2 flip with its records (the frame substitution of the grammar editors).
 * @param context Window context.
 * @param selector Selector string.
 * @param shown Set to show.
 * @return `false` when the records could not be built (the click is then forwarded unchanged).
 * @note Not `noexcept`: it calls the original, whose exceptions pass through.
 */
inline bool forward_flip(void* context, const MsvcString* selector, int shown) {
    Origin current{};
    current.context = context;
    void* model = nullptr;
    if (!editor::member(context, core::off::context_model, model) || model == nullptr) {
        return false;
    }
    current.model_name =
        reinterpret_cast<const MsvcString*>(reinterpret_cast<std::uintptr_t>(model) + core::off::model_name);
    current.selector = selector;
    current.id = editor::next_observation_id();
    current.public_model = model_name;
    current.public_apply = select_s2;
    editor::TempVector source{};
    std::size_t kept = 0;
    std::size_t sanitized = 0;
    if (editor::copy_original_records(current, source, kept, sanitized) != editor::CopyResult::ok ||
        !editor::append_record(current, kept, record_par, static_cast<std::uint64_t>(shown)) ||
        !editor::append_record(current, kept, record_n, static_cast<std::uint64_t>(state.flips))) {
        return false;
    }
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    {
        const editor::OriginFrame frame(&current);
        editor::original_dispatch(context, const_cast<MsvcString*>(selector));
    }
    if (current.ctor_calls != 1) {
        note("CAPIE_SPIKE flip n=%d: constructor calls %u (records not carried)", state.flips, current.ctor_calls);
    }
    return true;
}

/**
 * @brief One S2 flip: load the hidden set, then forward with the set to show.
 * @param context Window context.
 * @param selector Selector string (the click's, or the spike's own for auto flips).
 * @note Not `noexcept`: it calls the original dispatch.
 */
inline void flip(void* context, const MsvcString* selector) {
    remember_placeholder(1, sprite_s2[0]);
    remember_placeholder(2, sprite_s2[1]);
    const int hidden = 1 - state.shown_set;
    std::array<char, max_path> path{};
    if (!repoint_next(sprite_s2[static_cast<std::size_t>(hidden)], path)) {
        note("CAPIE_SPIKE flip n=%d skipped (set %d not re-pointed); click forwarded unchanged", state.flips + 1,
             hidden);
        editor::original_dispatch(context, const_cast<MsvcString*>(selector));
        return;
    }
    ++state.flips;
    state.shown_set = hidden;
    note("CAPIE_SPIKE flip n=%d show_set=%d path=%s", state.flips, hidden, path.data());
    if (!forward_flip(context, selector, hidden)) {
        note("CAPIE_SPIKE flip n=%d records not built; forwarded unchanged", state.flips);
        editor::original_dispatch(context, const_cast<MsvcString*>(selector));
    }
}

/**
 * @brief Whether a context belongs to the spike model.
 * @param context Window context.
 * @return `true` for `capie_spike`.
 */
inline bool spike_context(void* context) noexcept {
    std::uintptr_t vtable = 0;
    void* model = nullptr;
    return editor::member(context, 0, vtable) && vtable == engine::image_base + core::rva::vt_sg_context &&
           editor::member(context, core::off::context_model, model) && model != nullptr &&
           engine::name_equals(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(model) + core::off::model_name),
                               model_name);
}

/**
 * @brief Dispatch hook entry: handles the spike's clicks.
 * @param context Window context.
 * @param opaque_selector Selector.
 * @return `true` when the click was handled (forwarded exactly once here).
 * @note Not `noexcept`: it calls the original dispatch.
 */
inline bool dispatch(void* context, void* opaque_selector) {
    const auto* selector = static_cast<const MsvcString*>(opaque_selector);
    if (!state.ok || !spike_context(context)) {
        return false;
    }
    static_cast<void>(editor::member(context, core::off::context_model, state.model));
    if (engine::name_equals(selector, select_s1)) {
        remember_placeholder(0, sprite_s1);
        std::array<char, max_path> path{};
        const bool shown = repoint_next(sprite_s1, path);
        note("CAPIE_SPIKE s1 click path=%s ok=%d", path.data(), shown ? 1 : 0);
    } else if (engine::name_equals(selector, select_s2)) {
        flip(context, selector);
        return true;
    } else if (engine::name_equals(selector, select_auto)) {
        state.auto_left = auto_flips;
        state.last_flip_ms = 0;
        note("CAPIE_SPIKE auto start flips=%d interval_ms=%llu", auto_flips, auto_interval_ms);
    } else if (engine::name_equals(selector, select_stop)) {
        note("CAPIE_SPIKE auto stop left=%d", state.auto_left);
        state.auto_left = 0;
    } else if (engine::name_equals(selector, select_list)) {
        state.krx = !state.krx;
        state.next_flag = 0;
        note("CAPIE_SPIKE flag list %s", state.krx ? "kaiserredux" : "vanilla");
    } else if (engine::name_equals(selector, select_reset)) {
        const char* names[] = {sprite_s1, sprite_s2[0], sprite_s2[1]};
        for (std::size_t i = 0; i < 3; ++i) {
            if (state.placeholder[i][0] != '\0') {
                note("CAPIE_SPIKE reset sprite=%s ok=%d", names[i], repoint(names[i], state.placeholder[i].data()) ? 1 : 0);
            }
        }
    } else {
        return false;
    }
    editor::original_dispatch(context, opaque_selector);
    return true;
}

/**
 * @brief Tick hook entry (after the original): deferred releases and auto flips of a visible spike window.
 * @param window Ticking window.
 * @note Not `noexcept`: an auto flip calls the original dispatch.
 */
inline void tick(void* window) {
    if (!state.ok) {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    if (address == 0 || address > core::uptr_max - core::off::window_context) {
        return;
    }
    void* context = reinterpret_cast<void*>(address + core::off::window_context);
    std::uintptr_t model_seen = 0;
    // Per-frame filter: one SEH read of the model pointer, compared with the spike's model.
    if (state.model == 0 || address > core::uptr_max - core::off::window_model ||
        !core::seh_copy(&model_seen, reinterpret_cast<const void*>(address + core::off::window_model), sizeof(model_seen)) ||
        model_seen != state.model || !spike_context(context)) {
        return;
    }
    // Deferred releases: the old texture of a re-pointed sprite, after the window drew the swap.
    const auto now = GetTickCount64();
    for (auto& slot : state.pending) {
        if (slot.id < 0) {
            continue;
        }
        if (slot.ticks_left > 0) {
            --slot.ticks_left;
            continue;
        }
        if (now >= slot.due_ms) {
            Slot texmgr{};
            if (resolve(sprite_s1, texmgr)) {
                release_now(texmgr.texmgr, slot.id);
                note("CAPIE_SPIKE released id=%d live=%d outstanding=%d", slot.id, live_count(texmgr.texmgr),
                     state.outstanding);
            }
            slot.id = -1;
        }
    }
    if (state.auto_left <= 0 || now < state.last_flip_ms + auto_interval_ms) {
        return;
    }
    void* model = nullptr;
    editor::View view{};
    if (!editor::member(context, core::off::context_model, model) ||
        editor::inspect_chain(window, model, view) != editor::ViewResult::visible) {
        return;  // only while the window is shown
    }
    state.last_flip_ms = now;
    --state.auto_left;
    static const auto selector = engine::borrowed_string(select_s2, sizeof(select_s2) - 1);
    flip(context, &selector);
    if (state.auto_left == 0) {
        note("CAPIE_SPIKE auto done flips=%d outstanding=%d", state.flips, state.outstanding);
    }
}

}  // namespace capie::spike
