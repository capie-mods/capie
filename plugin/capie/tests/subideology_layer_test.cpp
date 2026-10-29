/**
 * @file subideology_layer_test.cpp
 * @brief Offline tests of the ruling-party ideology name layer (API level 8): variables, binding, lapse, panel frame.
 *
 * A fake memory holds countries (a tag id at +0x8, a variable container pointer at +0x218 built
 * with the engine's insertion rule, a politics pointer at +0xF90), their politics objects (ruling
 * party at +0xD0), parties (the `CPoliticalParty` vtable, the ideology object at +0x18, leader
 * roles at +0x70 / count +0x7C), ideology groups (the `CIdeologyGroup` vtable, the token string at
 * +0x18), characters (the `CCharacter` vtable, id at +0xC), the game-state country list and shown
 * tags (+0x520 / +0x524), and a politics view whose idler (+0x5F0) has the `CInGameIdler` vtable.
 * The real layer (`IdeologyLayer<FakeMemory>`) runs against a fake panel writer that localises
 * keys through the real `country::lookup_n` hook body (whose trampoline is a fake returning the
 * game's text). Covered: decoding, the binding chain and its class checks, binding kept while the
 * text and revision are unchanged, lapse on a party or leader change, re-binding on a new
 * revision, the shown tag, the exact-key answer only inside the writer, frame restore on an
 * exception, the slot checks and the commit name. No game, no EXE.
 */
#include "../src/features/country/subideology_layer_hooks.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace layer = capie::country::layer;
namespace sub = capie::country::subideology;
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

/// @brief Next free fake address for heap keys and strings.
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
 * @brief Variables of a layer as the contract writes them (20.5).
 * @param text Name.
 * @param rev Raw `capie_sn_rev` (fixed point); negative = absent.
 * @return Variables.
 */
Vars name_variables(std::u32string_view text, std::int64_t rev = fp(1)) {
    Vars out{{"capie_sn_ver", fp(1)}, {"capie_cn_ver", fp(1)}};  // a name layer on the same country must not mix in
    const auto codes = codes_of(text);
    out.emplace_back("capie_sn_n^num", fp(static_cast<std::int64_t>(codes.size())));
    for (std::size_t i = 0; i < codes.size(); ++i) {
        out.emplace_back("capie_sn_n^" + std::to_string(i), fp(codes[i]));
    }
    if (rev >= 0) {
        out.emplace_back("capie_sn_rev", rev);
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
constexpr uptr fake_view = 0x5200'0000;
constexpr uptr fake_idler = 0x5300'0000;

/**
 * @brief Maps an ideology group with a token (inline up to 15 characters, else on the fake heap).
 * @param mem Memory.
 * @param group Group address.
 * @param token Token.
 * @param capacity Capacity to store (0 = derived from the size).
 */
void map_group(FakeMemory& mem, uptr group, const std::string& token, std::uint64_t capacity = 0) {
    mem.map(group, 0x40);
    mem.put(group, fake_image + rva::vt_ideology_group);
    const std::uint64_t cap = capacity != 0 ? capacity : (token.size() <= 15 ? 15 : token.size());
    if (cap <= 15) {
        std::array<char, 16> small{};
        std::copy(token.begin(), token.end(), small.begin());
        mem.put(group + 0x18, small);
    } else {
        const auto heap = next_heap;
        next_heap += 0x100;
        auto* bytes = mem.map(heap, token.size() + 1);
        std::copy(token.begin(), token.end(), bytes);
        mem.put(group + 0x18, heap);
    }
    mem.put(group + 0x28, std::uint64_t{token.size()});
    mem.put(group + 0x30, cap);
}

/**
 * @brief Maps a character.
 * @param mem Memory.
 * @param character Address.
 * @param id Character id.
 */
void map_character(FakeMemory& mem, uptr character, std::int32_t id) {
    mem.map(character, 0x20);
    mem.put(character, fake_image + rva::vt_character);
    mem.put(character + 0xC, id);
}

/**
 * @brief Maps a party with an ideology group and (optionally) a leader.
 * @param mem Memory.
 * @param party Party address.
 * @param group Ideology group (may be 0).
 * @param leader Leader character (0 = none).
 */
void map_party(FakeMemory& mem, uptr party, uptr group, uptr leader) {
    mem.map(party, 0x100);
    mem.put(party, fake_image + rva::vt_political_party);
    mem.put(party + 0x18, group);
    const uptr roles = party + 0x80;
    const uptr role = party + 0xC0;
    mem.put(party + 0x70, roles);
    mem.put(party + 0x7C, std::int32_t{leader != 0 ? 1 : 0});
    mem.put(roles, role);
    mem.put(role + 0x8, leader);
}

/**
 * @brief Maps a country: tag id, variables, politics with a ruling party.
 * @param mem Memory.
 * @param address Country.
 * @param tag Tag id.
 * @param vars Variables.
 * @param party Ruling party.
 */
void map_country(FakeMemory& mem, uptr address, std::int32_t tag, const Vars& vars, uptr party) {
    mem.map(address, 0x1000);
    mem.put(address + 0x8, tag);
    const uptr container = address + 0x10'0000;
    mem.put(address + 0x218, container);
    check(build_table(mem, container, address + 0x11'0000, vars), "country table builds");
    const uptr politics = address + 0x20'0000;
    mem.map(politics, 0x100);
    mem.put(address + 0xF90, politics);
    mem.put(politics + 0xD0, party);
}

// ---- Fake game functions ---------------------------------------------------------------------

/// @brief The game's text of every key (the fake lookup trampoline).
constexpr char game_text[] = "GAME";
/// @brief Texts the fake panel received for the ideology key.
std::vector<std::string> panel_texts;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state
/// @brief Texts the fake panel received for an unrelated key (the leader line).
std::vector<std::string> other_texts;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state
/// @brief Key the fake panel localises (the shown party's token).
std::string panel_key = "neutrality";  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state
/// @brief The fake panel throws after localising (exception path of the hook).
bool panel_throws = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake state

/**
 * @brief Fake trampoline of `lookup_n`.
 * @return The game's text.
 */
const char* fake_lookup(const char* /*key*/, int /*length*/) {
    return game_text;
}

/**
 * @brief Fake ruling-party writer: localises its ideology key and an unrelated key through the real hook.
 * @param view View (unused).
 */
void fake_panel(void* /*view*/) {
    panel_texts.emplace_back(country::lookup_n(panel_key.data(), static_cast<int>(panel_key.size())));
    const std::string other = "POLITICS_LEADER";
    other_texts.emplace_back(country::lookup_n(other.data(), static_cast<int>(other.size())));
    if (panel_throws) {
        throw std::runtime_error("panel");
    }
}

/// @brief Countries, parties, groups and leaders of the standard fixture.
constexpr uptr pol = 0x2000'0000;
constexpr uptr ger = 0x2100'0000;
constexpr uptr pol_party = 0x3000'0000;
constexpr uptr pol_party_2 = 0x3000'1000;
constexpr uptr ger_party = 0x3000'2000;
constexpr uptr neutrality = 0x3100'0000;
constexpr uptr fascism = 0x3100'1000;
constexpr uptr leader_a = 0x3200'0000;
constexpr uptr leader_b = 0x3200'1000;

/// @brief Game state, list, view, two countries; the layer is activated.
struct Fixture {
    FakeMemory mem;                          ///< Fake memory.
    sub::IdeologyLayer<FakeMemory> sub_layer{mem};  ///< Layer under test.

    /// @brief Builds the fixture: POL (tag 1, neutrality, leader A, "Sanacja") and GER (tag 2, fascism, no layer).
    Fixture() {
        panel_texts.clear();
        other_texts.clear();
        panel_key = "neutrality";
        panel_throws = false;
        country::original_n = &fake_lookup;
        country::ideology_frame = nullptr;
        mem.map(fake_image + 0x33DD000, 0x1000);
        mem.put(fake_image + rva::gamestate_global, fake_state);
        mem.map(fake_state, 0x600);
        mem.put(fake_state + 0x310, fake_list);
        mem.put(fake_state + 0x31C, std::int32_t{3});
        mem.put(fake_state + 0x524, std::int32_t{1});  // the player plays POL
        mem.map(fake_list, 3 * 8);
        mem.map(fake_view, 0x600);
        mem.put(fake_view + 0x5F0, fake_idler);
        mem.map(fake_idler, 0x10);
        mem.put(fake_idler, fake_image + rva::vt_ingame_idler);
        map_group(mem, neutrality, "neutrality");
        map_group(mem, fascism, "fascism");
        map_character(mem, leader_a, 101);
        map_character(mem, leader_b, 102);
        map_party(mem, pol_party, neutrality, leader_a);
        map_party(mem, pol_party_2, fascism, leader_a);
        map_party(mem, ger_party, fascism, leader_b);
        map_country(mem, pol, 1, name_variables(U"Sanacja"), pol_party);
        map_country(mem, ger, 2, {{"other", fp(1)}}, ger_party);
        mem.put(fake_list + 8, pol);
        mem.put(fake_list + 16, ger);
        sub_layer.originals().panel = &fake_panel;
        sub_layer.activate(fake_image, nullptr);
    }

    /// @brief Replaces a country's variables. @param address Country. @param vars Variables.
    void set_vars(uptr address, const Vars& vars) {
        check(build_table(mem, address + 0x10'0000, address + 0x11'0000, vars), "rebuild table");
    }

    /// @brief Sets a country's ruling party. @param address Country. @param party Party.
    void set_party(uptr address, uptr party) { mem.put(address + 0x20'0000 + 0xD0, party); }

    /**
     * @brief Runs the panel hook once.
     * @return Text the panel got for its ideology key.
     */
    std::string panel() {
        sub_layer.panel(reinterpret_cast<void*>(fake_view));
        return panel_texts.empty() ? std::string{} : panel_texts.back();
    }
};

// ---- Decode ---------------------------------------------------------------------------------

void decode_tests() {
    const auto read = [](const Vars& vars) {
        FakeMemory mem;
        map_country(mem, 0x1000'0000, 1, vars, 0);
        return sub::read_ideology_layer(mem, 0x1000'0000);
    };
    auto got = read(name_variables(U"Sanacja", fp(7)));
    check(got.text.status == layer::Status::valid && got.text.view() == "Sanacja" && got.rev == fp(7) &&
              got.text.name_points == 7,
          "valid: text and revision");
    got = read(name_variables(U"Zażółć", fp(1)));
    check(got.text.status == layer::Status::valid && got.text.view() == "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87",
          "valid: UTF-8 of the extended alphabet");
    got = read(name_variables(U"Sanacja", -1));
    check(got.text.status == layer::Status::valid && got.rev == 0, "revision missing: 0");
    check(read({{"other", fp(1)}}).text.status == layer::Status::absent, "no capie_sn_ver: absent");
    const auto base = name_variables(U"Abc");
    const std::pair<const char*, Vars> invalid[] = {
        {"ver 2", with(base, "capie_sn_ver", fp(2))},
        {"ver fraction", with(base, "capie_sn_ver", fp(1) + 1)},
        {"count missing", without(base, "capie_sn_n^num")},
        {"count 0", with(base, "capie_sn_n^num", 0)},
        {"count 33", with(base, "capie_sn_n^num", fp(33))},
        {"count larger than the elements", with(base, "capie_sn_n^num", fp(4))},
        {"code 0", with(base, "capie_sn_n^1", 0)},
        {"code 265", with(base, "capie_sn_n^1", fp(265))},
        {"code fraction", with(base, "capie_sn_n^1", fp(3) + 1)},
    };
    for (const auto& [label, vars] : invalid) {
        const auto bad = read(vars);
        check(bad.text.status == layer::Status::invalid && bad.text.key_size == 0 && bad.rev == 0,
              std::string("invalid: ") + label);
    }
    check(read(name_variables(std::u32string(32, U'a'))).text.status == layer::Status::valid, "32 codes valid");
    {
        FakeMemory mem;
        mem.map(0x1000'0000, 0x400);
        mem.put(0x1000'0000 + 0x218, uptr{0x6000'0000});  // container pointer to unmapped memory
        check(sub::read_ideology_layer(mem, 0x1000'0000).text.status == layer::Status::unreadable,
              "container unreadable: unreadable");
        FakeMemory none;
        none.map(0x1000'0000, 0x400);
        check(sub::read_ideology_layer(none, 0x1000'0000).text.status == layer::Status::absent,
              "no container: absent");
    }
    check(std::string(sub::commit_variable) == "capie_sn_rev" && sub::version_variable == "capie_sn_ver" &&
              sub::name_array == "capie_sn_n",
          "variable names of CONTRACT 20");
}

// ---- Binding --------------------------------------------------------------------------------

void binding_tests() {
    Fixture f;
    sub::Binding binding{};
    check(sub::read_binding(f.mem, fake_image, pol, binding) && binding.view() == "neutrality" &&
              binding.leader == leader_a && binding.leader_id == 101,
          "binding: token, leader and id");
    sub::Binding other{};
    check(sub::read_binding(f.mem, fake_image, ger, other) && other.view() == "fascism" && !other.same(binding),
          "another government differs");
    sub::Binding again{};
    check(sub::read_binding(f.mem, fake_image, pol, again) && again.same(binding), "same government is same");
    {
        sub::Binding changed = binding;
        changed.leader_id = 7;
        check(!changed.same(binding), "a different leader id differs");
        changed = binding;
        changed.leader = leader_b;
        check(!changed.same(binding), "a different leader differs");
    }
    // Class checks of the chain.
    f.mem.put(pol_party, fake_image + 0x1234);
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "party with another vtable: no binding");
    f.mem.put(pol_party, fake_image + rva::vt_political_party);
    f.mem.put(neutrality, fake_image + rva::vt_country);
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "ideology with another vtable: no binding");
    f.mem.put(neutrality, fake_image + rva::vt_ideology_group);
    f.mem.put(leader_a, fake_image + 0x1234);
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "leader with another vtable: no binding");
    f.mem.put(leader_a, fake_image + rva::vt_character);
    f.set_party(pol, 0);
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "no ruling party: no binding");
    f.set_party(pol, pol_party);
    f.mem.put(pol_party + 0x18, uptr{0});
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "party without ideology: no binding");
    f.mem.put(pol_party + 0x18, neutrality);
    f.mem.put(pol + 0x20'0000 + 0xD0, uptr{0x6000'0000});
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "unreadable party: no binding");
    f.set_party(pol, pol_party);
    // Leaderless party: bound with leader 0.
    f.mem.put(pol_party + 0x7C, std::int32_t{0});
    check(sub::read_binding(f.mem, fake_image, pol, binding) && binding.leader == 0 && binding.leader_id == 0,
          "party without a leader: bound to leader 0");
    f.mem.put(pol_party + 0x7C, std::int32_t{1});
    // Token bounds and storage.
    map_group(f.mem, neutrality, "");
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "empty token: no binding");
    map_group(f.mem, neutrality, std::string(63, 'x'));
    check(sub::read_binding(f.mem, fake_image, pol, binding) && binding.key_size == 63 &&
              binding.view() == std::string(63, 'x') && binding.key[63] == '\0',
          "63-character token on the heap");
    map_group(f.mem, neutrality, std::string(64, 'x'));
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "64-character token: no binding");
    map_group(f.mem, neutrality, "communism_long_x", 0);
    check(sub::read_binding(f.mem, fake_image, pol, binding) && binding.view() == "communism_long_x",
          "16-character token read through the heap pointer");
    map_group(f.mem, neutrality, "neutrality");
    f.mem.put(neutrality + 0x30, std::uint64_t{5});
    check(!sub::read_binding(f.mem, fake_image, pol, binding), "size above capacity: no binding");
}

// ---- Hooks ----------------------------------------------------------------------------------

void hook_tests() {
    {
        Fixture f;
        check(f.panel() == "GAME" && f.sub_layer.entries() == 0, "no entry: the game's text");
        check(f.sub_layer.update(pol) && f.sub_layer.entries() == 1 && f.sub_layer.text_of(pol) == "Sanacja",
              "UpdateNames binds POL");
        check(!f.sub_layer.update(ger) && f.sub_layer.entries() == 1, "GER has no layer: no entry");
        check(f.panel() == "Sanacja", "panel: the layer text for the ideology key");
        check(!other_texts.empty() && other_texts.back() == "GAME", "panel: another key passes through");
        const std::string key = "neutrality";
        check(country::ideology_frame == nullptr && country::lookup_n(key.data(), static_cast<int>(key.size())) ==
                                                         std::string_view(game_text),
              "outside the writer: the game's text, frame cleared");
        panel_key = "neutralit";
        check(f.panel() == "GAME", "a prefix of the key passes through");
        panel_key = "neutralityx";
        check(f.panel() == "GAME", "a longer key passes through");
        panel_key = "Neutrality";
        check(f.panel() == "GAME", "another key of the same length passes through");
        panel_key = "neutrality";
        // Another thread during the frame sees nothing.
        std::string seen;
        country::IdeologyFrame frame{key.data(), key.size(), "X"};
        country::ideology_frame = &frame;
        std::thread([&seen, &key] { seen = country::lookup_n(key.data(), static_cast<int>(key.size())); }).join();
        country::ideology_frame = nullptr;
        check(seen == "GAME", "another thread: the game's text");
        check(country::ideology_answer(nullptr, 3) == nullptr && country::ideology_answer(key.data(), -1) == nullptr,
              "no frame, null or negative: no answer");
    }
    {
        Fixture f;
        static_cast<void>(f.sub_layer.update(pol));
        // Shown tag: +0x520 wins when > 0, else +0x524.
        f.mem.put(fake_state + 0x520, std::int32_t{2});
        check(f.panel() == "GAME", "shown tag GER (observer view): POL's name not shown");
        f.mem.put(fake_state + 0x520, std::int32_t{0});
        f.mem.put(fake_state + 0x524, std::int32_t{2});
        check(f.panel() == "GAME", "player tag GER: POL's name not shown");
        f.mem.put(fake_state + 0x520, std::int32_t{1});
        check(f.panel() == "Sanacja", "shown tag POL via +0x520");
        f.mem.put(fake_state + 0x520, std::int32_t{-1});
        f.mem.put(fake_state + 0x524, std::int32_t{1});
        check(f.panel() == "Sanacja", "negative +0x520: the player tag");
        // The view's idler must be the in-game idler.
        f.mem.put(fake_idler, fake_image + rva::vt_character);
        check(f.panel() == "GAME", "idler of another class: the game's text");
        f.mem.put(fake_idler, fake_image + rva::vt_ingame_idler);
        f.mem.put(fake_view + 0x5F0, uptr{0});
        check(f.panel() == "GAME", "no idler: the game's text");
        f.mem.put(fake_view + 0x5F0, fake_idler);
        // The entry's country must still be in its slot with its tag.
        f.mem.put(fake_list + 8, ger);
        check(f.panel() == "GAME", "slot holds another country: the game's text");
        f.mem.put(fake_list + 8, pol);
        f.mem.put(fake_state + 0x31C, std::int32_t{1});
        check(f.panel() == "GAME", "slot index not below the count: the game's text");
        f.mem.put(fake_state + 0x31C, std::int32_t{3});
        f.mem.put(pol + 0x8, std::int32_t{2});
        check(f.panel() == "GAME", "country tag changed: the game's text");
        f.mem.put(pol + 0x8, std::int32_t{1});
        check(f.panel() == "Sanacja", "restored: shown again");
        f.sub_layer.deactivate();
        check(f.panel() == "GAME", "disabled: the game's text");
    }
    {
        // Lapse on a party change, kept by UpdateNames, re-bound by a new revision.
        Fixture f;
        static_cast<void>(f.sub_layer.update(pol));
        f.set_party(pol, pol_party_2);  // set_politics / election: another ideology
        panel_key = "fascism";
        check(f.panel() == "GAME", "party changed: lapsed");
        check(f.sub_layer.update(pol) && f.panel() == "GAME", "UpdateNames keeps the old binding: still lapsed");
        f.sub_layer.refresh_all();
        check(f.panel() == "GAME", "commit with the same revision: still lapsed");
        f.set_vars(pol, name_variables(U"Sanacja", fp(2)));
        const auto stats = f.sub_layer.refresh_all();
        check(stats.list_ok && stats.countries == 2 && stats.layers == 1 && stats.bound == 1 && stats.kept == 0,
              "commit with a new revision: re-bound");
        check(f.panel() == "Sanacja", "new revision: shown for the new party");
        f.set_party(pol, pol_party);
        panel_key = "neutrality";
        check(f.panel() == "GAME", "back to the first party: lapsed (bound to the second)");
    }
    {
        // Lapse on a leader change (coup: same party, new leader and sub-ideology).
        Fixture f;
        static_cast<void>(f.sub_layer.update(pol));
        f.mem.put(pol_party + 0xC0 + 0x8, leader_b);
        check(f.panel() == "GAME", "leader changed: lapsed");
        f.mem.put(pol_party + 0xC0 + 0x8, leader_a);
        check(f.panel() == "Sanacja", "leader back: shown");
        f.mem.put(leader_a + 0xC, std::int32_t{555});
        check(f.panel() == "GAME", "leader id changed: lapsed");
        f.mem.put(leader_a + 0xC, std::int32_t{101});
        f.mem.put(pol_party + 0x7C, std::int32_t{0});
        check(f.panel() == "GAME", "leader removed: lapsed");
        f.mem.put(pol_party + 0x7C, std::int32_t{1});
        f.mem.put(neutrality, fake_image + 0x1234);
        check(f.panel() == "GAME", "unreadable government at the panel: the game's text");
        f.mem.put(neutrality, fake_image + rva::vt_ideology_group);
        check(f.panel() == "Sanacja", "restored: shown");
    }
    {
        // Text change re-binds at once; removal drops the entry.
        Fixture f;
        static_cast<void>(f.sub_layer.update(pol));
        f.set_party(pol, pol_party_2);
        f.set_vars(pol, name_variables(U"Obóz", fp(1)));
        panel_key = "fascism";
        check(f.sub_layer.update(pol) && f.sub_layer.text_of(pol) == "Ob\xC3\xB3z" && f.panel() == "Ob\xC3\xB3z",
              "text changed: re-bound to the current government");
        f.set_vars(pol, {{"other", fp(1)}});
        check(!f.sub_layer.update(pol) && f.sub_layer.entries() == 0 && f.panel() == "GAME",
              "layer removed: entry dropped");
        f.set_vars(pol, name_variables(U"Sanacja"));
        f.set_party(pol, 0);
        check(!f.sub_layer.update(pol) && f.sub_layer.entries() == 0, "no ruling party: no entry");
        f.set_party(pol, pol_party_2);
        check(f.sub_layer.update(pol) && f.sub_layer.entries() == 1, "ruling party back: bound");
        const auto stats = f.sub_layer.refresh_all();
        check(stats.kept == 1 && stats.bound == 0 && f.sub_layer.entries() == 1, "commit, nothing changed: kept");
        f.set_vars(pol, {{"other", fp(1)}});
        check(f.sub_layer.refresh_all().layers == 0 && f.sub_layer.entries() == 0 && f.panel() == "GAME",
              "commit after removal: entry dropped");
    }
    {
        // A country that is not in the list is not bound; another object in the same slot gets its own entry.
        Fixture f;
        map_country(f.mem, 0x2200'0000, 1, name_variables(U"Fake"), pol_party);
        check(!f.sub_layer.update(0x2200'0000) && f.sub_layer.entries() == 0, "country not in the list: no entry");
        check(!f.sub_layer.update(0) && f.sub_layer.entries() == 0, "null country: no entry");
        f.mem.put(fake_list + 16, uptr{0x2200'0000});
        f.mem.put(uptr{0x2200'0000} + 0x8, std::int32_t{5});  // its tag is not its index: found by the scan
        check(f.sub_layer.update(0x2200'0000) && f.sub_layer.entries() == 1, "found by the list scan");
        f.mem.put(fake_state + 0x524, std::int32_t{5});
        check(f.panel() == "Fake", "entry found by the scan: shown for its own tag");
    }
    {
        // Inactive layer, missing trampoline, implausible list.
        Fixture f;
        sub::IdeologyLayer<FakeMemory> idle{f.mem};
        idle.activate(fake_image, nullptr);
        check(!idle.enabled() && !idle.update(pol), "no trampoline: stays off");
        f.mem.put(fake_state + 0x31C, std::int32_t{4097});
        check(!f.sub_layer.update(pol) && !f.sub_layer.refresh_all().list_ok, "country count above 4096: refused");
        f.mem.put(fake_state + 0x31C, std::int32_t{-1});
        check(!f.sub_layer.update(pol), "negative country count: refused");
        f.mem.put(fake_state + 0x31C, std::int32_t{3});
        f.mem.put(fake_image + rva::gamestate_global, uptr{0});
        check(!f.sub_layer.update(pol) && !f.sub_layer.refresh_all().list_ok, "no game state: refused");
    }
    {
        // The frame is restored when the original throws (scan N6).
        Fixture f;
        static_cast<void>(f.sub_layer.update(pol));
        panel_throws = true;
        bool thrown = false;
        try {
            f.sub_layer.panel(reinterpret_cast<void*>(fake_view));
        } catch (const std::runtime_error&) {
            thrown = true;
        }
        const std::string key = "neutrality";
        check(thrown && panel_texts.back() == "Sanacja" && country::ideology_frame == nullptr &&
                  country::lookup_n(key.data(), static_cast<int>(key.size())) == std::string_view(game_text),
              "exception from the original passes through, frame restored");
    }
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        decode_tests();
        binding_tests();
        hook_tests();
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("ideology name layer tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
