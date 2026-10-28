/**
 * @file flag_layer_test.cpp
 * @brief Offline tests of the country flag layer (API level 6): packing, cosmetic gate and the three hooks.
 *
 * A fake memory holds countries with variable containers built with the engine's insertion
 * rule (shared with name_layer_test.cpp), the game-state country list and a fake flag atlas
 * (cosmetic key array at +0xC0, count at +0xCC). The real hook bodies (`FlagLayer<FakeMemory>`)
 * run against fake originals: the fake `LoadCountryFlag` calls `BuildFlagCandidates` through the
 * hook with the country's cosmetic tag field as the game does and returns a status the test
 * chooses; the fake `BuildFlagCandidates` records its arguments. Also covered: the two-attempt
 * fallback, the K7 argument check, the key write only for changed, in-range, matching slots,
 * thread-local frames, and that the log carries no player text. No game, no EXE, no MinHook.
 */
#include "../src/features/country/flag_layer_hooks.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace layer = capie::country::layer;
namespace flag = capie::country::flag;
using capie::core::uptr;

int checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
int failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

// Addresses, sizes and codes below are literal test data.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// @brief Byte-addressed fake memory made of separate regions.
struct FakeMemory {
    mutable std::map<uptr, std::vector<unsigned char>>
        regions;  ///< Start address -> bytes (mutable: `write` is const).

    /**
     * @brief Reads `size` bytes at `address` when one region holds all of them.
     * @param address Address.
     * @param out Destination.
     * @param size Byte count.
     * @return `false` when any byte is unmapped.
     */
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        auto found = regions.upper_bound(address);
        if (found == regions.begin()) {
            return false;
        }
        --found;
        const auto offset = address - found->first;
        if (offset > found->second.size() || size > found->second.size() - offset) {
            return false;
        }
        if (size != 0) {
            std::memcpy(out, found->second.data() + offset, size);
        }
        return true;
    }

    /**
     * @brief Writes `size` bytes at `address` when one region holds all of them (the slot-key write).
     * @param address Address.
     * @param data Bytes.
     * @param size Byte count.
     * @return `false` when any byte is unmapped or the region is read-only.
     */
    bool write(uptr address, const void* data, std::size_t size) const noexcept {
        auto found = regions.upper_bound(address);
        if (found == regions.begin()) {
            return false;
        }
        --found;
        const auto offset = address - found->first;
        if (read_only.contains(found->first) || offset > found->second.size() || size > found->second.size() - offset) {
            return false;
        }
        std::memcpy(found->second.data() + offset, data, size);
        ++writes;
        return true;
    }
    std::set<uptr> read_only;  ///< Regions whose writes fail.
    mutable int writes{};      ///< Successful writes.

    /**
     * @brief Maps a zeroed region.
     * @param address Start.
     * @param size Bytes.
     * @return Pointer to the region's bytes.
     */
    unsigned char* map(uptr address, std::size_t size) {
        auto& bytes = regions[address];
        bytes.assign(size, 0);
        return bytes.data();
    }

    /**
     * @brief Writes a value into a mapped region.
     * @param address Address inside a region.
     * @param value Value.
     */
    template <class T>
    void put(uptr address, const T& value) {
        auto found = regions.upper_bound(address);
        --found;
        std::memcpy(found->second.data() + (address - found->first), &value, sizeof(T));
    }
};

/// @brief Next free fake address for heap keys.
uptr next_heap = 0x7000'0000;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test allocator

/**
 * @brief Builds a variable container with the engine's insertion rule.
 * @param mem Fake memory.
 * @param container Container address (its fields at +0x18, +0x24, +0x28 are written).
 * @param entries Entry array address.
 * @param mask Bucket mask (2^n - 1).
 * @param extra Extra slots past the mask.
 * @param variables Name -> raw fixed-point value.
 * @return `false` when an insertion ran past the extra slots.
 */
bool build_table(FakeMemory& mem, uptr container, uptr entries, std::int32_t mask, std::uint8_t extra,
                 const std::vector<std::pair<std::string, std::int64_t>>& variables) {
    mem.map(container, 0x40);
    mem.put(container + 0x18, entries);
    mem.put(container + 0x24, mask);
    mem.put(container + 0x28, extra);
    const std::size_t slots = static_cast<std::size_t>(mask) + extra + 1;
    std::vector<layer::Entry> table(slots);
    std::vector<std::string> names(slots);
    for (const auto& [name, value] : variables) {
        layer::Entry entry{};
        std::string key = name;
        entry.value = value;
        auto index =
            static_cast<std::size_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(layer::fnv1a(key))) & mask);
        std::uint8_t distance = 1;
        while (true) {
            if (index >= slots) {
                return false;
            }
            if (table[index].probe == 0) {
                entry.probe = distance;
                table[index] = entry;
                names[index] = key;
                break;
            }
            if (table[index].probe < distance) {  // robin hood: the poorer entry takes the slot
                entry.probe = distance;
                std::swap(table[index], entry);
                std::swap(names[index], key);
                distance = entry.probe;
            }
            ++index;
            ++distance;
        }
    }
    for (std::size_t i = 0; i < slots; ++i) {
        if (table[i].probe == 0) {
            continue;
        }
        table[i].key_size = names[i].size();
        if (names[i].size() <= 15) {
            table[i].key_capacity = 15;
            std::memcpy(table[i].key.small.data(), names[i].data(), names[i].size());
        } else {
            table[i].key_capacity = names[i].size();
            const auto heap = next_heap;
            next_heap += 0x100;
            auto* bytes = mem.map(heap, names[i].size() + 1);
            std::copy(names[i].begin(), names[i].end(), bytes);
            table[i].key.pointer = heap;
        }
    }
    auto* bytes = mem.map(entries, slots * sizeof(layer::Entry));
    std::memcpy(bytes, table.data(), slots * sizeof(layer::Entry));
    return true;
}

/// @brief Fixed-point value of an integer.
constexpr std::int64_t fp(std::int64_t value) {
    return value * layer::fixed_point;
}

/**
 * @brief Maps a country object whose variable container is at `container`.
 * @param mem Fake memory.
 * @param country Country address.
 * @param container Container address (0 = no variables).
 */
void map_country(FakeMemory& mem, uptr country, uptr container) {
    mem.map(country, 0x2000);
    mem.put(country + 0x218, container);
}

/**
 * @brief Packs a file-name text three characters per element (the contract's rule, written independently).
 * @param text Text in `a-z A-Z 0-9 _`.
 * @return Elements.
 */
std::vector<std::int64_t> pack(const std::string& text) {
    const std::string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    std::vector<std::int64_t> out;
    for (std::size_t i = 0; i < text.size(); i += 3) {
        std::int64_t element = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::int64_t code =
                i + k < text.size() ? static_cast<std::int64_t>(alphabet.find(text[i + k])) + 1 : 0;
            element = (element * 64) + code;
        }
        out.push_back(element);
    }
    return out;
}

/**
 * @brief Variables of a flag layer as a mod stores them.
 * @param stem File stem.
 * @param snapshot Cosmetic snapshot; `nullptr` = no snapshot.
 * @return Variables.
 */
std::vector<std::pair<std::string, std::int64_t>> flag_variables(const std::string& stem, const char* snapshot) {
    std::vector<std::pair<std::string, std::int64_t>> out{{"capie_cf_ver", fp(1)},
                                                          {"capie_cf_len", fp(static_cast<std::int64_t>(stem.size()))}};
    const auto stem_elements = pack(stem);
    out.emplace_back("capie_cf_s^num", fp(static_cast<std::int64_t>(stem_elements.size())));
    for (std::size_t i = 0; i < stem_elements.size(); ++i) {
        out.emplace_back("capie_cf_s^" + std::to_string(i), fp(stem_elements[i]));
    }
    if (snapshot != nullptr) {
        const std::string cos = snapshot;
        out.emplace_back("capie_cf_cos_len", fp(static_cast<std::int64_t>(cos.size())));
        const auto cos_elements = pack(cos);
        if (!cos.empty()) {
            out.emplace_back("capie_cf_cos^num", fp(static_cast<std::int64_t>(cos_elements.size())));
        }
        for (std::size_t i = 0; i < cos_elements.size(); ++i) {
            out.emplace_back("capie_cf_cos^" + std::to_string(i), fp(cos_elements[i]));
        }
    }
    out.emplace_back("capie_cn_ver", fp(1));  // a name layer next to it must not mix in
    return out;
}

/**
 * @brief Replaces a variable.
 * @param vars Variables.
 * @param name Name.
 * @param value Raw value.
 * @return The list.
 */
std::vector<std::pair<std::string, std::int64_t>> with(std::vector<std::pair<std::string, std::int64_t>> vars,
                                                       const std::string& name, std::int64_t value) {
    bool found = false;
    for (auto& [key, raw] : vars) {
        if (key == name) {
            raw = value;
            found = true;
        }
    }
    if (!found) {
        vars.emplace_back(name, value);
    }
    return vars;
}

/**
 * @brief Removes a variable.
 * @param vars Variables.
 * @param name Name.
 * @return The list.
 */
std::vector<std::pair<std::string, std::int64_t>> without(std::vector<std::pair<std::string, std::int64_t>> vars,
                                                          const std::string& name) {
    std::erase_if(vars, [&name](const auto& entry) { return entry.first == name; });
    return vars;
}

/**
 * @brief Reads the layer of a fresh country.
 * @param vars Variables.
 * @return Layer.
 */
flag::Layer layer_of(const std::vector<std::pair<std::string, std::int64_t>>& vars) {
    FakeMemory mem;
    map_country(mem, 0x1000'0000, 0x2000'0000);
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, vars), "table builds");
    return flag::read_flag_layer(mem, 0x1000'0000);
}

void codec_tests() {
    check(flag::code_of('a') == 1 && flag::code_of('Z') == 52 && flag::code_of('0') == 53 && flag::code_of('_') == 63 &&
              flag::code_of('.') == 0 && flag::code_of('/') == 0 && flag::code_of('\\') == 0 && flag::code_of(':') == 0,
          "file-name alphabet: no path characters");
    bool round = true;
    for (std::int64_t code = 1; code <= 63; ++code) {
        round = round && flag::code_of(flag::char_of(code)) == code;
    }
    check(round && flag::char_of(0) == '\0' && flag::char_of(64) == '\0', "codes 1..63 round-trip, 0 and 64 invalid");
    check(pack("POL_") == std::vector<std::int64_t>{174694, 258048}, "CONTRACT 17.3 example");
}

void decode_tests() {
    auto got = layer_of(flag_variables("POL_KINGDOM_neutrality", "POL_KINGDOM"));
    check(got.status == layer::Status::valid && got.stem.view() == "POL_KINGDOM_neutrality" && got.has_snapshot &&
              got.snapshot.view() == "POL_KINGDOM",
          "valid layer with snapshot");
    got = layer_of(flag_variables("A", nullptr));
    check(got.status == layer::Status::valid && got.stem.view() == "A" && !got.has_snapshot,
          "one character, no snapshot");
    got = layer_of(flag_variables(std::string(63, 'z'), ""));
    check(got.status == layer::Status::valid && got.stem.size == 63 && got.has_snapshot && got.snapshot.size == 0,
          "63 characters, snapshot 'no cosmetic tag'");
    check(layer_of({{"other", fp(1)}}).status == layer::Status::absent, "no capie_cf_ver: absent");
    const auto base = flag_variables("POL_KINGDOM", "POL_x");
    const std::pair<const char*, std::vector<std::pair<std::string, std::int64_t>>> invalid[] = {
        {"version 2", with(base, "capie_cf_ver", fp(2))},
        {"length missing", without(base, "capie_cf_len")},
        {"length 0", with(base, "capie_cf_len", 0)},
        {"length 64", with(base, "capie_cf_len", fp(64))},
        {"length fraction", with(base, "capie_cf_len", fp(11) + 1)},
        {"length shorter than the elements", with(base, "capie_cf_len", fp(8))},
        {"length longer than the elements", with(base, "capie_cf_len", fp(13))},
        {"^num wrong", with(base, "capie_cf_s^num", fp(5))},
        {"element missing", without(base, "capie_cf_s^2")},
        {"element 262144", with(base, "capie_cf_s^0", fp(262144))},
        {"element negative", with(base, "capie_cf_s^0", fp(-1))},
        {"element fraction", with(base, "capie_cf_s^0", fp(174694) + 1)},
        {"zero inside", with(base, "capie_cf_s^1", fp(0))},
        {"padding not zero", with(base, "capie_cf_s^3", fp(pack("Mxy")[0]))},
        {"snapshot length 64", with(base, "capie_cf_cos_len", fp(64))},
        {"snapshot element missing", without(base, "capie_cf_cos^0")},
    };
    for (const auto& [label, vars] : invalid) {
        const auto bad = layer_of(vars);
        check(bad.status == layer::Status::invalid && bad.stem.size == 0, std::string("invalid: ") + label);
    }
}

void gate_tests() {
    const auto with_snapshot = layer_of(flag_variables("POL_KINGDOM", "POL_x"));
    check(flag::flag_shown(with_snapshot, "POL_x", false), "snapshot matches: shown");
    check(!flag::flag_shown(with_snapshot, "POL_y", false), "cosmetic tag changed: hidden at once");
    check(!flag::flag_shown(with_snapshot, "", false), "cosmetic tag dropped: hidden");
    check(!flag::flag_shown(with_snapshot, "POL_x", true), "a too long tag never matches");
    const auto none = layer_of(flag_variables("POL_KINGDOM", ""));
    check(flag::flag_shown(none, "", false) && !flag::flag_shown(none, "POL_x", false),
          "snapshot 'no cosmetic tag': hidden once a tag appears");
    const auto unchecked = layer_of(flag_variables("POL_KINGDOM", nullptr));
    check(flag::flag_shown(unchecked, "anything", false), "no snapshot: no gate");
    check(!flag::flag_shown(flag::Layer{}, "", false), "no layer: not shown");
}

// ---- Hooks --------------------------------------------------------------------------------------

/// @brief Fake game state.
struct Game {
    flag::FlagLayer<FakeMemory>* layer{};  ///< Layer under test.
    std::vector<std::string> tags;         ///< Tag argument of every candidate call.
    std::vector<std::string> cosmetics;    ///< Cosmetic argument of every candidate call.
    std::vector<void*> groups;             ///< Group argument of every candidate call.
    std::vector<int> statuses;             ///< Statuses the fake loader returns, in order.
    int loads{};                           ///< Calls of the original LoadCountryFlag.
    int atlas_calls{};                     ///< Calls of the original UpdateFlagAtlas.
    std::vector<std::string> log;          ///< Log lines.
    void* group{};                         ///< The country's ideology group pointer.
    uptr cosmetic{};                       ///< Cosmetic field the loader passes (0 = the loaded country's).
    bool throw_load{};                     ///< LoadCountryFlag throws a C++ exception after its candidates.
};
Game game;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake game state

/**
 * @brief Text of a host MSVC string.
 * @param value String.
 * @return Text.
 */
std::string text_of(const capie::engine::MsvcString& value) {
    const char* bytes = value.capacity <= 15 ? reinterpret_cast<const char*>(value.data.small) : value.data.pointer;
    return {bytes, static_cast<std::size_t>(value.size)};
}

/// @brief Host buffer that stands in for a country's cosmetic tag string (address = country + 0x1488).
capie::engine::MsvcString fake_tag = capie::engine::borrowed_string("POL", 3);  // NOLINT: fake

void* fake_candidates(void* out, const void* tag, const void* cosmetic, void* group) {
    game.tags.push_back(text_of(*static_cast<const capie::engine::MsvcString*>(tag)));
    // The real cosmetic argument points into the country; only the address matters to the hook.
    const auto address = reinterpret_cast<uptr>(cosmetic);
    const bool field = (address & 0xFFFF) == 0x1488 && address >= 0x1000'0000 && address < 0x1300'0000;
    game.cosmetics.push_back(field ? std::string("<country field>")
                                   : text_of(*static_cast<const capie::engine::MsvcString*>(cosmetic)));
    game.groups.push_back(group);
    return out;
}

int fake_load(uptr /*atlas*/, uptr country, void* /*dst*/, int /*a4*/, int /*a5*/, int /*a6*/, int /*a7*/,
              void* /*a8*/) {
    ++game.loads;
    const uptr owner = game.cosmetic != 0 ? game.cosmetic : country;
    static_cast<void>(
        game.layer->candidates(nullptr, &fake_tag, reinterpret_cast<const void*>(owner + 0x1488), game.group));
    if (game.throw_load) {
        throw std::bad_alloc();  // a game allocation failing inside the load
    }
    if (game.statuses.empty()) {
        return 0;
    }
    const int status = game.statuses.front();
    game.statuses.erase(game.statuses.begin());
    return status;
}

std::uint64_t fake_atlas(uptr /*atlas*/, uptr /*gfx*/) {
    ++game.atlas_calls;
    return 7;
}

void fake_log(capie::core::logging::Channel /*channel*/, const char* line) {
    game.log.emplace_back(line);
}

constexpr uptr fake_image = 0x1'4000'0000;
constexpr uptr fake_state = 0x5000'0000;
constexpr uptr fake_list = 0x5100'0000;
constexpr uptr fake_atlas_object = 0x5800'0000;
constexpr uptr fake_keys = 0x5900'0000;

/**
 * @brief Writes an inline MSVC string.
 * @param mem Memory.
 * @param address Address.
 * @param text Text (at most 15 bytes).
 */
void put_string(FakeMemory& mem, uptr address, std::string_view text) {
    capie::engine::MsvcString value{};
    std::memcpy(value.data.small, text.data(), text.size());
    value.size = text.size();
    value.capacity = 15;
    mem.put(address, value);
}

/**
 * @brief Reads a slot key of the fake atlas.
 * @param mem Memory.
 * @param index Slot.
 * @return Key.
 */
std::uint64_t key_of(const FakeMemory& mem, std::int32_t index) {
    std::uint64_t value = 0;
    static_cast<void>(mem.read(fake_keys + (static_cast<uptr>(index) * 8), &value, 8));
    return value;
}

void hook_tests() {
    FakeMemory mem;
    flag::FlagLayer<FakeMemory> flags{mem};
    game = Game{};
    game.layer = &flags;
    int group_dummy = 0;
    game.group = &group_dummy;
    flags.originals() = {&fake_atlas, &fake_load, &fake_candidates};
    // Countries at list indexes 1 (POL, layer), 2 (GER, none), 3 (CAN, layer with a stale snapshot).
    constexpr uptr pol = 0x1000'0000;
    constexpr uptr ger = 0x1100'0000;
    constexpr uptr can = 0x1200'0000;
    map_country(mem, pol, 0x2000'0000);
    map_country(mem, ger, 0x2100'0000);
    map_country(mem, can, 0x2200'0000);
    mem.put(pol + 8, std::int32_t{1});
    mem.put(ger + 8, std::int32_t{2});
    mem.put(can + 8, std::int32_t{9});  // tag int not the list index: the scan finds it
    put_string(mem, pol + 0x1488, "POL_x");
    put_string(mem, ger + 0x1488, "");
    put_string(mem, can + 0x1488, "CAN_dominion");
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, flag_variables("POL_KINGDOM", "POL_x")) &&
              build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, {{"other", fp(1)}}) &&
              build_table(mem, 0x2200'0000, 0x3200'0000, 127, 16, flag_variables("ENG", "CAN_old")),
          "country tables build");
    mem.map(fake_image + 0x33DD6B0, 8);
    mem.put(fake_image + 0x33DD6B0, fake_state);
    mem.map(fake_state, 0x400);
    mem.put(fake_state + 0x310, fake_list);
    mem.put(fake_state + 0x31C, std::int32_t{4});
    mem.map(fake_list, 32);
    mem.put(fake_list + 8, pol);
    mem.put(fake_list + 16, ger);
    mem.put(fake_list + 24, can);
    mem.map(fake_atlas_object, 0x100);
    mem.put(fake_atlas_object + 0xC0, fake_keys);
    mem.put(fake_atlas_object + 0xCC, std::int32_t{4});
    mem.map(fake_keys, 32);
    for (std::int32_t i = 0; i < 4; ++i) {
        mem.put(fake_keys + (static_cast<uptr>(i) * 8), std::uint64_t{100} + static_cast<std::uint64_t>(i));
    }

    // Inactive: everything passes through, nothing cached.
    check(!flags.update(pol) && flags.shown(pol).empty(), "inactive: no cache");
    check(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr) == 0 && game.loads == 1 &&
              game.tags.back() == "POL",
          "inactive: the game's candidates");
    check(flags.atlas_update(fake_atlas_object, 0) == 7 && mem.writes == 0, "inactive: atlas untouched");

    flags.activate(fake_image, &fake_log);
    check(flags.enabled(), "active");
    check(flags.update(pol) && flags.shown(pol) == "POL_KINGDOM", "update: POL shows its layer");
    check(!flags.update(ger) && flags.shown(ger).empty(), "GER: no layer");
    check(!flags.update(can) && flags.shown(can).empty(), "CAN: stale snapshot hides the layer");

    // Atlas: POL's slot key is forced once; GER and CAN untouched.
    check(flags.atlas_update(fake_atlas_object, 0) == 7 && key_of(mem, 1) == flag::forced_key &&
              key_of(mem, 2) == 102 && key_of(mem, 3) == 103,
          "changed slot key forced, others untouched");
    mem.put(fake_keys + 8, std::uint64_t{55});
    const auto writes = mem.writes;
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    check(mem.writes == writes && key_of(mem, 1) == 55, "same generation: no second write");
    // Only GER changes: GER's slot is forced, POL's unchanged slot keeps its key.
    check(build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, flag_variables("GER_x", nullptr)), "GER gets a layer");
    check(flags.update(ger), "GER shows its layer");
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    check(key_of(mem, 2) == flag::forced_key && key_of(mem, 1) == 55, "only the changed country's slot is forced");
    check(build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, {{"other", fp(1)}}) && !flags.update(ger),
          "GER back without a layer");
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    mem.put(fake_keys + 16, std::uint64_t{102});

    // Load of POL: the only candidate is the stem, no cosmetic, no group.
    game.tags.clear();
    game.cosmetics.clear();
    game.groups.clear();
    game.loads = 0;
    check(flags.load_flag(fake_atlas_object, pol, nullptr, 1, 2, 3, 4, nullptr) == 0 && game.loads == 1 &&
              game.tags == std::vector<std::string>{"POL_KINGDOM"} && game.cosmetics == std::vector<std::string>{""} &&
              game.groups == std::vector<void*>{nullptr},
          "layer: one load with the stem only");
    // Missing file: second load with the game's own candidates.
    game.statuses = {1, 0};
    game.tags.clear();
    check(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr) == 0 && game.loads == 3 &&
              game.tags == std::vector<std::string>{"POL_KINGDOM", "POL"} && game.groups.back() == &group_dummy,
          "missing file: the game's flag on the second load");
    game.statuses = {5};
    check(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr) == 5 && game.loads == 4,
          "status 5 is success");
    // GER: no layer, one load with the game's arguments.
    game.tags.clear();
    check(flags.load_flag(fake_atlas_object, ger, nullptr, 0, 0, 0, 0, nullptr) == 0 &&
              game.tags == std::vector<std::string>{"POL"},
          "no layer: arguments unchanged");
    // A candidate call outside a load, or with another country's cosmetic field, is untouched.
    game.tags.clear();
    static_cast<void>(flags.candidates(nullptr, &fake_tag, reinterpret_cast<const void*>(pol + 0x1488), nullptr));
    check(game.tags == std::vector<std::string>{"POL"}, "candidates outside a load: unchanged");
    // Scan N6: an exception of the game's load passes through, and the load frame is restored.
    game.tags.clear();
    game.throw_load = true;
    bool caught = false;
    try {
        static_cast<void>(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr));
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    game.throw_load = false;
    check(caught && game.tags == std::vector<std::string>{"POL_KINGDOM"}, "N6: the load's exception reaches the caller");
    check(!flags.in_load(), "N6: no load frame is left set after the exception");
    game.tags.clear();
    static_cast<void>(flags.candidates(nullptr, &fake_tag, reinterpret_cast<const void*>(pol + 0x1488), nullptr));
    check(game.tags == std::vector<std::string>{"POL"}, "N6: the load frame was restored after the exception");
    game.tags.clear();
    game.cosmetic = ger;
    static_cast<void>(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr));
    game.cosmetic = 0;
    check(game.tags == std::vector<std::string>{"POL"} && game.cosmetics.back() == "<country field>" &&
              game.groups.back() == &group_dummy,
          "candidates for another country's cosmetic field inside POL's load: unchanged");

    // A slot that no longer holds the country (another game loaded): no layer.
    mem.put(fake_list + 8, ger);
    game.tags.clear();
    static_cast<void>(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr));
    check(game.tags == std::vector<std::string>{"POL"}, "list slot changed: the game's flag");
    mem.put(fake_list + 8, pol);

    // Cosmetic tag change: hidden at once (UpdateNames runs on the change), slot forced again.
    put_string(mem, pol + 0x1488, "POL_new");
    check(!flags.update(pol) && flags.shown(pol).empty(), "cosmetic change hides the layer");
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    check(key_of(mem, 1) == flag::forced_key, "hidden layer reloads the slot");
    // Commit after a new choice with the new snapshot.
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, flag_variables("POL_fascism", "POL_new")),
          "POL chooses again");
    const auto stats = flags.refresh_all();
    check(stats.list_ok && stats.countries == 3 && stats.shown == 1 && flags.shown(pol) == "POL_fascism",
          "commit refresh");
    // Bounds: a slot index beyond the key count is never written.
    mem.put(fake_atlas_object + 0xCC, std::int32_t{1});
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, flag_variables("POL", nullptr)), "POL changes again");
    static_cast<void>(flags.refresh_all());
    const auto before = mem.writes;
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    check(mem.writes == before, "slot beyond the key count: no write");
    mem.put(fake_atlas_object + 0xCC, std::int32_t{4});
    // A read-only key array: the write fails, nothing crashes.
    mem.read_only.insert(fake_keys);
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, flag_variables("POL_x", nullptr)), "POL changes");
    static_cast<void>(flags.refresh_all());
    static_cast<void>(flags.atlas_update(fake_atlas_object, 0));
    check(game.atlas_calls > 0, "read-only keys: the original still runs");
    mem.read_only.clear();

    // Frames are per thread: a load on another thread does not see this thread's frame.
    game.tags.clear();
    std::thread other([&] {
        static_cast<void>(flags.candidates(nullptr, &fake_tag, reinterpret_cast<const void*>(pol + 0x1488), nullptr));
    });
    other.join();
    check(game.tags == std::vector<std::string>{"POL"}, "no frame leaks across threads");

    flags.deactivate();
    game.tags.clear();
    static_cast<void>(flags.load_flag(fake_atlas_object, pol, nullptr, 0, 0, 0, 0, nullptr));
    check(game.tags == std::vector<std::string>{"POL"}, "deactivated: the game's flag");
    bool lifecycle = false;
    for (const auto& line : game.log) {
        lifecycle = lifecycle || line.starts_with("CAPIE_FLAG commit list_ok=1");
    }
    check(lifecycle, "commit summary logged");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        codec_tests();
        decode_tests();
        gate_tests();
        hook_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("flag layer tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
