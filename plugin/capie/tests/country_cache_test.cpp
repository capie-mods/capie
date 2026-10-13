/**
 * @file country_cache_test.cpp
 * @brief Differential test of the optimized country lookups against the accepted logic.
 *
 * `legacy_override` is the accepted country-status candidate `override_value` (country.cpp
 * 689924A8 + country_status.patch), copied verbatim as a reference model. For a corpus of
 * keys (protocol corpus, near-misses, status keys, boundary lengths, random bytes) every
 * hook body must give the same answer as the reference and must call the original exactly
 * once whenever it does not answer. Also checked: repeat lookups are served from the cache
 * with a stable pointer, `lookup_n` never reads past `length` (key at the end of a page
 * followed by PAGE_NOACCESS), inactive forwards everything. No hooks are installed.
 */
#include "../src/dll/capie_core.cpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace capie::country;  // NOLINT(google-build-using-namespace): test brevity

namespace {

int checks = 0;
int failures = 0;

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Description printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

// ---- Reference model: accepted override_value (country.cpp + country_status.patch) ---------
/**
 * @brief Interning map of the reference model (created on first use).
 * @return The map.
 */
std::unordered_map<std::string, std::unique_ptr<const std::string>>& legacy_cache() {
    static std::unordered_map<std::string, std::unique_ptr<const std::string>> cache;
    return cache;
}
/**
 * @brief Accepted `override_value`, verbatim apart from names.
 * @param key Key.
 * @return Answer or null.
 */
const char* legacy_override(std::string_view key) {
    if (key == "RN_PLUGIN_STATUS" || key == "RN_PLUGIN_STATUS_EN") {
        return "CAPIE 1.2 loaded";
    }
    if (key == "RN_PLUGIN_STATUS_PL") {
        return "CAPIE 1.2 załadowane";
    }
    auto decoded = decode_country_key(key);
    if (!decoded) {
        return nullptr;
    }
    const auto& value = decoded->adjective_key ? decoded->adjective : decoded->name;
    auto found = legacy_cache().find(value);
    if (found == legacy_cache().end()) {
        found = legacy_cache().emplace(value, std::make_unique<const std::string>(value)).first;
    }
    return found->second->c_str();
}
/**
 * @brief Accepted lookup_n gate (length 0..360, then override).
 * @param key Key bytes.
 * @param length Length.
 * @return Answer or null (= original).
 */
const char* legacy_n(const char* key, int length) {
    if (key && length >= 0 && length <= 360) {
        return legacy_override(std::string_view(key, static_cast<std::size_t>(length)));
    }
    return nullptr;
}
/**
 * @brief Accepted lookup_z gate (prefix/status, strnlen 360).
 * @param key Key.
 * @return Answer or null.
 */
const char* legacy_z(const char* key) {
    if (key && (std::strncmp(key, "RN1|", 4) == 0 || std::strcmp(key, "RN_PLUGIN_STATUS") == 0 ||
                std::strcmp(key, "RN_PLUGIN_STATUS_EN") == 0 || std::strcmp(key, "RN_PLUGIN_STATUS_PL") == 0)) {
        const auto n = strnlen_s(key, 361);
        if (n <= 360) {
            return legacy_override(std::string_view(key, n));
        }
    }
    return nullptr;
}
/**
 * @brief Accepted exists gate.
 * @param key Key.
 * @return `true` when the DLL claims the key.
 */
bool legacy_exists(const char* key) {
    if (key && std::strncmp(key, "RN1|", 4) == 0) {
        const auto n = strnlen_s(key, 361);
        if (n <= 360 && decode_country_key(std::string_view(key, n))) {
            return true;
        }
    }
    return false;
}

// ---- Fake originals -------------------------------------------------------------------------
int original_calls = 0;
const char original_text[] = "ORIGINAL";
const char* fake_n(const char*, int) {
    ++original_calls;
    return original_text;
}
const char* fake_z(const char*) {
    ++original_calls;
    return original_text;
}
bool fake_exists(const char*) {
    ++original_calls;
    return false;
}

/**
 * @brief Compares all three hook bodies with the reference for one key.
 * @param key Key.
 */
void compare(const std::string& key) {
    const auto* const expected_n = legacy_n(key.data(), static_cast<int>(key.size()));
    original_calls = 0;
    const auto* const actual_n = lookup_n(key.data(), static_cast<int>(key.size()));
    check(expected_n ? (actual_n && std::strcmp(actual_n, expected_n) == 0 && original_calls == 0)
                     : (actual_n == original_text && original_calls == 1),
          "lookup_n equivalence: " + key.substr(0, 60));
    const auto* const expected_z = legacy_z(key.c_str());
    original_calls = 0;
    const auto* const actual_z = lookup_z(key.c_str());
    check(expected_z ? (actual_z && std::strcmp(actual_z, expected_z) == 0 && original_calls == 0)
                     : (actual_z == original_text && original_calls == 1),
          "lookup_z equivalence: " + key.substr(0, 60));
    const bool expected_e = legacy_exists(key.c_str());
    original_calls = 0;
    const bool actual_e = exists(key.c_str());
    check(expected_e ? (actual_e && original_calls == 0) : (!actual_e && original_calls == 1),
          "exists equivalence: " + key.substr(0, 60));
}

}  // namespace

/**
 * @brief Entry point.
 * @return 0 when every check passed.
 */
int main() {
    original_n = fake_n;
    original_z = fake_z;
    original_exists = fake_exists;
    // Inactive: everything forwarded exactly once.
    original_calls = 0;
    check(lookup_n("RN_PLUGIN_STATUS", 16) == original_text && lookup_z("RN1|Polska|polski|") == original_text &&
              !exists("RN1|Polska|polski|") && original_calls == 3,
          "inactive forwards");
    active.store(true);
    std::vector<std::string> corpus = {
        "",
        "R",
        "RN",
        "RN1",
        "RN1|",
        "RN1||",
        "RN1|Polska|polski|",
        "RN1|Polska|polski|_ADJ",
        "RN1|Polska|polski|_DEF",
        "RN1|Polska|polski|_GER",
        "RN1|Polska|polski|_ADJ!",
        "RN1|Polska|polski",
        "RN1|\xc4\x85\xc4\x99\xc5\x82\xc5\xbc|testowy|",
        "RN1|Rzeczpospolita Testowa|testowy|",
        "RN1|Rzeczpospolita Testowa|testowy|_ADJ",
        "RN1||testowy|",
        "RN1|Polska||",
        "RN_PLUGIN_STATUS",
        "RN_PLUGIN_STATUS_EN",
        "RN_PLUGIN_STATUS_PL",
        "RN_PLUGIN_STATUS_DE",
        "RN_PLUGIN_STATUSX",
        "RN_PLUGIN_STATU",
        "RN_COUNTRY_PLUGIN",
        "POL_neutrality",
        "POL",
        "rn1|Polska|polski|",
        "XRN1|Polska|polski|",
        "RN1|[ROOT.GetName]|x|",
        "RN1|a\n|x|",
        "RN1|\xc0\xaf|x|",
        "RN1|\xed\xa0\x80|x|",
        "RN1|\xf4\x90\x80\x80|x|",
        "RN1|\xe2\x82|x|",
        "RN2|Polska|polski|",
        "RNX",
        "RN_",
        "RN1|Polska|polski|_neutrality_ADJ",
    };
    constexpr const char* unrelated[] = {"_POL_peasants_strike",
                                         "_POL_peasants_strike_desc",
                                         "_anti_fascist_raids",
                                         "_ban_fascism",
                                         "_fascism",
                                         "_neutrality",
                                         "_neutrality_ADJ",
                                         "_neutrality_DEF",
                                         "_focus",
                                         "_focus_desc",
                                         "ADJ",
                                         "_ADJ_extra"};
    for (const char* suffix : unrelated) {
        corpus.push_back(std::string("RN1|Polska Testowa|testowy|") + suffix);
    }
    corpus.push_back("RN1|" + std::string(32, 'a') + "|b|");
    corpus.push_back("RN1|" + std::string(33, 'a') + "|b|");
    corpus.push_back("RN1|a|" + std::string(355 - 7, 'b'));    // long, invalid field
    corpus.push_back("RN1|a|b|" + std::string(360 - 8, '_'));  // exactly 360
    corpus.push_back("RN1|a|b|" + std::string(361 - 8, '_'));  // 361: forwarded
    corpus.push_back("RN_PLUGIN_STATUS" + std::string(400, 'x'));
    // A fixed seed keeps the random corpus identical in every run.
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937 random(12345);
    const std::string alphabet = "RN1|_PLUGINSTATUSADJEFabc \xc5\x82\x01[$";
    for (int i = 0; i < 5000; ++i) {
        std::string key;
        if (i % 3 == 0) {
            key = "RN1|";
        } else if (i % 3 == 1) {
            key = "RN";
        }
        const auto length = random() % 40;
        for (unsigned j = 0; j < length; ++j) {
            key += alphabet[random() % alphabet.size()];
        }
        corpus.push_back(key);
    }
    for (const auto& key : corpus) {
        compare(key);
    }
    for (const auto& key : corpus) {
        compare(key);  // second pass: served from the cache
    }
    // Stable interned pointers and cache hits.
    const auto* const first = lookup_z("RN1|Polska|polski|_ADJ");
    const auto* const second = lookup_n("RN1|Polska|polski|_ADJ", 22);
    check(first != nullptr && first == second && std::strcmp(first, "polski") == 0, "cached pointer stable");
    // lookup_n must not read beyond `length`: key ends exactly at a PAGE_NOACCESS boundary.
    auto* pages = static_cast<char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old = 0;
    check(pages != nullptr && VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old) != 0, "guard page");
    const std::string tail = "RN1|Polska|polski|";
    std::memcpy(pages + 4096 - tail.size(), tail.data(), tail.size());
    const auto* const bounded = lookup_n(pages + 4096 - tail.size(), static_cast<int>(tail.size()));
    check(bounded != nullptr && std::strcmp(bounded, "Polska") == 0, "lookup_n bounded by length");
    std::memcpy(pages + 4096 - 2, "XY", 2);
    original_calls = 0;
    check(lookup_n(pages + 4096 - 2, 2) == original_text && original_calls == 1, "two-byte foreign key bounded");
    check(lookup_n(pages + 4096 - 1, 1) == original_text, "one-byte key never reads byte 2");
    VirtualFree(pages, 0, MEM_RELEASE);
    static_cast<void>(std::printf("country cache differential: %d checks, %d failures (%zu keys x2 passes)\n", checks,
                                  failures, corpus.size()));
    return failures != 0 ? 1 : 0;
}
