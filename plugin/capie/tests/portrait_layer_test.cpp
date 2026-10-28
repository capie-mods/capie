/**
 * @file portrait_layer_test.cpp
 * @brief Offline tests of the leader portrait layer (API level 7): variables, leader chain, capabilities, hook.
 *
 * A fake memory holds countries with the game's ruling-leader chain (politics +0xF90, ruling
 * party +0xD0, leader roles +0x70 / count +0x7C, role +0x8 character), characters with the
 * `CCharacter` vtable, an id at +0xC and an inline variable container at +0xD8 built with the
 * engine's insertion rule (shared with name_layer_test.cpp), and the game-state country list.
 * The real hook body (`PortraitLayer<FakeMemory>`) runs against a fake `ResolvePortrait` (writes
 * the game's entry "GFX_game_portrait"), a fake validity check (a set of existing sprites) and a
 * fake string copy. Covered: decoding, the leader chain, the capability values, civilian only,
 * large and small, fail-closed on a missing sprite (the original runs), the object and id
 * check, leader change, cache rebuild on commit, and concurrent readers. No game, no EXE.
 */
#include "../src/features/country/capability.hpp"
#include "../src/features/country/portrait_layer_hooks.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace layer = capie::country::layer;
namespace portrait = capie::country::portrait;
namespace country = capie::country;
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
 * @param variables Name -> raw fixed-point value.
 * @return `false` when an insertion ran past the extra slots.
 */
bool build_table(FakeMemory& mem, uptr container, uptr entries,
                 const std::vector<std::pair<std::string, std::int64_t>>& variables) {
    constexpr std::int32_t mask = 127;
    constexpr std::uint8_t extra = 16;
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
            if (table[index].probe < distance) {
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
 * @brief Packs a sprite name three characters per element (the contract's rule, written independently).
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

using Vars = std::vector<std::pair<std::string, std::int64_t>>;

/**
 * @brief Appends one packed text.
 * @param out Variables.
 * @param length Length variable.
 * @param array Array name.
 * @param text Text.
 */
void add_packed(Vars& out, const std::string& length, const std::string& array, const std::string& text) {
    out.emplace_back(length, fp(static_cast<std::int64_t>(text.size())));
    const auto elements = pack(text);
    if (!elements.empty()) {
        out.emplace_back(array + "^num", fp(static_cast<std::int64_t>(elements.size())));
    }
    for (std::size_t i = 0; i < elements.size(); ++i) {
        out.emplace_back(array + "^" + std::to_string(i), fp(elements[i]));
    }
}

/**
 * @brief Variables of a portrait layer as a mod stores them.
 * @param large Large sprite name.
 * @param small Small sprite name; `nullptr` = no small variables.
 * @return Variables.
 */
Vars portrait_variables(const std::string& large, const char* small) {
    Vars out{{"capie_pt_ver", fp(1)}};
    add_packed(out, "capie_pt_len", "capie_pt_s", large);
    if (small != nullptr) {
        add_packed(out, "capie_pt_slen", "capie_pt_ss", small);
    }
    out.emplace_back("capie_cf_ver", fp(1));  // a flag layer on the same object must not mix in
    return out;
}

/**
 * @brief Replaces a variable.
 * @param vars Variables.
 * @param name Name.
 * @param value Raw value.
 * @return The list.
 */
Vars with(Vars vars, const std::string& name, std::int64_t value) {
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
Vars without(Vars vars, const std::string& name) {
    std::erase_if(vars, [&name](const auto& entry) { return entry.first == name; });
    return vars;
}

constexpr uptr fake_image = 0x1'4000'0000;
constexpr uptr fake_state = 0x5000'0000;
constexpr uptr fake_list = 0x5100'0000;

/**
 * @brief Maps a character with the CCharacter vtable, an id and (optionally) variables.
 * @param mem Memory.
 * @param character Address.
 * @param id Character id.
 * @param vars Variables (empty = an empty container).
 * @param entries Entry array address of its container.
 */
void map_character(FakeMemory& mem, uptr character, std::int32_t id, const Vars& vars, uptr entries) {
    mem.map(character, 0xD8);
    mem.put(character, fake_image + capie::core::rva::vt_character);
    mem.put(character + 0xC, id);
    check(build_table(mem, character + 0xD8, entries, vars), "character table builds");
}

/**
 * @brief Maps a country whose ruling party's first leader role names `character`.
 * @param mem Memory.
 * @param country Country address; politics, party, roles and role follow at +0x100000 steps.
 * @param character Ruling leader (0 = a party without leaders).
 */
void map_country(FakeMemory& mem, uptr country, uptr character) {
    const uptr politics = country + 0x10'0000;
    const uptr party = country + 0x20'0000;
    const uptr roles = country + 0x30'0000;
    const uptr role = country + 0x40'0000;
    mem.map(country, 0x1000);
    mem.put(country + 0xF90, politics);
    mem.map(politics, 0x100);
    mem.put(politics + 0xD0, party);
    mem.map(party, 0x100);
    mem.put(party + 0x70, roles);
    mem.put(party + 0x7C, std::int32_t{character != 0 ? 1 : 0});
    mem.map(roles, 0x10);
    mem.put(roles, role);
    mem.map(role, 0x20);
    mem.put(role + 8, character);
}

/**
 * @brief Points a country's first leader role at another character.
 * @param mem Memory.
 * @param country Country.
 * @param character New leader.
 */
void set_leader(FakeMemory& mem, uptr country, uptr character) {
    mem.put(country + 0x40'0000 + 8, character);
}

void decode_tests() {
    const auto read = [](const Vars& vars) {
        FakeMemory mem;
        map_character(mem, 0x1000'0000, 7, vars, 0x3000'0000);
        return portrait::read_portrait_layer(mem, 0x1000'0000);
    };
    auto got = read(portrait_variables("GFX_portrait_POL_ignacy_moscicki", "GFX_portrait_POL_ignacy_moscicki_small"));
    check(got.status == layer::Status::valid && got.large.view() == "GFX_portrait_POL_ignacy_moscicki" &&
              got.small.view() == "GFX_portrait_POL_ignacy_moscicki_small",
          "large and small read from the character's own container (+0xD8)");
    got = read(portrait_variables("GFX_a", nullptr));
    check(got.status == layer::Status::valid && got.large.view() == "GFX_a" && got.small.size == 0,
          "small absent: game's small portrait");
    got = read(portrait_variables("GFX_a", ""));
    check(got.status == layer::Status::valid && got.small.size == 0, "small length 0: game's small portrait");
    got = read(portrait_variables(std::string(63, 'Z'), std::string(63, '_').c_str()));
    check(got.status == layer::Status::valid && got.large.size == 63 && got.small.size == 63, "63 characters each");
    check(read({{"other", fp(1)}}).status == layer::Status::absent, "no capie_pt_ver: absent");
    check(read(portrait_variables("GFX_a", "GFX_b")).status == layer::Status::valid, "baseline valid");
    const auto base = portrait_variables("GFX_portrait_x", "GFX_portrait_x_small");
    const std::pair<const char*, Vars> invalid[] = {
        {"version 2", with(base, "capie_pt_ver", fp(2))},
        {"large missing", without(base, "capie_pt_len")},
        {"large length 0", with(base, "capie_pt_len", 0)},
        {"large length 64", with(base, "capie_pt_len", fp(64))},
        {"large ^num wrong", with(base, "capie_pt_s^num", fp(9))},
        {"large element missing", without(base, "capie_pt_s^1")},
        {"large element too big", with(base, "capie_pt_s^0", fp(262144))},
        {"large zero inside", with(base, "capie_pt_s^1", 0)},
        {"small length 64", with(base, "capie_pt_slen", fp(64))},
        {"small element missing", without(base, "capie_pt_ss^0")},
        {"small fraction", with(base, "capie_pt_ss^0", fp(1) + 3)},
    };
    for (const auto& [label, vars] : invalid) {
        const auto bad = read(vars);
        check(bad.status == layer::Status::invalid && bad.large.size == 0 && bad.small.size == 0,
              std::string("invalid: ") + label);
    }
    const FakeMemory empty;
    check(portrait::read_portrait_layer(empty, 0x1000'0000).status == layer::Status::unreadable,
          "unmapped character: unreadable");
    check(portrait::read_portrait_layer(empty, 0).status == layer::Status::unreadable, "null character: unreadable");
}

void leader_tests() {
    FakeMemory mem;
    map_character(mem, 0x1000'0000, 7, {}, 0x3000'0000);
    map_country(mem, 0x2000'0000, 0x1000'0000);
    uptr leader = 1;
    check(portrait::ruling_leader(mem, fake_image, 0x2000'0000, leader) && leader == 0x1000'0000,
          "chain: politics +F90, party +D0, roles +70 [0], role +8");
    map_country(mem, 0x2100'0000, 0);
    check(portrait::ruling_leader(mem, fake_image, 0x2100'0000, leader) && leader == 0, "party without leaders");
    mem.put(0x2100'0000 + 0x10'0000 + 0xD0, uptr{0});
    check(portrait::ruling_leader(mem, fake_image, 0x2100'0000, leader) && leader == 0, "no ruling party");
    map_country(mem, 0x2200'0000, 0x1000'0000);
    mem.put(0x1000'0000, fake_image + 0x1234);
    check(!portrait::ruling_leader(mem, fake_image, 0x2200'0000, leader) && leader == 0, "not a CCharacter: no leader");
    mem.put(0x1000'0000, fake_image + capie::core::rva::vt_character);
    mem.put(0x2200'0000 + 0x20'0000 + 0x7C, std::int32_t{-1});
    check(!portrait::ruling_leader(mem, fake_image, 0x2200'0000, leader), "negative leader count");
    check(!portrait::ruling_leader(mem, fake_image, 0x2900'0000, leader), "unmapped country");
}

void capability_tests() {
    check(country::capabilities_of(false, true, true) == country::country_capabilities_v1, "no names: v1");
    check(country::capabilities_of(true, false, false) == country::country_capabilities_v2, "names: v2");
    check(country::capabilities_of(true, true, false) == country::country_capabilities_v3, "names + flag: v3");
    const auto both = country::capabilities_of(true, true, true);
    const auto portrait_only = country::capabilities_of(true, false, true);
    check(both == 0x524E4403U && portrait_only == 0x524E4402U, "v4 values");
    check(country::names_active(both) && country::flag_active(both) && country::portrait_active(both),
          "v4 with flag: everything");
    check(country::names_active(portrait_only) && !country::flag_active(portrait_only) &&
              country::portrait_active(portrait_only),
          "v4 without flag");
    check(country::flag_active(country::country_capabilities_v3) &&
              !country::portrait_active(country::country_capabilities_v3),
          "v3: flag, no portrait");
    check(!country::names_active(country::country_capabilities_v1) && !country::names_active(0) &&
              !country::portrait_active(0x524E4400U) && !country::portrait_active(0x524E4407U) &&
              !country::names_active(0x524E4401U),
          "v1, 0 and undefined v4 values are refused");
}

// ---- Hook -----------------------------------------------------------------------------------

/// @brief Fake game state of the hook tests.
struct Game {
    std::set<std::string> sprites;  ///< Sprites that exist.
    std::set<std::string> heap;     ///< Storage of copied names (interned).
    int copies{};                   ///< Calls of the string copy.
    int resolves{};                 ///< Calls of the original ResolvePortrait.
    int valid_calls{};              ///< Calls of the validity check.
};
Game* game = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake game state

/**
 * @brief Text of a host MSVC string.
 * @param value String.
 * @return Text.
 */
std::string text_of(const capie::engine::MsvcString& value) {
    const char* bytes = value.capacity <= 15 ? reinterpret_cast<const char*>(value.data.small) : value.data.pointer;
    return {bytes, static_cast<std::size_t>(value.size)};
}

void* fake_copy(void* out, const void* source) {
    ++game->copies;
    const auto& stored = *game->heap.insert(text_of(*static_cast<const capie::engine::MsvcString*>(source))).first;
    *static_cast<capie::engine::MsvcString*>(out) = capie::engine::borrowed_string(stored.data(), stored.size());
    if (stored.size() <= 15) {
        static_cast<capie::engine::MsvcString*>(out)->capacity = 15;
    }
    return out;
}

bool fake_valid(const void* entry) {
    ++game->valid_calls;
    const auto* e = static_cast<const portrait::Entry*>(entry);
    return e->set != 0 && e->kind == 0 && game->sprites.contains(text_of(e->name));
}

void* fake_resolve(void* /*map*/, void* out, std::int32_t /*type*/, std::int32_t /*size*/, uptr /*character*/,
                   const void* /*fallback*/) {
    ++game->resolves;
    auto* entry = static_cast<portrait::Entry*>(out);
    entry->set = 1;
    entry->kind = 1;
    static const std::string path = "gfx/leaders/game_portrait.dds";
    entry->name = capie::engine::borrowed_string(path.data(), path.size());
    entry->handle = 0xABCD;
    return out;
}

/**
 * @brief Calls the hook and returns the name in the buffer.
 * @param layer Layer.
 * @param character Character.
 * @param type Type.
 * @param size Size.
 * @param entry Receives the buffer.
 * @return Name.
 */
std::string resolve_of(portrait::PortraitLayer<FakeMemory>& layer, uptr character, std::int32_t type, std::int32_t size,
                       portrait::Entry& entry) {
    entry = portrait::Entry{};
    void* const result = layer.resolve(nullptr, &entry, type, size, character, nullptr);
    return result == &entry ? text_of(entry.name) : std::string("<wrong return>");
}

void hook_tests() {
    FakeMemory mem;
    portrait::PortraitLayer<FakeMemory> layer{mem};
    Game state{};
    game = &state;
    game->sprites = {"GFX_portrait_GER_chosen", "GFX_portrait_GER_chosen_small", "GFX_portrait_ITA_other"};
    layer.originals() = {&fake_resolve, &fake_valid, &fake_copy};
    // POL (list 1) rules with character A (layer), GER (list 2) with B (no layer), ITA (list 3) with C
    // (layer with a sprite that does not exist).
    constexpr uptr pol = 0x2000'0000;
    constexpr uptr ger = 0x2100'0000;
    constexpr uptr ita = 0x2200'0000;
    constexpr uptr char_a = 0x1000'0000;
    constexpr uptr char_b = 0x1100'0000;
    constexpr uptr char_c = 0x1200'0000;
    constexpr uptr char_d = 0x1300'0000;
    map_character(mem, char_a, 11, portrait_variables("GFX_portrait_GER_chosen", "GFX_portrait_GER_chosen_small"),
                  0x3000'0000);
    map_character(mem, char_b, 12, {{"other", fp(1)}}, 0x3100'0000);
    map_character(mem, char_c, 13, portrait_variables("GFX_portrait_missing", "GFX_portrait_missing_small"),
                  0x3200'0000);
    map_character(mem, char_d, 14, portrait_variables("GFX_portrait_ITA_other", nullptr), 0x3300'0000);
    map_country(mem, pol, char_a);
    map_country(mem, ger, char_b);
    map_country(mem, ita, char_c);
    mem.map(fake_image + 0x33DD6B0, 8);
    mem.put(fake_image + 0x33DD6B0, fake_state);
    mem.map(fake_state, 0x400);
    mem.put(fake_state + 0x310, fake_list);
    mem.put(fake_state + 0x31C, std::int32_t{4});
    mem.map(fake_list, 32);
    mem.put(fake_list + 8, pol);
    mem.put(fake_list + 16, ger);
    mem.put(fake_list + 24, ita);

    portrait::Entry entry{};
    check(!layer.update(pol) && resolve_of(layer, char_a, 0, 1, entry) == "gfx/leaders/game_portrait.dds" &&
              game->resolves == 1,
          "inactive: the game's portrait");

    layer.activate(fake_image, nullptr);
    check(layer.enabled(), "active");
    check(layer.update(pol) && layer.shown(char_a, 1) == "GFX_portrait_GER_chosen", "update: POL's leader cached");
    check(!layer.update(ger) && layer.shown(char_b, 1).empty(), "GER's leader has no layer");
    check(layer.update(ita), "ITA's leader cached (its sprite is checked only when drawn)");

    game->resolves = 0;
    check(resolve_of(layer, char_a, 0, 1, entry) == "GFX_portrait_GER_chosen" && entry.set == 1 && entry.kind == 0 &&
              entry.handle == 0 && game->resolves == 0,
          "civilian large: chosen sprite, kind 0, no handle, original not called");
    check(resolve_of(layer, char_a, 0, 0, entry) == "GFX_portrait_GER_chosen_small" && game->resolves == 0,
          "civilian small: chosen small sprite");
    check(resolve_of(layer, char_a, 1, 1, entry) == "gfx/leaders/game_portrait.dds" && entry.kind == 1 &&
              entry.handle == 0xABCD && game->resolves == 1,
          "army portrait: the game's (U2)");
    check(resolve_of(layer, char_a, 2, 1, entry) == "gfx/leaders/game_portrait.dds" && game->resolves == 2,
          "navy portrait: the game's");
    check(resolve_of(layer, char_b, 0, 1, entry) == "gfx/leaders/game_portrait.dds" && game->resolves == 3,
          "leader without a layer: the game's");
    const auto valid_before = game->valid_calls;
    check(resolve_of(layer, char_c, 0, 1, entry) == "gfx/leaders/game_portrait.dds" && game->resolves == 4 &&
              game->valid_calls == valid_before + 1,
          "missing sprite: checked, then the game's portrait (not the unknown image)");
    check(resolve_of(layer, char_d, 0, 1, entry) == "gfx/leaders/game_portrait.dds" && game->resolves == 5,
          "a character that does not rule: the game's");
    const auto copies_before = game->copies;
    check(resolve_of(layer, 0, 0, 1, entry) == "gfx/leaders/game_portrait.dds" && game->copies == copies_before,
          "null character: the game's, nothing copied");

    // Object checks: a changed id or vtable (another object at the same address) shows the game's.
    mem.put(char_a + 0xC, std::int32_t{99});
    check(resolve_of(layer, char_a, 0, 1, entry) == "gfx/leaders/game_portrait.dds", "id changed: the game's");
    mem.put(char_a + 0xC, std::int32_t{11});
    mem.put(char_a, fake_image + 0x1234);
    check(resolve_of(layer, char_a, 0, 1, entry) == "gfx/leaders/game_portrait.dds", "vtable changed: the game's");
    mem.put(char_a, fake_image + capie::core::rva::vt_character);
    check(resolve_of(layer, char_a, 0, 1, entry) == "GFX_portrait_GER_chosen", "restored object: chosen again");

    // A large-only layer: the small size stays the game's.
    set_leader(mem, ger, char_d);
    check(layer.update(ger) && resolve_of(layer, char_d, 0, 1, entry) == "GFX_portrait_ITA_other" &&
              resolve_of(layer, char_d, 0, 0, entry) == "gfx/leaders/game_portrait.dds",
          "no small sprite: the game's small portrait");

    // Leader change through UpdateNames: the old leader is dropped.
    set_leader(mem, pol, char_b);
    check(!layer.update(pol) && layer.shown(char_a, 1).empty() &&
              resolve_of(layer, char_a, 0, 1, entry) == "gfx/leaders/game_portrait.dds",
          "leader changed: the previous leader shows the game's portrait");
    set_leader(mem, pol, char_a);

    // Commit: the cache is rebuilt from every country.
    auto stats = layer.refresh_all();
    check(stats.list_ok && stats.countries == 3 && stats.leaders == 3 && stats.layers == 3 &&
              layer.shown(char_a, 1) == "GFX_portrait_GER_chosen" && layer.shown(char_d, 0).empty(),
          "commit: every ruling leader read");
    check(build_table(mem, char_a + 0xD8, 0x3000'0000, {{"other", fp(1)}}), "reset A");
    set_leader(mem, ger, char_b);
    stats = layer.refresh_all();
    check(stats.layers == 1 && layer.shown(char_a, 1).empty() && layer.shown(char_d, 1).empty() &&
              layer.shown(char_c, 1) == "GFX_portrait_missing",
          "commit after a reset and a leader change: both dropped");
    check(build_table(mem, char_a + 0xD8, 0x3000'0000,
                      with(portrait_variables("GFX_portrait_GER_chosen", nullptr), "capie_pt_len", fp(64))),
          "A invalid");
    stats = layer.refresh_all();
    check(stats.invalid == 1 && layer.shown(char_a, 1).empty(), "invalid layer counted, not shown");

    // Readers on another thread while the logic thread rebuilds the cache.
    check(build_table(mem, char_a + 0xD8, 0x3000'0000,
                      portrait_variables("GFX_portrait_GER_chosen", "GFX_portrait_GER_chosen_small")),
          "A again");
    static_cast<void>(layer.refresh_all());
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::thread reader([&] {
        portrait::Entry local{};
        while (!stop.load()) {
            local = portrait::Entry{};
            static_cast<void>(layer.resolve(nullptr, &local, 0, 1, char_a, nullptr));
            const auto text = text_of(local.name);
            if (text != "GFX_portrait_GER_chosen" && text != "gfx/leaders/game_portrait.dds") {
                ++bad;
            }
        }
    });
    for (int i = 0; i < 200; ++i) {
        static_cast<void>(layer.refresh_all());
        static_cast<void>(layer.update(pol));
    }
    stop.store(true);
    reader.join();
    check(bad.load() == 0, "concurrent readers see a whole entry");

    layer.deactivate();
    game->resolves = 0;
    check(resolve_of(layer, char_a, 0, 1, entry) == "gfx/leaders/game_portrait.dds" && game->resolves == 1,
          "deactivated: the game's portrait");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        decode_tests();
        leader_tests();
        capability_tests();
        hook_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("portrait layer tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
