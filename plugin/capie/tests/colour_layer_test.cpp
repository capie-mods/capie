/**
 * @file colour_layer_test.cpp
 * @brief Offline tests of the country colour layer (API level 8): variables, colour maths, block, slots, hook, repaint.
 *
 * A fake memory holds countries (the `CCountry` vtable, a tag id at +0x8, a variable container
 * pointer at +0x218 built with the engine's insertion rule, a cosmetic tag string at +0x1488),
 * the game-state country list, the six colour defines, the current idler with its vtable, and
 * the map with its mode table. The real layer (`ColourLayer<FakeMemory>`) runs against a fake
 * `GetMapColors` (returns the overlord's block for a subject drawn in its colour) and a fake
 * `RefreshCountryColor` (counts calls, or faults). Covered: decoding, the game's colour maths
 * against an independent double-precision reference, the block layout, slot bounds, pointer and
 * vtable checks, the double-buffer order under a concurrent reader, the fast path, the resolved
 * country, the in-game guard, the repaint of changed countries only, the 23 dirty bytes, the
 * fault latch, the commit name and the capability values. No game, no EXE.
 */
#include "../src/engine/game_calls.hpp"
#include "../src/features/country/capability.hpp"
#include "../src/features/country/colour_layer_hooks.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace layer = capie::country::layer;
namespace colour = capie::country::colour;
namespace country = capie::country;
namespace rva = capie::core::rva;
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

// Addresses, sizes, colours and codes below are literal test data.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// @brief Byte-addressed fake memory made of separate regions, with counted getter reads and writes.
struct FakeMemory {
    std::map<uptr, std::vector<unsigned char>> regions;  ///< Start address -> bytes.
    mutable std::atomic<int> hot_reads{0};               ///< Reads made through `hot_read`.
    int writes{0};                                       ///< Writes made through `write`.

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
     * @brief The getter's SEH-only read (counted).
     * @param address Address.
     * @param out Destination.
     * @param size Byte count.
     * @return As `read`.
     */
    bool hot_read(uptr address, void* out, std::size_t size) const noexcept {
        hot_reads.fetch_add(1);
        return read(address, out, size);
    }

    /**
     * @brief Writes into a mapped region (the mode-dirty bytes).
     * @param address Address.
     * @param data Bytes.
     * @param size Byte count.
     * @return `false` when unmapped.
     */
    bool write(uptr address, const void* data, std::size_t size) noexcept {
        auto found = regions.upper_bound(address);
        if (found == regions.begin()) {
            return false;
        }
        --found;
        const auto offset = address - found->first;
        if (offset > found->second.size() || size > found->second.size() - offset) {
            return false;
        }
        std::memcpy(found->second.data() + offset, data, size);
        ++writes;
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

    /**
     * @brief Reads a value (test side).
     * @param address Address.
     * @return Value.
     */
    template <class T>
    T get(uptr address) const {
        T value{};
        static_cast<void>(read(address, &value, sizeof(T)));
        return value;
    }
};

/// @brief Policy that the layer writes through (the fake keeps `write` non-const; the layer holds a const ref).
struct Memory {
    FakeMemory* fake;  ///< Backing fake.
    /// @brief Read. @return As the fake.
    bool read(uptr a, void* o, std::size_t s) const noexcept { return fake->read(a, o, s); }
    /// @brief Hot read. @return As the fake.
    bool hot_read(uptr a, void* o, std::size_t s) const noexcept { return fake->hot_read(a, o, s); }
    /// @brief Write. @return As the fake.
    bool write(uptr a, const void* d, std::size_t s) const noexcept { return fake->write(a, d, s); }
};

/// @brief Next free fake address for heap keys.
uptr next_heap = 0x7000'0000;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test allocator

/**
 * @brief Builds a variable container with the engine's insertion rule.
 * @param mem Fake memory.
 * @param container Container address.
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

using Vars = std::vector<std::pair<std::string, std::int64_t>>;

/**
 * @brief Packs a text three characters per element (CONTRACT 17.3, written independently).
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
 * @brief Variables of a colour layer as the contract writes them (19.5).
 * @param r Red.
 * @param g Green.
 * @param b Blue.
 * @param snapshot Cosmetic snapshot; `nullptr` = no snapshot variables.
 * @return Variables.
 */
Vars colour_variables(std::int64_t r, std::int64_t g, std::int64_t b, const char* snapshot) {
    Vars out{{"capie_cc_ver", fp(1)}, {"capie_cc_r", fp(r)}, {"capie_cc_g", fp(g)}, {"capie_cc_b", fp(b)},
             {"capie_cc_rev", fp(1)},  {"capie_cf_ver", fp(1)}};  // a flag layer on the same country must not mix in
    if (snapshot != nullptr) {
        const std::string text = snapshot;
        out.emplace_back("capie_cc_cos_len", fp(static_cast<std::int64_t>(text.size())));
        const auto elements = pack(text);
        if (!elements.empty()) {
            out.emplace_back("capie_cc_cos^num", fp(static_cast<std::int64_t>(elements.size())));
        }
        for (std::size_t i = 0; i < elements.size(); ++i) {
            out.emplace_back("capie_cc_cos^" + std::to_string(i), fp(elements[i]));
        }
    }
    return out;
}

/**
 * @brief Replaces (or adds) a variable.
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
constexpr uptr fake_idler = 0x6000'0000;
constexpr uptr fake_idler_vtable = 0x6100'0000;
constexpr uptr fake_map = 0x6200'0000;
constexpr uptr fake_manager = 0x6300'0000;
constexpr uptr fake_modes = 0x6400'0000;

/// @brief Vanilla map defines (`COUNTRY_COLOR_*`).
constexpr colour::Defines vanilla_map{0.0F, 0.6F, 0.8F};
/// @brief Vanilla UI defines (`COUNTRY_UI_COLOR_*`).
constexpr colour::Defines vanilla_ui{0.0F, 1.0F, 1.0F};

/**
 * @brief Maps a country: vtable, tag id, variables (container pointer at +0x218), cosmetic tag.
 * @param mem Memory.
 * @param address Country address.
 * @param tag Tag id.
 * @param vars Variables.
 * @param cosmetic Cosmetic tag (at most 15 characters, inline string).
 */
void map_country(FakeMemory& mem, uptr address, std::int32_t tag, const Vars& vars, const std::string& cosmetic) {
    mem.map(address, 0x1500);
    mem.put(address, fake_image + rva::vt_country);
    mem.put(address + 0x8, tag);
    const uptr container = address + 0x10'0000;
    mem.put(address + 0x218, container);
    check(build_table(mem, container, address + 0x11'0000, vars), "country table builds");
    std::array<char, 16> small{};
    std::copy(cosmetic.begin(), cosmetic.end(), small.begin());
    mem.put(address + 0x1488, small);
    mem.put(address + 0x1498, std::uint64_t{cosmetic.size()});
    mem.put(address + 0x14A0, std::uint64_t{15});
}

/**
 * @brief Sets a country's cosmetic tag (inline string).
 * @param mem Memory.
 * @param address Country.
 * @param cosmetic Tag.
 */
void set_cosmetic(FakeMemory& mem, uptr address, const std::string& cosmetic) {
    std::array<char, 16> small{};
    std::copy(cosmetic.begin(), cosmetic.end(), small.begin());
    mem.put(address + 0x1488, small);
    mem.put(address + 0x1498, std::uint64_t{cosmetic.size()});
}

// ---- Fake game functions ---------------------------------------------------------------------

/// @brief Subject -> overlord for the fake getter (a subject drawn in its overlord's colour).
std::map<uptr, uptr> overlord_of;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state
/// @brief Calls of the fake refresh.
std::atomic<int> refresh_calls{0};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state
/// @brief Tag pointers the fake refresh received.
std::vector<uptr> refresh_tags;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state

/**
 * @brief Fake `GetMapColors`: the overlord's block for a subject in `overlord_of`, else its own.
 * @param country Country.
 * @return Block address.
 */
const void* fake_map_colors(const void* country) {
    auto address = reinterpret_cast<uptr>(country);
    if (const auto found = overlord_of.find(address); found != overlord_of.end()) {
        address = found->second;
    }
    return reinterpret_cast<const void*>(address + 0x350);
}

/**
 * @brief Fake `RefreshCountryColor` that counts.
 * @param map Map.
 * @param tag Tag pointer.
 */
void fake_refresh(void* map, const std::int32_t* tag) {
    static_cast<void>(map);
    ++refresh_calls;
    refresh_tags.push_back(reinterpret_cast<uptr>(tag));
}

/**
 * @brief Fake `RefreshCountryColor` that faults like a stale object.
 * @param map Map.
 * @param tag Tag pointer.
 */
void faulting_refresh(void* map, const std::int32_t* tag) {
    static_cast<void>(map);
    static_cast<void>(tag);
    ++refresh_calls;
    // NOLINTNEXTLINE(performance-no-int-to-ptr): deliberate access violation, caught by the wrapper's SEH
    *reinterpret_cast<volatile int*>(std::uintptr_t{16}) = 1;
}

/// @brief Countries of the standard fixture.
constexpr uptr pol = 0x2000'0000;
constexpr uptr subject = 0x2100'0000;
constexpr uptr other = 0x2200'0000;

/// @brief Game state, list, globals, idler, map and three countries; the layer is activated.
struct Fixture {
    FakeMemory mem;                                     ///< Fake memory.
    Memory policy{&mem};                                ///< Layer policy.
    colour::ColourLayer<Memory> colour_layer{policy};  ///< Layer under test.

    /// @brief Builds the fixture. @param in_game Idler slot is the in-game one.
    explicit Fixture(bool in_game = true) {
        overlord_of.clear();
        refresh_calls = 0;
        refresh_tags.clear();
        capie::engine::game_call_faulted.store(false);
        mem.map(fake_image + 0x33DD000, 0xC000);  // gamestate, idler, defines and map globals
        mem.put(fake_image + rva::gamestate_global, fake_state);
        mem.put(fake_image + rva::current_idler, fake_idler);
        mem.put(fake_image + rva::map_singleton, fake_map);
        mem.put(fake_image + rva::define_color_hue, vanilla_map.hue);
        mem.put(fake_image + rva::define_color_saturation, vanilla_map.saturation);
        mem.put(fake_image + rva::define_color_value, vanilla_map.value);
        mem.put(fake_image + rva::define_ui_color_hue, vanilla_ui.hue);
        mem.put(fake_image + rva::define_ui_color_saturation, vanilla_ui.saturation);
        mem.put(fake_image + rva::define_ui_color_value, vanilla_ui.value);
        mem.map(fake_idler, 0x10);
        mem.put(fake_idler, fake_idler_vtable);
        mem.map(fake_idler_vtable, 0x300);
        set_idler_slot(in_game ? fake_image + rva::idler_in_game_true : fake_image + rva::idler_in_game_false);
        mem.map(fake_map, 0x10);
        mem.put(fake_map + 0x8, fake_manager);
        mem.map(fake_manager, 0x40);
        mem.put(fake_manager + 0x30, fake_modes);
        mem.map(fake_modes, 0x80);
        mem.map(fake_state, 0x400);
        mem.put(fake_state + 0x310, fake_list);
        mem.put(fake_state + 0x31C, std::int32_t{5});
        mem.map(fake_list, 5 * 8);
        map_country(mem, pol, 1, colour_variables(30, 90, 160, ""), "");
        map_country(mem, subject, 2, colour_variables(200, 10, 10, nullptr), "");
        map_country(mem, other, 3, {{"other", fp(1)}}, "");
        mem.put(fake_list + 8, pol);
        mem.put(fake_list + 16, subject);
        mem.put(fake_list + 24, other);
        colour_layer.originals().map_colors = &fake_map_colors;
        colour_layer.originals().refresh = &fake_refresh;
        colour_layer.activate(fake_image, nullptr);
    }

    /// @brief Sets the idler's in-game slot. @param target Slot value.
    void set_idler_slot(uptr target) { mem.put(fake_idler_vtable + 0x2D8, target); }

    /// @brief Replaces a country's variables. @param address Country. @param vars Variables.
    void set_vars(uptr address, const Vars& vars) {
        check(build_table(mem, address + 0x10'0000, address + 0x11'0000, vars), "rebuild table");
    }

    /// @brief The block the getter returns. @param address Country. @return Block address.
    uptr got(uptr address) { return reinterpret_cast<uptr>(colour_layer.map_colors(reinterpret_cast<void*>(address))); }

    /// @brief Whether the getter returns the layer's block for a country. @param address Country. @param id Tag id. @return Result.
    bool overridden(uptr address, std::int32_t id) {
        const auto* block = colour_layer.published(id);
        return block != nullptr && got(address) == reinterpret_cast<uptr>(block);
    }
};

// ---- Decode ---------------------------------------------------------------------------------

void decode_tests() {
    const auto read = [](const Vars& vars, const std::string& cosmetic = "") {
        FakeMemory mem;
        map_country(mem, 0x1000'0000, 1, vars, cosmetic);
        return colour::read_colour_layer(mem, 0x1000'0000);
    };
    auto got = read(colour_variables(30, 90, 160, nullptr));
    check(got.status == layer::Status::valid && got.rgb == 0x1E5AA0U && !got.has_snapshot, "valid: 0x1E5AA0");
    check(read(colour_variables(0, 0, 0, nullptr)).rgb == 0 && read(colour_variables(255, 255, 255, nullptr)).rgb == 0xFFFFFFU,
          "0 and 255 are valid channels");
    check(read({{"other", fp(1)}}).status == layer::Status::absent, "no capie_cc_ver: absent");
    const auto base = colour_variables(10, 20, 30, nullptr);
    const std::pair<const char*, Vars> invalid[] = {
        {"ver 2", with(base, "capie_cc_ver", fp(2))},
        {"ver fraction", with(base, "capie_cc_ver", fp(1) + 1)},
        {"red missing", without(base, "capie_cc_r")},
        {"green missing", without(base, "capie_cc_g")},
        {"blue missing", without(base, "capie_cc_b")},
        {"red -1", with(base, "capie_cc_r", fp(-1))},
        {"green 256", with(base, "capie_cc_g", fp(256))},
        {"blue 1.5", with(base, "capie_cc_b", fp(1) + 50000)},
        {"snapshot length 64", with(base, "capie_cc_cos_len", fp(64))},
        {"snapshot ^num missing", with(base, "capie_cc_cos_len", fp(3))},
        {"snapshot fraction", with(base, "capie_cc_cos_len", fp(1) + 7)},
    };
    for (const auto& [label, vars] : invalid) {
        const auto bad = read(vars);
        check(bad.status == layer::Status::invalid && bad.rgb == 0 && !bad.has_snapshot, std::string("invalid: ") + label);
    }
    got = read(colour_variables(1, 2, 3, ""));
    check(got.status == layer::Status::valid && got.has_snapshot && got.snapshot.size == 0, "snapshot 0: had no tag");
    got = read(colour_variables(1, 2, 3, "POL_KINGDOM"));
    check(got.status == layer::Status::valid && got.has_snapshot && got.snapshot.view() == "POL_KINGDOM",
          "snapshot read");
    // Verdicts.
    const auto verdict = [](const colour::Layer& l, const std::string& tag) {
        return colour::colour_verdict(l, tag, false);
    };
    check(verdict(read(colour_variables(1, 2, 3, nullptr)), "ANY") == colour::Verdict::valid, "no snapshot: shown");
    check(verdict(read(colour_variables(1, 2, 3, "")), "") == colour::Verdict::valid, "snapshot 0, no tag: shown");
    check(verdict(read(colour_variables(1, 2, 3, "")), "POL_X") == colour::Verdict::hidden,
          "snapshot 0, tag now: hidden");
    check(verdict(read(colour_variables(1, 2, 3, "POL_X")), "POL_X") == colour::Verdict::valid, "snapshot matches");
    check(verdict(read(colour_variables(1, 2, 3, "POL_X")), "POL_Y") == colour::Verdict::hidden,
          "snapshot mismatch: hidden");
    check(colour::colour_verdict(read(colour_variables(1, 2, 3, "POL_X")), "POL_X", true) == colour::Verdict::hidden,
          "too long cosmetic: hidden");
    check(verdict(read(with(base, "capie_cc_ver", fp(2))), "") == colour::Verdict::invalid, "invalid verdict");
    // Unreadable container.
    FakeMemory mem;
    mem.map(0x1000'0000, 0x400);
    mem.put(0x1000'0218, uptr{0x3333'0000});
    check(colour::read_colour_layer(mem, 0x1000'0000).status == layer::Status::unreadable, "unreadable container");
    check(colour::read_colour_layer(mem, 0x9000'0000).status == layer::Status::unreadable, "unmapped country");
    check(std::string_view(colour::commit_variable) == "capie_cc_rev", "commit variable is capie_cc_rev");
}

// ---- Maths ----------------------------------------------------------------------------------

/// @brief Independent double-precision reference (textbook formulas on raw channels).
struct Reference {
    double r, g, b;  ///< Result.
};

/**
 * @brief Reference adjustment: hue from raw max/min, then the sector rule with no wrap.
 * @param rgb Packed colour.
 * @param d Defines.
 * @return Adjusted colour.
 */
Reference reference(std::uint32_t rgb, colour::Defines d) {
    const double r = static_cast<double>((rgb >> 16U) & 0xFFU) / 255.0;
    const double g = static_cast<double>((rgb >> 8U) & 0xFFU) / 255.0;
    const double b = static_cast<double>(rgb & 0xFFU) / 255.0;
    const double high = std::max({r, g, b});
    const double low = std::min({r, g, b});
    const double delta = high - low;
    double h = 0.0;
    if (delta > 0.0) {
        if (high == r) {
            h = std::fmod(((g - b) / delta) + 6.0, 6.0) / 6.0;
        } else if (high == g) {
            h = (((b - r) / delta) + 2.0) / 6.0;
        } else {
            h = (((r - g) / delta) + 4.0) / 6.0;
        }
    }
    double s = high > 0.0 ? delta / high : 0.0;
    double v = high;
    h += d.hue;
    s *= d.saturation;
    v *= d.value;
    if (s == 0.0) {
        return {v, v, v};
    }
    const double sector = std::min(std::floor(h * 6.0), 5.0);
    const double f = (h * 6.0) - sector;
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - (s * f));
    const double t = v * (1.0 - (s * (1.0 - f)));
    switch (static_cast<int>(sector)) {
        case 0:
            return {v, t, p};
        case 1:
            return {q, v, p};
        case 2:
            return {p, v, t};
        case 3:
            return {p, q, v};
        case 4:
            return {t, p, v};
        default:
            return {v, p, q};
    }
}

/**
 * @brief Compares the layer's maths with the reference.
 * @param rgb Colour.
 * @param d Defines.
 * @param label Label.
 */
void same(std::uint32_t rgb, colour::Defines d, const std::string& label) {
    const auto got = colour::adjust(rgb, d);
    const auto want = reference(rgb, d);
    const bool ok = std::fabs(got.r - want.r) < 1e-6 && std::fabs(got.g - want.g) < 1e-6 &&
                    std::fabs(got.b - want.b) < 1e-6;
    check(ok, label + " (" + std::to_string(got.r) + "," + std::to_string(got.g) + "," + std::to_string(got.b) +
                  " want " + std::to_string(want.r) + "," + std::to_string(want.g) + "," + std::to_string(want.b) + ")");
}

void maths_tests() {
    const std::pair<const char*, std::uint32_t> vectors[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},      {"green", 0x00FF00},
        {"blue", 0x0000FF},  {"grey", 0x808080},  {"ENG 201 56 93", 0xC9385D}, {"SOV 125 13 24", 0x7D0D18},
        {"POL 214 102 139", 0xD6668B}, {"USA 20 133 237", 0x1485ED}, {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF},
        {"magenta", 0xFF00FF}, {"picked 30 90 160", 0x1E5AA0},
    };
    for (const auto& [label, rgb] : vectors) {
        same(rgb, vanilla_map, std::string("map defines: ") + label);
        same(rgb, vanilla_ui, std::string("ui defines: ") + label);
        same(rgb, {0.5F, 1.0F, 1.0F}, std::string("hue +0.5 past 1, no wrap: ") + label);
        same(rgb, {0.0F, 0.0F, 1.0F}, std::string("saturation x0: ") + label);
        same(rgb, {0.0F, 1.0F, 1.2F}, std::string("value x1.2, no clamp: ") + label);
    }
    // The UI define set (0, 1, 1) is the identity.
    for (const auto& [label, rgb] : vectors) {
        const auto ui = colour::adjust(rgb, vanilla_ui);
        const auto in = colour::from_packed(rgb);
        check(std::fabs(ui.r - in.r) < 1e-6 && std::fabs(ui.g - in.g) < 1e-6 && std::fabs(ui.b - in.b) < 1e-6,
              std::string("ui identity: ") + label);
    }
    // Map defines: S x0.6, V x0.8 (not the other way round).
    const auto red = colour::adjust(0xFF0000, vanilla_map);
    check(std::fabs(red.r - 0.8F) < 1e-6 && std::fabs(red.g - (0.8F * 0.4F)) < 1e-6, "red: V 0.8, S 0.6");
    const auto bright = colour::adjust(0xFF0000, {0.0F, 1.0F, 1.2F});
    check(bright.r > 1.0F, "value above 1 is not clamped");
    check(colour::floor_f(-0.5F) == -1.0F && colour::floor_f(2.0F) == 2.0F && colour::floor_f(2.7F) == 2.0F,
          "floor");
}

// ---- Block ----------------------------------------------------------------------------------

void block_tests() {
    static_assert(sizeof(colour::ColourBlock) == 0x60);
    const auto block = colour::make_block(fake_image, 0xC9385D, vanilla_map, vanilla_ui);
    std::array<unsigned char, 0x60> bytes{};
    std::memcpy(bytes.data(), &block, sizeof(block));
    const auto at = [&bytes](std::size_t offset) {
        float value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    const auto at64 = [&bytes](std::size_t offset) {
        uptr value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    const auto map = colour::adjust(0xC9385D, vanilla_map);
    const auto ui = colour::adjust(0xC9385D, vanilla_ui);
    check(at64(0) == fake_image + rva::vt_country_colors && at64(0x10) == fake_image + rva::vt_color &&
              at64(0x30) == fake_image + rva::vt_color,
          "vtables: CCountryColors, CColor, CColor at base + RVA");
    check(at(0x20) == map.r && at(0x24) == map.g && at(0x28) == map.b && at(0x2C) == 1.0F, "color floats at +0x20");
    check(at(0x40) == ui.r && at(0x44) == ui.g && at(0x48) == ui.b && at(0x4C) == 1.0F, "color_ui floats at +0x40");
    check(bytes[0x50] == 3, "flags 3 at +0x50");
    check(offsetof(colour::ColourBlock, color) == 0x10 && offsetof(colour::ColourBlock, color_ui) == 0x30 &&
              offsetof(colour::ColourBlock, flags) == 0x50,
          "runtime offsets");
}

// ---- Slots and detour -----------------------------------------------------------------------

void detour_tests() {
    {
        Fixture f;
        f.colour_layer.deactivate();
        f.mem.hot_reads = 0;
        check(f.got(pol) == pol + 0x350 && f.mem.hot_reads == 0, "disabled: passes through, no read");
    }
    {
        Fixture f;
        f.mem.hot_reads = 0;
        check(f.colour_layer.live() == 0 && f.got(pol) == pol + 0x350 && f.mem.hot_reads == 0,
              "no layer published: fast path, no read");
        check(f.colour_layer.update(pol) && f.colour_layer.live() == 1, "update publishes POL");
        check(f.overridden(pol, 1), "POL returns its block");
        const auto* block = f.colour_layer.published(1);
        check(block != nullptr && block->flags == 3, "published block flags 3");
        check(f.got(other) == other + 0x350, "a country without a slot gets the game's block");
        // A subject drawn in POL's colour: the getter resolves to POL, so POL's block (not the subject's own).
        check(f.colour_layer.update(subject) && f.colour_layer.live() == 2, "subject has its own layer");
        overlord_of[subject] = pol;
        check(f.got(subject) == reinterpret_cast<uptr>(f.colour_layer.published(1)),
              "subject in the overlord's colour: the overlord's block");
        overlord_of.clear();
        check(f.got(subject) == reinterpret_cast<uptr>(f.colour_layer.published(2)),
              "subject in its own colour: its own block");
        // Wrong vtable on the resolved object.
        f.mem.put(pol, fake_image + 0x1234);
        check(f.got(pol) == pol + 0x350, "wrong vtable: the game's block");
        f.mem.put(pol, fake_image + rva::vt_country);
        // Another object with the same tag id (the slot is keyed by the pointer).
        map_country(f.mem, 0x2300'0000, 1, {{"other", fp(1)}}, "");
        check(f.got(0x2300'0000) == 0x2300'0000 + 0x350, "same id, other object: the game's block");
        // Reuse of the id by that object resets the slot.
        check(f.colour_layer.update(0x2300'0000) == false && f.colour_layer.published(1) == nullptr &&
                  f.colour_layer.live() == 1 && f.got(pol) == pol + 0x350,
              "id reused by another object: slot reset");
    }
    {
        Fixture f;
        map_country(f.mem, 0x2400'0000, 0, colour_variables(1, 2, 3, nullptr), "");
        map_country(f.mem, 0x2500'0000, 4095, colour_variables(1, 2, 3, nullptr), "");
        map_country(f.mem, 0x2600'0000, 4096, colour_variables(1, 2, 3, nullptr), "");
        map_country(f.mem, 0x2700'0000, -1, colour_variables(1, 2, 3, nullptr), "");
        check(!f.colour_layer.update(0x2400'0000) && f.colour_layer.live() == 0, "id 0 refused");
        check(f.colour_layer.update(pol) && f.colour_layer.published(1) != nullptr, "id 1 accepted");
        check(f.colour_layer.update(0x2500'0000) && f.overridden(0x2500'0000, 4095), "id 4095 accepted");
        check(!f.colour_layer.update(0x2600'0000) && f.colour_layer.published(4096) == nullptr &&
                  f.colour_layer.live() == 2 && f.got(0x2600'0000) == 0x2600'0000 + 0x350,
              "id 4096 refused");
        check(!f.colour_layer.update(0x2700'0000) && f.colour_layer.live() == 2, "id -1 refused");
    }
    {
        // Double buffer: a change writes the other block; a reader holding the old one still sees the old colour.
        Fixture f;
        check(f.colour_layer.update(pol), "first colour published");
        const auto* first = f.colour_layer.published(1);
        const auto old_red = first != nullptr ? first->color_ui.rgba[0] : -1.0F;
        f.set_vars(pol, colour_variables(250, 90, 160, ""));
        check(f.colour_layer.update(pol), "second colour published");
        const auto* second = f.colour_layer.published(1);
        check(second != nullptr && second != first && first->color_ui.rgba[0] == old_red &&
                  second->color_ui.rgba[0] != old_red,
              "a change writes the inactive block, the published one is untouched");
    }
    {
        // Defines unreadable at an update (activated before): nothing is shown (fail closed).
        Fixture f;
        f.mem.regions.erase(fake_image + 0x33DD000);
        f.mem.map(fake_image + 0x33DD000, 0x1000);  // game state and idler globals only
        f.mem.put(fake_image + rva::gamestate_global, fake_state);
        f.mem.put(fake_image + rva::current_idler, fake_idler);
        f.mem.map(fake_image + 0x33E7000, 0x1000);  // map global
        f.mem.put(fake_image + rva::map_singleton, fake_map);
        check(f.colour_layer.enabled() && !f.colour_layer.update(pol) && f.colour_layer.published(1) == nullptr &&
                  f.colour_layer.live() == 0 && refresh_calls == 0,
              "defines unreadable: no block, no refresh");
    }
}

void flip_order_test() {
    Fixture f;
    constexpr int flips = 3000;
    // One container per colour (built before the reader starts): the writer only swaps the pointer.
    std::vector<uptr> containers;
    for (int i = 1; i <= flips; ++i) {
        const uptr container = 0x0800'0000 + (static_cast<uptr>(i) * 0x4000);
        check(build_table(f.mem, container, container + 0x100,
                          colour_variables((i >> 8) & 0xFF, i & 0xFF, 7, nullptr)),
              "flip table");
        containers.push_back(container);
    }
    std::atomic<bool> stop{false};
    std::atomic<int> bad_flags{0};
    std::atomic<int> went_back{0};
    std::atomic<int> seen{0};
    f.mem.put(pol + 0x218, containers[0]);
    check(f.colour_layer.update(pol), "first flip");
    std::thread reader([&] {
        int last = 0;
        while (!stop.load()) {
            const auto* block = static_cast<const colour::ColourBlock*>(f.colour_layer.map_colors(reinterpret_cast<void*>(pol)));
            if (reinterpret_cast<uptr>(block) == pol + 0x350) {
                continue;
            }
            if (block->flags != 3) {
                ++bad_flags;
            }
            const int value = static_cast<int>(std::lround(block->color_ui.rgba[0] * 255.0F)) * 256 +
                              static_cast<int>(std::lround(block->color_ui.rgba[1] * 255.0F));
            if (value < last) {
                ++went_back;
            }
            last = value;
            ++seen;
        }
    });
    for (int i = 1; i < flips; ++i) {
        f.mem.put(pol + 0x218, containers[static_cast<std::size_t>(i)]);
        static_cast<void>(f.colour_layer.update(pol));
    }
    stop = true;
    reader.join();
    check(seen.load() > 0, "reader saw blocks");
    check(bad_flags.load() == 0, "reader never saw a block with flags other than 3");
    check(went_back.load() == 0, "reader never saw an older colour after a newer one (" + std::to_string(went_back.load()) + ")");
}

// ---- Refresh --------------------------------------------------------------------------------

void refresh_tests() {
    {
        Fixture f;
        check(f.colour_layer.update(pol) && refresh_calls == 1 && refresh_tags.size() == 1 && refresh_tags[0] == pol + 8,
              "changed country: one refresh with the country's tag field");
        check(!f.colour_layer.update(pol) && refresh_calls == 1, "unchanged: no refresh");
        std::array<std::uint8_t, 0x80> modes{};
        static_cast<void>(f.mem.read(fake_modes, modes.data(), modes.size()));
        const auto dirty = std::count(modes.begin() + 0x48, modes.begin() + 0x48 + 23, std::uint8_t{1});
        check(dirty == 23 && modes[0x47] == 0 && modes[0x48 + 23] == 0, "23 mode-dirty bytes set (+0x48..+0x5E)");
        // refresh_all: only changed countries are refreshed.
        refresh_calls = 0;
        auto stats = f.colour_layer.refresh_all();
        check(stats.list_ok && stats.countries == 3 && stats.changed == 1 && refresh_calls == 1,
              "commit: the subject changed, POL unchanged, other has none");
        refresh_calls = 0;
        stats = f.colour_layer.refresh_all();
        check(stats.changed == 0 && refresh_calls == 0, "second commit: nothing changed, no refresh");
        f.set_vars(pol, colour_variables(31, 90, 160, ""));
        stats = f.colour_layer.refresh_all();
        check(stats.changed == 1 && refresh_calls == 1, "a new colour: one refresh");
        f.set_vars(pol, {{"other", fp(1)}});
        stats = f.colour_layer.refresh_all();
        check(stats.changed == 1 && refresh_calls == 2 && f.colour_layer.published(1) == nullptr &&
                  f.got(pol) == pol + 0x350,
              "removed: block dropped, refreshed, the game's block");
    }
    // Guard false: no call (the slot is still published; the map is built later from the getter).
    const auto guard_case = [](const char* label, auto&& setup) {
        Fixture f;
        setup(f);
        check(f.colour_layer.update(pol) && refresh_calls == 0 && f.colour_layer.enabled() &&
                  f.colour_layer.published(1) != nullptr,
              std::string("guard false, no call: ") + label);
    };
    guard_case("map null", [](Fixture& f) { f.mem.put(fake_image + rva::map_singleton, uptr{0}); });
    guard_case("idler null", [](Fixture& f) { f.mem.put(fake_image + rva::current_idler, uptr{0}); });
    guard_case("idler slot is the base one", [](Fixture& f) { f.set_idler_slot(fake_image + rva::idler_in_game_false); });
    guard_case("idler slot unknown", [](Fixture& f) { f.set_idler_slot(fake_image + 0x123456); });
    {
        Fixture f;
        f.mem.put(fake_state + 0x31C, std::int32_t{1});  // POL's id 1 is not below the count
        check(f.colour_layer.update(pol) && refresh_calls == 0, "id not below the country count: no call");
    }
    {
        Fixture f;
        f.colour_layer.originals().refresh = &faulting_refresh;
        check(f.colour_layer.update(pol) && refresh_calls == 1, "faulting refresh called once");
        check(!f.colour_layer.enabled() && capie::engine::game_call_faulted.load() && f.got(pol) == pol + 0x350,
              "a fault latches and turns the layer off: the getter passes through");
        refresh_calls = 0;
        check(f.colour_layer.refresh_all().countries == 0 && refresh_calls == 0, "no repaint after a fault");
        capie::engine::game_call_faulted.store(false);
    }
    {
        Fixture f;
        capie::engine::game_call_faulted.store(true);  // the editor DLL faulted: shared latch
        check(f.colour_layer.update(pol) && refresh_calls == 0 && !f.colour_layer.enabled(),
              "a latched fault refuses the call and turns the layer off");
        capie::engine::game_call_faulted.store(false);
    }
    {
        Fixture f;
        f.mem.regions.erase(fake_image + 0x33DD000);
        f.mem.map(fake_image + 0x33DD000, 0x100);  // the define globals are gone
        colour::ColourLayer<Memory> fresh{f.policy};
        fresh.originals().map_colors = &fake_map_colors;
        fresh.originals().refresh = &fake_refresh;
        fresh.activate(fake_image, nullptr);
        check(!fresh.enabled(), "unreadable defines: the layer stays off");
        colour::ColourLayer<Memory> no_refresh{f.policy};
        no_refresh.originals().map_colors = &fake_map_colors;
        no_refresh.activate(fake_image, nullptr);
        check(!no_refresh.enabled(), "no refresh function: off");
    }
}

// ---- Hooks ----------------------------------------------------------------------------------

void hook_tests() {
    Fixture f;
    check(f.colour_layer.update(pol) && f.colour_layer.published(2) == nullptr, "UpdateNames re-evaluates only its country");
    // A cosmetic tag change between two UpdateNames calls hides the layer (snapshot "" = no tag).
    set_cosmetic(f.mem, pol, "POL_KING");
    check(f.colour_layer.update(pol) && f.colour_layer.published(1) == nullptr && f.got(pol) == pol + 0x350,
          "cosmetic tag change hides the layer");
    set_cosmetic(f.mem, pol, "");
    check(f.colour_layer.update(pol) && f.colour_layer.published(1) != nullptr, "cosmetic tag back: shown again");
    // A snapshot that names the tag.
    f.set_vars(pol, colour_variables(30, 90, 160, "POL_KING"));
    check(f.colour_layer.update(pol) && f.colour_layer.published(1) == nullptr, "snapshot POL_KING, no tag: hidden");
    set_cosmetic(f.mem, pol, "POL_KING");
    check(f.colour_layer.update(pol) && f.colour_layer.published(1) != nullptr, "snapshot POL_KING matches: shown");
    // Disabled: update does nothing.
    f.colour_layer.deactivate();
    f.set_vars(pol, {{"other", fp(1)}});
    check(!f.colour_layer.update(pol) && f.colour_layer.refresh_all().countries == 0, "disabled: no evaluation");
}

void capability_tests() {
    for (int mask = 0; mask < 32; ++mask) {
        const bool names = (mask & 1) != 0;
        const bool flag = (mask & 2) != 0;
        const bool portrait = (mask & 4) != 0;
        const bool colour_on = (mask & 8) != 0;
        const bool sub = (mask & 16) != 0;
        const auto value = country::capabilities_of(names, flag, portrait, colour_on, sub);
        const std::string label = "capability mask " + std::to_string(mask);
        if (!names) {
            check(value == country::country_capabilities_v1 && !country::names_active(value) &&
                      !country::colour_active(value),
                  label + ": v1");
            continue;
        }
        check(country::names_active(value), label + ": names");
        check(country::flag_active(value) == flag, label + ": flag");
        check(country::portrait_active(value) == portrait, label + ": portrait");
        check(country::colour_active(value) == colour_on, label + ": colour");
        check(country::subideology_active(value) == sub, label + ": sub-ideology");
        if (!portrait && !colour_on && !sub) {
            check(value == (flag ? country::country_capabilities_v3 : country::country_capabilities_v2),
                  label + ": v2/v3");
        } else {
            check(country::is_v4(value), label + ": v4");
        }
    }
    check(country::capabilities_of(true, false, false, true) == 0x524E4404U, "colour only: 0x524E4404");
    check(country::capabilities_of(true, true, true, true, true) == 0x524E440FU, "all: 0x524E440F");
    check(!country::is_v4(0x524E4400U) && !country::is_v4(0x524E4401U) && !country::is_v4(0x524E4414U) &&
              !country::colour_active(0x524E4403U),
          "undefined v4 values are refused");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        decode_tests();
        maths_tests();
        block_tests();
        detour_tests();
        flip_order_test();
        refresh_tests();
        hook_tests();
        capability_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("colour layer tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
