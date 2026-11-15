#include "../compat/rn_country.hpp"
#include <cassert>
#include <iostream>

int main() {
    auto name = rn::decode_country_key("RN1|Rzeczpospolita Testowa|testowy|");
    assert(name && name->name == "Rzeczpospolita Testowa" && !name->adjective_key);
    auto adj = rn::decode_country_key("RN1|Rzeczpospolita Testowa|testowy|_ADJ");
    assert(adj && adj->adjective_key && adj->adjective == "testowy");
    auto def = rn::decode_country_key("RN1|Polska|polski|_DEF");
    assert(def && !def->adjective_key);
    assert(rn::decode_country_key("RN1|\xc4\x85\xc4\x99\xc5\x82\xc5\xbc|testowy|"));
    assert(!rn::decode_country_key("POL_neutrality"));
    assert(!rn::decode_country_key("RN1||testowy|"));
    assert(!rn::decode_country_key("RN1|Polska||"));
    assert(!rn::decode_country_key("RN1|Polska|polski|_ADJ!"));
    assert(!rn::decode_country_key("RN1|Polska|polski|_ADJ|extra"));
    assert(!rn::valid_text("[ROOT.GetName]"));
    assert(!rn::valid_text("a\n"));
    assert(!rn::valid_text(std::string(65, 'a')));
    assert(rn::valid_text(std::string(64, 'a')));
    assert(rn::valid_text(std::string(32, 'a')));
    // 64 four-byte code points are 256 bytes and valid; 65 are not.
    assert(rn::valid_text([] { std::string s; for (int i = 0; i < 64; ++i) s += "\xF0\x9F\x98\x80"; return s; }()));
    assert(!rn::valid_text([] { std::string s; for (int i = 0; i < 65; ++i) s += "\xF0\x9F\x98\x80"; return s; }()));
    // 64 Polish letters (2 bytes each).
    assert(rn::valid_text([] { std::string s; for (int i = 0; i < 64; ++i) s += "\xC5\xBB"; return s; }()));
    assert(!rn::valid_text([] { std::string s; for (int i = 0; i < 65; ++i) s += "\xC5\xBB"; return s; }()));
    assert(!rn::decode_country_key("RN1|Polska|polski"));
    assert(!rn::valid_text("\xc0\xaf"));
    assert(!rn::valid_text("\xed\xa0\x80"));
    assert(!rn::valid_text("\xf4\x90\x80\x80"));
    assert(!rn::valid_text("\xe2\x82"));
    // Country-specific decisions and other content can probe cosmetic-tag prefixes.
    // They must fall through to vanilla, rather than claiming every suffix exists.
    constexpr std::string_view unrelated[] = {
        "_POL_peasants_strike", "_POL_peasants_strike_desc",
        "_anti_fascist_raids", "_anti_fascist_raids_desc", "_ban_fascism",
        "_fascism", "_neutrality", "_neutrality_ADJ", "_neutrality_DEF",
        "_focus", "_focus_desc", "ADJ", "_ADJ_extra"
    };
    for (const auto suffix : unrelated) {
        if (rn::decode_country_key(std::string("RN1|Polska Testowa|testowy|") + std::string(suffix))) {
            std::cerr << "FAIL: unrelated localization key accepted: " << suffix << '\n';
            return 1;
        }
    }
    std::cout << "country protocol: 18 field checks + 13 key-boundary checks passed\n";
}
