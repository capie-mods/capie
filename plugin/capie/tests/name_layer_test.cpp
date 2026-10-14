/**
 * @file name_layer_test.cpp
 * @brief Offline tests of the country name layer (API level 4): variable lookup, decoding and hook bodies.
 *
 * A fake memory holds country objects, variable containers built with the engine's own
 * insertion rule (FNV-1a bucket, robin-hood probe distances, extra slots past the mask), the
 * game-state country list and a `CSetVariableEffect` with its variable name. The real hook
 * bodies (`NameLayer<FakeMemory>`) run against fake originals that behave like the game:
 * the fake `UpdateNames` calls `BuildNameKey` through the hook, the fake `BuildNameKey` records
 * the tag it received. No game process, no EXE file, no MinHook.
 *
 * Covered: FNV-1a values; lookups with collisions, heap keys, empty slots and probe ends;
 * valid layers (ASCII, Polish, extended Latin); every invalid shape (version, length, code,
 * fractions, missing elements, RN1 guard); unreadable memory; priority (layer replaces the
 * tag, no layer passes the cosmetic tag through); the commit refresh (only changed countries,
 * removal of a layer, other variable names, bad country lists); log lines carry no name text.
 */
#include "../src/features/country/name_layer_hooks.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace layer = capie::country::layer;
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
    std::map<uptr, std::vector<unsigned char>> regions;  ///< Start address -> bytes.

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
 * @brief Codes of a UTF-32 text in the extended alphabet (0 for a character outside it).
 * @param text Text.
 * @return Codes.
 */
std::vector<std::int64_t> codes_of(std::u32string_view text) {
    std::vector<std::int64_t> codes;
    for (const char32_t c : text) {
        const auto at = capie::editor::extended_alphabet.find(c);
        codes.push_back(at == std::u32string_view::npos ? 0 : static_cast<std::int64_t>(at) + 1);
    }
    return codes;
}

/**
 * @brief Variables of a layer as a mod stores them.
 * @param name Name text.
 * @param adjective Adjective text.
 * @param version Raw `capie_cn_ver` (fixed point).
 * @return Variables.
 */
std::vector<std::pair<std::string, std::int64_t>> layer_variables(std::u32string_view name,
                                                                  std::u32string_view adjective,
                                                                  std::int64_t version = fp(1)) {
    std::vector<std::pair<std::string, std::int64_t>> out{{"capie_cn_ver", version}};
    const auto add = [&out](const std::string& array, std::u32string_view text) {
        const auto codes = codes_of(text);
        out.emplace_back(array + "^num", fp(static_cast<std::int64_t>(codes.size())));
        for (std::size_t i = 0; i < codes.size(); ++i) {
            out.emplace_back(array + "^" + std::to_string(i), fp(codes[i]));
        }
    };
    add("capie_cn_n", name);
    add("capie_cn_a", adjective);
    // Unrelated variables a country has anyway, including one longer than the inline capacity.
    out.emplace_back("rn_cname^0", fp(42));
    out.emplace_back("rn_cname_len", fp(1));
    out.emplace_back("some_mod_counter_with_a_long_name", fp(7));
    return out;
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
 * @brief Replaces one variable in a list.
 * @param vars Variables.
 * @param name Name.
 * @param value New raw value.
 * @return The list.
 */
std::vector<std::pair<std::string, std::int64_t>> with(std::vector<std::pair<std::string, std::int64_t>> vars,
                                                       const std::string& name, std::int64_t value) {
    for (auto& [key, raw] : vars) {
        if (key == name) {
            raw = value;
        }
    }
    return vars;
}

/**
 * @brief Removes one variable from a list.
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
 * @brief Reads the layer of a fresh country with the given variables.
 * @param vars Variables.
 * @param mask Bucket mask.
 * @return Layer.
 */
layer::Layer layer_of(const std::vector<std::pair<std::string, std::int64_t>>& vars, std::int32_t mask = 127) {
    FakeMemory mem;
    map_country(mem, 0x1000'0000, 0x2000'0000);
    if (!build_table(mem, 0x2000'0000, 0x3000'0000, mask, 16, vars)) {
        check(false, "table build");
    }
    return layer::read_layer(mem, 0x1000'0000);
}

void hash_tests() {
    check(layer::fnv1a("") == 0x811C9DC5U, "FNV-1a of the empty string is the basis");
    check(layer::fnv1a("a") == 0xE40C292CU, "FNV-1a of 'a'");
    check(layer::fnv1a("foobar") == 0xBF9CF968U, "FNV-1a of 'foobar'");
}

void lookup_tests() {
    // A tiny table forces collisions and long probes.
    FakeMemory mem;
    std::vector<std::pair<std::string, std::int64_t>> vars;
    vars.reserve(13);
    for (int i = 0; i < 12; ++i) {
        vars.emplace_back("v" + std::to_string(i), fp(i + 100));
    }
    vars.emplace_back("a_name_longer_than_15", fp(5));
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 15, 12, vars), "small table builds");
    layer::Table table{};
    check(layer::read_table(mem, 0x2000'0000, table) && table.slots == 28, "table read, 16 + 12 slots");
    bool all = true;
    for (const auto& [name, value] : vars) {
        std::int64_t found = 0;
        all = all && layer::find_variable(mem, table, name, found) == layer::Lookup::found && found == value;
    }
    check(all, "every variable found with its value (collisions, heap key)");
    std::int64_t value = 0;
    check(layer::find_variable(mem, table, "v12", value) == layer::Lookup::absent, "missing variable is absent");
    check(layer::find_variable(mem, table, "", value) == layer::Lookup::absent, "empty name is absent");
    check(layer::find_variable(mem, table, std::string(33, 'v'), value) == layer::Lookup::absent,
          "name over the lookup bound is absent");
    // A dense table: a missing name stops at the first entry whose probe distance is smaller
    // than the step (BC4960); it never reports a wrong variable. Absent and unreadable both mean
    // "no layer", so the exact stop point is not observable for display.
    {
        FakeMemory full;
        std::vector<std::pair<std::string, std::int64_t>> dense;
        dense.reserve(8);
        for (int i = 0; i < 8; ++i) {
            dense.emplace_back("k" + std::to_string(i), fp(i + 1));
        }
        check(build_table(full, 0x2000'0000, 0x3000'0000, 3, 4, dense), "full table builds (4 + 4 slots)");
        layer::Table full_table{};
        check(layer::read_table(full, 0x2000'0000, full_table) && full_table.slots == 8, "full table read");
        bool never_found = true;
        for (int i = 0; i < 40; ++i) {
            never_found = never_found && layer::find_variable(full, full_table, "m" + std::to_string(i), value) !=
                                             layer::Lookup::found;
        }
        check(never_found, "missing names in a full table are never found");
        bool all_found = true;
        for (const auto& [name, raw] : dense) {
            all_found = all_found && layer::find_variable(full, full_table, name, value) == layer::Lookup::found &&
                        value == raw;
        }
        check(all_found, "every name of a full table is found");
    }
    // Empty container: entries == 0.
    FakeMemory empty;
    empty.map(0x2000'0000, 0x40);
    check(layer::read_table(empty, 0x2000'0000, table) &&
              layer::find_variable(empty, table, "v1", value) == layer::Lookup::absent,
          "no entries: absent");
    // Implausible masks.
    FakeMemory bad;
    bad.map(0x2000'0000, 0x40);
    bad.put(0x2000'0018, uptr{0x3000'0000});
    bad.put(0x2000'0024, std::int32_t{-1});
    check(!layer::read_table(bad, 0x2000'0000, table), "negative mask refused");
    bad.put(0x2000'0024, std::int32_t{6});
    check(!layer::read_table(bad, 0x2000'0000, table), "mask that is not 2^n-1 refused");
    bad.put(0x2000'0024, std::int32_t{0x7FFFFFF});
    check(!layer::read_table(bad, 0x2000'0000, table), "huge mask refused");
    bad.put(0x2000'0024, std::int32_t{15});
    check(layer::read_table(bad, 0x2000'0000, table) &&
              layer::find_variable(bad, table, "v1", value) == layer::Lookup::unreadable,
          "unmapped entries: unreadable");
    // A heap key whose bytes are unmapped.
    FakeMemory broken;
    check(build_table(broken, 0x2000'0000, 0x3000'0000, 15, 4, {{"a_name_longer_than_15", fp(1)}}),
          "heap key table builds");
    for (auto it = broken.regions.begin(); it != broken.regions.end();) {
        it = it->first >= 0x7000'0000 ? broken.regions.erase(it) : std::next(it);
    }
    check(layer::read_table(broken, 0x2000'0000, table) &&
              layer::find_variable(broken, table, "a_name_longer_than_15", value) == layer::Lookup::unreadable,
          "unreadable heap key: unreadable");
}

void decode_tests() {
    auto vars = layer_variables(U"Kanadia", U"kanadyjski");
    auto result = layer_of(vars);
    check(result.status == layer::Status::valid && result.view() == "RN1|Kanadia|kanadyjski|" &&
              result.name_points == 7 && result.adjective_points == 10,
          "valid ASCII layer");
    result = layer_of(layer_variables(U"Rzeczpospolita Łódzka", U"łódzki"));
    check(result.status == layer::Status::valid && result.view() ==
                                                       "RN1|Rzeczpospolita \xC5\x81\xC3\xB3"
                                                       "dzka|\xC5\x82\xC3\xB3"
                                                       "dzki|",
          "Polish letters as UTF-8");
    result = layer_of(layer_variables(U"Česká Ŕ", U"český"));
    check(result.status == layer::Status::valid && result.view().starts_with("RN1|\xC4\x8C"
                                                                             "esk\xC3\xA1 \xC5\x94|"),
          "extended Latin letters");
    result = layer_of(layer_variables(U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef", U"x"));
    check(result.status == layer::Status::valid && result.name_points == 32, "32 code points");
    result = layer_of(layer_variables(U"Kanadia", U"kanadyjski"), 1023);
    check(result.status == layer::Status::valid, "large table");

    check(layer_of(without(vars, "capie_cn_ver")).status == layer::Status::absent, "no version: absent");
    check(layer_of({{"other", fp(1)}}).status == layer::Status::absent, "no layer variables: absent");
    {
        FakeMemory mem;
        map_country(mem, 0x1000'0000, 0);
        check(layer::read_layer(mem, 0x1000'0000).status == layer::Status::absent, "no container: absent");
        check(layer::read_layer(mem, 0x1100'0000).status == layer::Status::unreadable, "unmapped country");
        mem.put(0x1000'0000 + 0x218, uptr{0x2000'0000});
        check(layer::read_layer(mem, 0x1000'0000).status == layer::Status::unreadable, "unmapped container");
        // Container readable, entry array not: the version lookup is unreadable, not absent.
        mem.map(0x2000'0000, 0x40);
        mem.put(0x2000'0018, uptr{0x3000'0000});
        mem.put(0x2000'0024, std::int32_t{127});
        check(layer::read_layer(mem, 0x1000'0000).status == layer::Status::unreadable, "unmapped entries");
        // Entries readable, one element unreadable: the layer is unreadable.
        check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, layer_variables(U"Kanadia", U"k")),
              "table for the partial case");
        check(layer::read_layer(mem, 0x1000'0000).status == layer::Status::valid, "baseline valid");
    }
    const std::pair<const char*, std::vector<std::pair<std::string, std::int64_t>>> invalid[] = {
        {"version 2", with(vars, "capie_cn_ver", fp(2))},
        {"version 0", with(vars, "capie_cn_ver", 0)},
        {"version fraction", with(vars, "capie_cn_ver", fp(1) + 1)},
        {"name count missing", without(vars, "capie_cn_n^num")},
        {"name count 0", with(vars, "capie_cn_n^num", 0)},
        {"name count 33", with(vars, "capie_cn_n^num", fp(33))},
        {"name count fraction", with(vars, "capie_cn_n^num", fp(7) + 50000)},
        {"name count larger than the elements", with(vars, "capie_cn_n^num", fp(8))},
        {"code 0", with(vars, "capie_cn_n^3", 0)},
        {"code 265", with(vars, "capie_cn_n^3", fp(265))},
        {"code negative", with(vars, "capie_cn_n^3", fp(-1))},
        {"code fraction", with(vars, "capie_cn_n^3", fp(3) + 1)},
        {"element missing", without(vars, "capie_cn_n^6")},
        {"adjective missing", without(without(vars, "capie_cn_a^num"), "capie_cn_a^0")},
        {"adjective code missing", without(vars, "capie_cn_a^9")},
    };
    for (const auto& [label, bad] : invalid) {
        const auto got = layer_of(bad);
        check(got.status == layer::Status::invalid && got.view().empty(), std::string("invalid: ") + label);
    }
    // Leading space is allowed by the RN1 guard; a lone space is one valid code point.
    check(layer_of(layer_variables(U" ", U"x")).status == layer::Status::valid, "space-only name passes the guard");
    // Elements beyond ^num are ignored.
    auto extra = vars;
    extra.emplace_back("capie_cn_n^7", fp(1));
    check(layer_of(extra).view() == "RN1|Kanadia|kanadyjski|", "element past ^num ignored");
}

// ---- Hook bodies ------------------------------------------------------------------------------------

/// @brief Shared state of the fake game functions.
struct Game {
    layer::NameLayer<FakeMemory>* layer{};  ///< Layer under test.
    std::vector<std::string> tags;          ///< Tag strings BuildNameKey received.
    std::vector<uptr> updated;              ///< Countries the original UpdateNames ran for.
    std::vector<uptr> set_calls;            ///< Effects the original set_variable ran for.
    capie::engine::MsvcString cosmetic{};   ///< Tag the fake UpdateNames passes.
    std::vector<std::string> log;           ///< Log lines.
    int faction_originals{};                ///< Calls of the original CFaction::GetName.
    std::vector<std::string> copied;        ///< Texts the fake string copy received.
};
Game game;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake game state

/**
 * @brief Text of a host MSVC string (what BuildNameKey would copy).
 * @param value String object.
 * @return Text.
 */
std::string text_of(const capie::engine::MsvcString& value) {
    const char* bytes = value.capacity <= 15 ? reinterpret_cast<const char*>(value.data.small) : value.data.pointer;
    return {bytes, static_cast<std::size_t>(value.size)};
}

/**
 * @brief Fake BuildNameKey: records the tag text.
 * @param out Returned unchanged.
 * @param tag Tag string.
 * @return `out`.
 */
void* fake_build(void* out, const void* tag, void* /*a3*/, void* /*a4*/, void* /*a5*/, void* /*a6*/, void* /*a7*/,
                 char /*a8*/) {
    game.tags.push_back(text_of(*static_cast<const capie::engine::MsvcString*>(tag)));
    return out;
}

/**
 * @brief Fake UpdateNames: asks for the three keys through the hooked BuildNameKey.
 * @param country Country.
 * @return 1.
 */
std::uint64_t fake_update(uptr country) {
    game.updated.push_back(country);
    for (int key = 0; key < 3; ++key) {
        static_cast<void>(
            game.layer->build_name_key(nullptr, &game.cosmetic, nullptr, nullptr, nullptr, nullptr, nullptr, 1));
    }
    return 1;
}

/**
 * @brief Fake set_variable original.
 * @param effect Effect.
 * @param context Context (unused).
 */
void fake_set(uptr effect, uptr /*context*/) {
    game.set_calls.push_back(effect);
}

/**
 * @brief Fake CFaction::GetName original.
 * @param faction Faction (unused).
 * @param out Returned unchanged.
 * @return `out`.
 */
void* fake_faction_name(uptr /*faction*/, void* out) {
    ++game.faction_originals;
    return out;
}

/**
 * @brief Fake game string copy: records the source text.
 * @param out Returned unchanged.
 * @param source Source string.
 * @return `out`.
 */
void* fake_copy(void* out, const void* source) {
    game.copied.push_back(text_of(*static_cast<const capie::engine::MsvcString*>(source)));
    return out;
}

/**
 * @brief Log sink.
 * @param channel Channel (unused).
 * @param line Line.
 */
void fake_log(capie::core::logging::Channel /*channel*/, const char* line) {
    game.log.emplace_back(line);
}

constexpr uptr fake_image = 0x1'4000'0000;
constexpr uptr fake_state = 0x5000'0000;
constexpr uptr fake_list = 0x5100'0000;
constexpr uptr effect_commit = 0x6000'0000;
constexpr uptr effect_other = 0x6100'0000;

/**
 * @brief Writes an MSVC string (inline) at an address.
 * @param mem Fake memory.
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

void hook_tests() {
    FakeMemory mem;
    layer::NameLayer<FakeMemory> name_layer{mem};
    game = Game{};
    game.layer = &name_layer;
    game.cosmetic = capie::engine::borrowed_string("CAN_dominion", 12);
    name_layer.originals() = {&fake_update, &fake_build, &fake_set, &fake_faction_name};

    // Three countries: A with a layer, B without, C with an invalid layer.
    constexpr uptr country_a = 0x1000'0000;
    constexpr uptr country_b = 0x1100'0000;
    constexpr uptr country_c = 0x1200'0000;
    map_country(mem, country_a, 0x2000'0000);
    map_country(mem, country_b, 0x2100'0000);
    map_country(mem, country_c, 0x2200'0000);
    const auto vars_a = layer_variables(U"Kanadia", U"kanadyjski");
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, vars_a) &&
              build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, {{"other", fp(1)}}) &&
              build_table(mem, 0x2200'0000, 0x3200'0000, 127, 16,
                          with(layer_variables(U"X", U"y"), "capie_cn_ver", fp(3))),
          "country tables build");
    mem.map(fake_image + 0x332F260, 8);
    mem.put(fake_image + 0x332F260, fake_state);
    mem.map(fake_state, 0x400);
    mem.put(fake_state + 0x310, fake_list);
    mem.put(fake_state + 0x31C, std::int32_t{4});
    mem.map(fake_list, std::size_t{32});
    mem.put(fake_list + 0, country_a);
    mem.put(fake_list + 8, uptr{0});  // an empty slot in the country list
    mem.put(fake_list + 16, country_b);
    mem.put(fake_list + 24, country_c);
    mem.map(effect_commit, 0x100);
    put_string(mem, effect_commit + 0xC8, "capie_cn_rev");
    mem.map(effect_other, 0x100);
    put_string(mem, effect_other + 0xC8, "capie_cn_ver");

    // Inactive: everything passes through.
    static_cast<void>(name_layer.update_names(country_a));
    check(game.tags.size() == 3 && game.tags[0] == "CAN_dominion", "inactive: cosmetic tag passes through");
    name_layer.set_variable(effect_commit, 0);
    check(game.set_calls.size() == 1 && game.updated.size() == 1, "inactive: commit refreshes nothing");

    name_layer.activate(fake_image, &fake_log, nullptr);
    check(!name_layer.enabled(), "no string copy: layers stay off");
    name_layer.activate(fake_image, &fake_log, &fake_copy);
    check(name_layer.enabled(), "active after activate");
    game.tags.clear();
    game.updated.clear();
    static_cast<void>(name_layer.update_names(country_a));
    check(game.updated == std::vector<uptr>{country_a} && game.tags.size() == 3 &&
              game.tags[0] == "RN1|Kanadia|kanadyjski|" && game.tags[2] == "RN1|Kanadia|kanadyjski|",
          "layer replaces the tag for every key of the call");
    check(name_layer.shown_key(country_a) == "RN1|Kanadia|kanadyjski|", "shown key recorded");
    game.tags.clear();
    static_cast<void>(name_layer.update_names(country_b));
    check(game.tags.size() == 3 && game.tags[0] == "CAN_dominion" && name_layer.shown_key(country_b).empty(),
          "no layer: the cosmetic tag passes through");
    game.tags.clear();
    static_cast<void>(name_layer.update_names(country_c));
    check(game.tags.size() == 3 && game.tags[0] == "CAN_dominion", "invalid layer: the cosmetic tag passes through");
    game.tags.clear();
    static_cast<void>(
        name_layer.build_name_key(nullptr, &game.cosmetic, nullptr, nullptr, nullptr, nullptr, nullptr, 0));
    check(game.tags.size() == 1 && game.tags[0] == "CAN_dominion",
          "BuildNameKey outside UpdateNames (set_cosmetic_tag tooltip) is unchanged");

    // Commit: A already shows its layer, B and C show none -> nothing to refresh.
    game.updated.clear();
    name_layer.set_variable(effect_commit, 0);
    check(game.updated.empty(), "commit without changes refreshes nothing");
    // A new layer on B -> only B is refreshed.
    check(build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, layer_variables(U"Nowa", U"nowy")), "B gets a layer");
    name_layer.set_variable(effect_commit, 0);
    check(game.updated == std::vector<uptr>{country_b} && name_layer.shown_key(country_b) == "RN1|Nowa|nowy|",
          "commit refreshes only the changed country");
    // Another set_variable does not refresh.
    check(build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, {{"other", fp(1)}}), "B loses the layer");
    game.updated.clear();
    name_layer.set_variable(effect_other, 0);
    check(game.updated.empty() && game.set_calls.back() == effect_other, "other variable: original only");
    // The reset is committed -> B is refreshed and shows no layer again.
    name_layer.set_variable(effect_commit, 0);
    check(game.updated == std::vector<uptr>{country_b} && name_layer.shown_key(country_b).empty(),
          "removed layer is refreshed");
    // A changed text -> refreshed.
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, layer_variables(U"Kanadia", U"kanadyjska")),
          "A changes the adjective");
    game.updated.clear();
    const auto stats = name_layer.refresh_changed();
    check(stats.list_ok && stats.countries == 3 && stats.layers == 1 && stats.invalid == 1 && stats.refreshed == 1 &&
              game.updated == std::vector<uptr>{country_a},
          "refresh counters");

    // Bad country lists refresh nothing.
    mem.put(fake_state + 0x31C, std::int32_t{-1});
    check(!name_layer.refresh_changed().list_ok, "negative count refused");
    mem.put(fake_state + 0x31C, std::int32_t{layer::max_countries + 1});
    check(!name_layer.refresh_changed().list_ok, "huge count refused");
    mem.put(fake_state + 0x31C, std::int32_t{6});  // past the mapped list: unreadable slots are skipped
    check(name_layer.refresh_changed().list_ok, "unreadable slots skipped");
    mem.put(fake_image + 0x332F260, uptr{0});
    check(!name_layer.refresh_changed().list_ok, "no game state refused");

    // Nested UpdateNames restores the outer frame.
    game.tags.clear();
    static_cast<void>(name_layer.update_names(country_a));
    static_cast<void>(
        name_layer.build_name_key(nullptr, &game.cosmetic, nullptr, nullptr, nullptr, nullptr, nullptr, 0));
    check(game.tags.size() == 4 && game.tags[3] == "CAN_dominion", "frame cleared after UpdateNames returns");

    name_layer.deactivate();
    game.tags.clear();
    static_cast<void>(name_layer.update_names(country_a));
    check(game.tags.size() == 3 && game.tags[0] == "CAN_dominion", "deactivated: pass-through");

    bool no_text = true;
    for (const auto& line : game.log) {
        no_text = no_text && line.find("Kanadia") == std::string::npos && line.find("Nowa") == std::string::npos &&
                  line.find("kanady") == std::string::npos;
    }
    check(no_text, "log lines never contain a name");
    bool has_commit_line = false;
    for (const auto& line : game.log) {
        has_commit_line = has_commit_line || line.starts_with("CAPIE_NAME commit list_ok=1");
    }
    check(has_commit_line, "commit summary logged");
}

/**
 * @brief Variables of a faction layer as a mod stores them on the leader.
 * @param name Faction name.
 * @param version Raw `capie_fn_ver`.
 * @return Variables (with a country name layer next to it, which must not mix in).
 */
std::vector<std::pair<std::string, std::int64_t>> faction_variables(std::u32string_view name,
                                                                    std::int64_t version = fp(1)) {
    auto out = layer_variables(U"Kanadia", U"kanadyjski");
    out.emplace_back("capie_fn_ver", version);
    const auto codes = codes_of(name);
    out.emplace_back("capie_fn_n^num", fp(static_cast<std::int64_t>(codes.size())));
    for (std::size_t i = 0; i < codes.size(); ++i) {
        out.emplace_back("capie_fn_n^" + std::to_string(i), fp(codes[i]));
    }
    return out;
}

void faction_tests() {
    // Reader.
    const auto read = [](const std::vector<std::pair<std::string, std::int64_t>>& vars) {
        FakeMemory mem;
        map_country(mem, 0x1000'0000, 0x2000'0000);
        check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, vars), "faction table builds");
        return layer::read_faction_layer(mem, 0x1000'0000);
    };
    const auto got = read(faction_variables(U"Sojusz P\u00F3\u0142nocny"));
    check(got.status == layer::Status::valid && got.view() == "Sojusz P\xC3\xB3\xC5\x82nocny" && got.name_points == 15,
          "faction layer: plain UTF-8 text");
    check(read(layer_variables(U"Kanadia", U"k")).status == layer::Status::absent, "no capie_fn_ver: absent");
    check(read(faction_variables(U"X", fp(2))).status == layer::Status::invalid, "faction version 2: invalid");
    check(read(without(faction_variables(U"O\u015B"), "capie_fn_n^1")).status == layer::Status::invalid,
          "faction element missing: invalid");

    // Hook body.
    FakeMemory mem;
    layer::NameLayer<FakeMemory> name_layer{mem};
    game = Game{};
    game.layer = &name_layer;
    game.cosmetic = capie::engine::borrowed_string("GER", 3);
    name_layer.originals() = {&fake_update, &fake_build, &fake_set, &fake_faction_name};
    constexpr uptr leader = 0x1000'0000;
    constexpr uptr member = 0x1100'0000;
    constexpr uptr faction = 0x7800'0000;
    constexpr uptr members = 0x7900'0000;
    constexpr uptr effect_faction = 0x6200'0000;
    map_country(mem, leader, 0x2000'0000);
    map_country(mem, member, 0x2100'0000);
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, faction_variables(U"Sojusz P\u00F3\u0142nocny")) &&
              build_table(mem, 0x2100'0000, 0x3100'0000, 127, 16, {{"other", fp(1)}}),
          "faction country tables build");
    mem.map(faction, 0x600);
    mem.put(faction + 0x58, members);
    mem.put(faction + 0x64, std::int32_t{2});
    mem.map(members, 16);
    mem.put(members, leader);
    mem.put(members + 8, member);
    mem.map(fake_image + 0x332F260, 8);
    mem.put(fake_image + 0x332F260, fake_state);
    mem.map(fake_state, 0x400);
    mem.put(fake_state + 0x310, fake_list);
    mem.put(fake_state + 0x31C, std::int32_t{2});
    mem.map(fake_list, std::size_t{16});
    mem.put(fake_list, leader);
    mem.put(fake_list + 8, member);
    mem.map(effect_faction, 0x100);
    put_string(mem, effect_faction + 0xC8, "capie_fn_rev");
    name_layer.activate(fake_image, &fake_log, &fake_copy);

    capie::engine::MsvcString out{};
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.faction_originals == 1 && game.copied.empty(), "empty cache: the original answers");
    static_cast<void>(name_layer.update_names(leader));
    static_cast<void>(name_layer.update_names(member));
    check(name_layer.faction_key(leader) == "Sojusz P\xC3\xB3\xC5\x82nocny" && name_layer.faction_key(member).empty(),
          "UpdateNames fills the faction cache (also after a load)");
    check(name_layer.faction_name(faction, &out) == &out && game.faction_originals == 1 &&
              game.copied == std::vector<std::string>{"Sojusz P\xC3\xB3\xC5\x82nocny"},
          "leader layer: the text is copied, the original is not called");
    // Another leader: the layer stays with the old leader, the faction shows the game's name.
    mem.put(members, member);
    mem.put(members + 8, leader);
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.faction_originals == 2 && game.copied.size() == 1, "new leader without a layer: the original answers");
    mem.put(members, leader);
    mem.put(faction + 0x64, std::int32_t{0});
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.faction_originals == 3, "no members: the original answers");
    mem.put(faction + 0x64, std::int32_t{2});
    static_cast<void>(name_layer.faction_name(0x7A00'0000, &out));
    check(game.faction_originals == 4, "unreadable faction: the original answers");
    // Reset committed through capie_fn_rev: cache cleared, country names untouched.
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, layer_variables(U"Kanadia", U"kanadyjski")),
          "leader loses the faction layer");
    game.updated.clear();
    name_layer.set_variable(effect_faction, 0);
    check(name_layer.faction_key(leader).empty() && game.updated.empty(),
          "faction commit refreshes the faction cache only");
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.faction_originals == 5 && game.copied.size() == 1, "after the reset the game's name is back");
    // A new name committed.
    check(build_table(mem, 0x2000'0000, 0x3000'0000, 127, 16, faction_variables(U"O\u015B")), "leader gets a new name");
    name_layer.set_variable(effect_faction, 0);
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.copied.size() == 2 && game.copied.back() == "O\xC5\x9B", "faction commit shows the new name");
    const auto stats = name_layer.refresh_factions();
    check(stats.list_ok && stats.countries == 2 && stats.layers == 1, "faction refresh counters");
    name_layer.deactivate();
    static_cast<void>(name_layer.faction_name(faction, &out));
    check(game.faction_originals == 6, "deactivated: the original answers");
    bool no_text = true;
    bool commit_line = false;
    for (const auto& line : game.log) {
        no_text = no_text && line.find("Sojusz") == std::string::npos && line.find("O\xC5\x9B") == std::string::npos;
        commit_line = commit_line || line.starts_with("CAPIE_FACTION commit list_ok=1");
    }
    check(no_text && commit_line, "faction log lines: counts only");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        hash_tests();
        lookup_tests();
        decode_tests();
        hook_tests();
        faction_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("name layer tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
