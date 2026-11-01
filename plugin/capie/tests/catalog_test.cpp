/**
 * @file catalog_test.cpp
 * @brief Offline tests of the API 9 catalog views: names, VP view, thread guard, records, label store.
 *
 * A fake memory holds the game-state global, a state array by id with `CState` objects, their
 * province arrays with `CProvince` objects (VP value +0x38, id +0xA4), a scope with a state id at
 * +0xA8 and a fake TLS block for `ThreadForbidCount`. No game, no EXE (spec 12.2 items 5, 7, 8, 10).
 */
#include "../src/features/catalog/catalog_names.hpp"
#include "../src/features/catalog/catalog_records.hpp"
#include "../src/features/catalog/catalog_text.hpp"
#include "../src/features/catalog/vp_view.hpp"
#include "../src/features/country/country_names.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

namespace names = capie::catalog::names;
namespace vp = capie::catalog::vp;
namespace records = capie::catalog::records;
namespace text = capie::catalog::text;
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

// Addresses and values are literal test data.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// @brief Byte-addressed fake memory made of separate regions.
struct FakeMemory {
    std::map<uptr, std::vector<unsigned char>> regions;  ///< Start -> bytes.

    /**
     * @brief Reads when one region holds all bytes.
     * @param address Address.
     * @param out Destination.
     * @param size Bytes.
     * @return `false` when unmapped.
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
        std::memcpy(out, found->second.data() + offset, size);
        return true;
    }
    /// @brief Maps a zeroed region. @param address Start. @param size Bytes.
    void map(uptr address, std::size_t size) { regions[address].assign(size, 0); }
    /// @brief Writes a value. @param address Address. @param value Value.
    template <class T>
    void put(uptr address, const T& value) {
        auto found = regions.upper_bound(address);
        --found;
        std::memcpy(found->second.data() + (address - found->first), &value, sizeof(T));
    }
};

constexpr uptr image = 0x1'4000'0000;
constexpr uptr fake_gamestate = 0x5000'0000;
constexpr uptr state_array = 0x5100'0000;

/**
 * @brief Maps the game state with `slots` state slots.
 * @param mem Memory.
 * @param slots Slots (index = state id; slot 0 empty).
 */
void map_fake_gamestate(FakeMemory& mem, std::int32_t slots) {
    mem.map(image + capie::core::rva::gamestate_global, 8);
    mem.put(image + capie::core::rva::gamestate_global, fake_gamestate);
    mem.map(fake_gamestate, 0x400);
    mem.put(fake_gamestate + 0x2C8, state_array);
    mem.put(fake_gamestate + 0x2D4, slots);
    mem.map(state_array, static_cast<std::size_t>(slots) * 8);
}

/**
 * @brief Maps a state with provinces `{id, vp}`.
 * @param mem Memory.
 * @param id State id.
 * @param provinces Provinces.
 * @return State address.
 */
uptr map_state(FakeMemory& mem, std::int32_t id, const std::vector<std::pair<std::int32_t, std::int32_t>>& provinces) {
    const uptr state = 0x6000'0000 + (static_cast<uptr>(id) * 0x10'0000);
    const uptr array = state + 0x1000;
    mem.map(state, 0x100);
    mem.put(state, image + capie::core::rva::vt_state);
    mem.put(state + 0x18, array);
    mem.put(state + 0x24, static_cast<std::int32_t>(provinces.size()));
    mem.map(array, (provinces.size() * 8) + 8);
    for (std::size_t i = 0; i < provinces.size(); ++i) {
        const uptr province = state + 0x2000 + (i * 0x200);
        mem.map(province, 0x100);
        mem.put(province, image + capie::core::rva::vt_province);
        mem.put(province + 0x38, provinces[i].second);
        mem.put(province + 0xA4, provinces[i].first);
        mem.put(array + (i * 8), province);
    }
    mem.put(state_array + (static_cast<uptr>(id) * 8), state);
    return state;
}

void name_tests() {
    const auto id = names::parse_model("capie_v1_cat__rn__vp");
    check(id && id->mod_view() == "rn" && id->editor_view() == "vp", "model parses");
    check(names::view_model(*id).view() == "capie_v1_cat__rn__vp", "model rebuilt");
    check(names::view_action(*id, names::Action::vp).view() == "capie_v1_cat__rn__vp__vp_click", "vp selector");
    check(names::view_action(*id, names::Action::portraitown).view() == "capie_v1_cat__rn__vp__portraitown_click",
          "portraitown selector");
    for (const char* bad : {"capie_v1_cat__RN__vp", "capie_v1_cat__rn", "capie_v1_cat__rn__vp__x", "capie_v1_ed__rn__vp",
                            "capie_v1_cat__rn__vp ", "capie_v1_cat____vp", "capie_v1_cat__abcdefghijklm__vp"}) {
        check(!names::parse_model(bad), std::string("model refused: ") + bad);
    }
    check(names::parse_action(*id, "capie_v1_cat__rn__vp__vp_click") == names::Action::vp, "vp action");
    check(names::parse_action(*id, "capie_v1_cat__rn__vp__pick_click") == names::Action::pick, "pick action");
    check(names::parse_action(*id, "capie_v1_cat__rn__vp__next_click") == names::Action::next, "next action");
    for (const char* bad : {"capie_v1_cat__rn__vp__VP_click", "capie_v1_cat__rn__vp__vp", "capie_v1_cat__rn__fp__vp_click",
                            "capie_v1_cat__rn__vp_vp_click", "capie_v1_cat__rn__vpxxvp_click", "capie_v1_cat__rn__vp__zoom_click", "capie_v1_cat__rn__vp__vp_click "}) {
        check(!names::parse_action(*id, bad), std::string("action refused: ") + bad);
    }
    check(names::row_key(*id, 3).view() == "CAPIE_V1_T__RN__VP__3", "row key upper case");
    check(names::row_key(*id, 31).view() == "CAPIE_V1_T__RN__VP__31", "row key 31");
    check(names::text_key(*id, "COUNT").view() == "CAPIE_V1_T__RN__VP__COUNT", "count key");
    const auto longest = names::parse_model("capie_v1_cat__abcdefghijkl__abcdefghijkl");
    check(longest && names::text_key(*longest, "COUNT").size == names::max_key &&
              names::view_action(*longest, names::Action::portraitown).size == names::max_selector,
          "longest names fit");
    check(names::may_be_model("capie_v1_cat__a__b") && !names::may_be_model("capie_v1_ed__a__b"), "pre-filter");
}

void vp_tests() {
    FakeMemory mem;
    map_fake_gamestate(mem, 1200);
    // State 1000 (KaiserreduX-like ids above vanilla's 1081): VPs out of order, a VP 0, equal values.
    map_state(mem, 1000, {{10472, 10}, {13671, 25}, {500, 0}, {12, 10}, {9, 1}});
    vp::List list{};
    check(vp::read_state(mem, image, 1000, list) == vp::Read::ok && list.state == 1000 && list.count == 4,
          "state 1000: four VPs (VP 0 excluded)");
    check(list.rows[0].id == 13671 && list.rows[1].id == 12 && list.rows[2].id == 10472 && list.rows[3].id == 9,
          "order: VP value descending, then province id");
    check(list.rows[0].value == 25 && list.rows[3].value == 1 && list.rows[0].province != 0, "values and objects");
    // A state with 40 VPs: the best 32 are kept.
    std::vector<std::pair<std::int32_t, std::int32_t>> many;
    for (std::int32_t i = 0; i < 40; ++i) {
        many.emplace_back(100 + i, 1 + i);
    }
    map_state(mem, 7, many);
    check(vp::read_state(mem, image, 7, list) == vp::Read::ok && list.count == 32 && list.dropped == 8 &&
              list.rows[0].value == 40 && list.rows[31].value == 9,
          "32 cap: the highest 32 VPs, sorted");
    map_state(mem, 8, {{1, 0}, {2, 0}});
    check(vp::read_state(mem, image, 8, list) == vp::Read::ok && list.count == 0, "no VP: an empty view");
    check(vp::read_state(mem, image, 0, list) == vp::Read::no_state && list.count == 0, "state 0: no state");
    check(vp::read_state(mem, image, 1200, list) == vp::Read::no_state, "id past the array: no state");
    check(vp::read_state(mem, image, 3, list) == vp::Read::unreadable, "empty slot: unreadable");
    const uptr bad = map_state(mem, 9, {{1, 5}});
    mem.put(bad, image + 0x1234);
    check(vp::read_state(mem, image, 9, list) == vp::Read::unreadable, "not a CState: unreadable");
    const uptr wrong_province = map_state(mem, 10, {{1, 5}, {2, 6}});
    mem.put(wrong_province + 0x2200, image + capie::core::rva::vt_state);
    check(vp::read_state(mem, image, 10, list) == vp::Read::unreadable && list.count == 0,
          "a province that is not a CProvince: unreadable, nothing kept");
    const uptr huge = map_state(mem, 11, {{1, 5}});
    mem.put(huge + 0x24, std::int32_t{5000});
    check(vp::read_state(mem, image, 11, list) == vp::Read::unreadable, "implausible province count");
    mem.put(fake_gamestate + 0x2D4, std::int32_t{0});
    check(vp::read_state(mem, image, 1000, list) == vp::Read::unreadable, "no state array");
    mem.put(fake_gamestate + 0x2D4, std::int32_t{1200});
    // The scope's state id.
    mem.map(0x7000'0000, 0x200);
    mem.put(0x7000'0000 + 0xA8, std::int32_t{1000});
    std::int32_t state = 0;
    check(vp::scope_state(mem, 0x7000'0000, state) && state == 1000, "scope +0xA8: the state id");
    mem.put(0x7000'0000 + 0xA8, std::int32_t{-1});
    check(vp::scope_state(mem, 0x7000'0000, state) && state == 0, "negative id: no state");
    check(!vp::scope_state(mem, 0x7100'0000, state), "unmapped scope");
}

void thread_tests() {
    FakeMemory mem;
    mem.map(image + capie::core::rva::tls_index, 4);
    mem.put(image + capie::core::rva::tls_index, std::uint32_t{3});
    mem.map(0x8000'0000, 64);
    mem.put(0x8000'0000 + (3 * 8), uptr{0x8100'0000});
    mem.map(0x8100'0000, 0x40);
    mem.put(0x8100'0000 + 0x10, std::int32_t{0});
    std::int32_t count = 7;
    check(vp::thread_forbid_count(mem, image, 0x8000'0000, count) && count == 0, "ThreadForbidCount 0");
    mem.put(0x8100'0000 + 0x10, std::int32_t{2});
    check(vp::thread_forbid_count(mem, image, 0x8000'0000, count) && count == 2, "ThreadForbidCount 2 (+0x10)");
    check(!vp::thread_forbid_count(mem, image, 0, count) && count == -1, "no TLS array: unreadable");
    mem.put(0x8000'0000 + (3 * 8), uptr{0});
    check(!vp::thread_forbid_count(mem, image, 0x8000'0000, count), "no TLS block: unreadable");
}

void record_tests() {
    vp::List list{};
    list.state = 1000;
    list.count = 2;
    list.rows[0] = {0x1, 13671, 25};
    list.rows[1] = {0x2, 10472, 10};
    const auto got = records::vp_view(list);
    std::int64_t value = 0;
    const auto has = [&got, &value](const char* name, std::int64_t expected) {
        return got.find(name, value) && value == expected;
    };
    check(!got.overflow && got.count == 10 + 1 + 4, "vp: 15 records");
    check(has("capie_cap", static_cast<std::int64_t>(capie::core::capie_api_level)) &&
              has("capie_ver", static_cast<std::int64_t>(capie::core::capie_build_number)) && has("capie_catalog", 1) &&
              has("capie_cat", 3) && has("capie_cn", 2) && has("capie_pg", 1) && has("capie_pgs", 1) &&
              has("capie_pn", 2) && has("capie_par", 0) && has("capie_ppar", 0) && has("capie_st", 1000),
          "vp header records");
    check(has("capie_vp_0", 13671) && has("capie_vpv_0", 25) && has("capie_vp_1", 10472) && has("capie_vpv_1", 10) &&
              !got.find("capie_vp_2", value),
          "vp rows in view order");
    vp::List full{};
    full.state = 7;
    full.count = 32;
    for (std::size_t k = 0; k < 32; ++k) {
        full.rows[k] = {k + 1, static_cast<std::int32_t>(k), 1};
    }
    const auto big = records::vp_view(full);
    check(!big.overflow && big.count == 75 && big.find("capie_vpv_31", value), "32 rows: 75 records, names fit");
    for (std::size_t i = 0; i < big.count; ++i) {
        check(std::strlen(big.items[i].name.data()) <= 15, "record name fits the short string");
    }
    vp::List empty{};
    const auto none = records::vp_view(empty);
    check(none.find("capie_cn", value) && value == 0 && none.find("capie_pg", value) && value == 0 &&
              none.find("capie_st", value) && value == 0,
          "empty view: cn 0, page 0 of 0, st 0");
    const auto sentinel = records::sentinel(records::Sentinel::thread);
    check(sentinel.count == 3 && sentinel.find("capie_catalog", value) && value == -5, "sentinel -5: three records");
    check(records::sentinel(records::Sentinel::window).find("capie_catalog", value) && value == -1, "sentinel -1");
    check(records::sentinel(records::Sentinel::registry_full).find("capie_catalog", value) && value == -2,
          "sentinel -2");
    const auto off = records::unavailable();
    check(off.count == 3 && off.find("capie_catalog", value) && value == 0 && !off.find("capie_cn", value),
          "unavailable: capie_catalog 0 and nothing below");
    records::List overflow{};
    overflow.add("capie_this_is_too_long", 1);
    check(overflow.overflow && overflow.count == 0, "a long name overflows");
}

void text_tests() {
    text::Store store;
    check(store.answer("CAPIE_V1_T__RN__VP__0") == nullptr, "unpublished key: original lookup");
    check(store.publish("CAPIE_V1_T__RN__VP__0", "Berlin"), "publish");
    const char* first = store.answer("CAPIE_V1_T__RN__VP__0");
    check(first != nullptr && std::string(first) == "Berlin", "answer");
    check(store.publish("CAPIE_V1_T__RN__VP__1", "Berlin") && store.answer("CAPIE_V1_T__RN__VP__1") == first,
          "same text interned once");
    check(store.publish("CAPIE_V1_T__RN__VP__0", "Breslau") && std::string(store.answer("CAPIE_V1_T__RN__VP__0")) ==
                                                                   "Breslau" &&
              std::string(first) == "Berlin",
          "a new text swaps the pointer; the old pointer stays valid");
    check(!store.publish("RN1|x|y|", "x") && !store.publish("CAPIE_STATUS", "x"), "only catalog keys");
    check(text::clean("A$B[C]D\nE") == "A B C D E", "markup and control characters become spaces");
    check(text::clean("Kraków") == "Kraków", "UTF-8 kept");
    const std::string long_text(300, 'x');
    check(text::clean(long_text).size() == 255, "cut at 255 bytes");
    std::string utf(254, 'x');
    utf += "\xC3\xB3";
    check(text::clean(utf).size() == 254, "the cut does not split a UTF-8 sequence");
}


void hook_answer_tests() {
    namespace country = capie::country;
    country::active.store(true);
    check(country::override_value("CAPIE_V1_T__ZZ__QQ__0") == nullptr, "loc hook: an unregistered view goes to the original");
    check(text::store().publish("CAPIE_V1_T__ZZ__QQ__0", "Warszawa"), "publish through the process store");
    const char* got = country::override_value("CAPIE_V1_T__ZZ__QQ__0");
    check(got != nullptr && std::string(got) == "Warszawa", "loc hook answers a published key");
    check(country::lookup_n != nullptr && country::may_claim("CAPIE_V1_T__ZZ__QQ__0", 21), "the key passes the claim prefix");
    check(std::string(country::override_value("CAPIE_API")) == capie::core::capie_api_text, "status keys unchanged");
    country::active.store(false);
    check(country::override_value("CAPIE_V1_T__ZZ__QQ__0") == nullptr, "inactive core: no answer");
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        name_tests();
        vp_tests();
        thread_tests();
        record_tests();
        text_tests();
        hook_answer_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("catalog tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
