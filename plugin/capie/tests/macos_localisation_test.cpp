/** @file macos_localisation_test.cpp
 * @brief Tests the shared localisation hook contract on macOS without a game or hooks.
 */
#include "../src/features/country/country_names.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
/// @brief Number of invocations forwarded to the original lookup.
int calls = 0;
/// @brief Last forwarded key, including null.
const char* seen = nullptr;
/// @brief Last explicit length forwarded unchanged.
int seen_length = 0;
/// @brief Stable original result used to verify pointer identity.
constexpr char fallback[] = "original";
/** @brief Record an explicit-length fallback.
 * @param key Forwarded key. @param length Forwarded length. @return Original result pointer.
 */
const char* original_n(const char* key, int length) {
    ++calls;
    seen = key;
    seen_length = length;
    return fallback;
}
/** @brief Record a terminated-key fallback.
 * @param key Forwarded key. @return Original result pointer.
 */
const char* original_z(const char* key) {
    ++calls;
    seen = key;
    return fallback;
}
/** @brief Record an existence fallback.
 * @param key Forwarded key. @return False, distinguishing the original from an RN1 answer.
 */
bool original_exists(const char* key) {
    ++calls;
    seen = key;
    return false;
}
/// @brief Number of successful assertions before termination.
int checks = 0;
/** @brief Require an assertion. @param value Expected true. */
void check(bool value) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "FAIL check %d\n", checks);
        std::exit(1);
    }
}
}  // namespace

/** @brief Exercise forwarding, activation, RN1 forms and persistent answers. */
int main() {
    namespace c = capie::country;
    c::original_n = original_n;
    c::original_z = original_z;
    c::original_exists = original_exists;
    constexpr char key[] = "RN1|Polska|Polski|";
    c::active = false;
    check(c::lookup_z(key) == fallback && calls == 1 && seen == key);
    check(!c::exists(key) && calls == 2);
    check(c::lookup_n(nullptr, -1) == fallback && calls == 3 && seen == nullptr && seen_length == -1);
    c::active = true;
    check(std::string_view(c::lookup_z(key)) == "Polska" && calls == 3);
    const auto* held = c::lookup_z(key);
    check(c::lookup_z(key) == held);
    check(std::string_view(c::lookup_z("RN1|Polska|Polski|_ADJ")) == "Polski");
    check(std::string_view(c::lookup_z("RN1|Polska|Polski|_DEF")) == "Polska");
    check(c::exists(key) && calls == 3);
    check(!c::exists("CAPIE_API") && calls == 4); // Status existence belongs to the game.
    check(std::string_view(c::lookup_z("CAPIE_API")) == capie::core::capie_api_text);
    check(std::string_view(c::lookup_z("CAPIE_BUILD")) == capie::core::capie_release);
    check(std::string_view(c::lookup_z("RN_PLUGIN_STATUS_PL")) == c::status_text_pl);
    constexpr char sized[] = {'C', 'A', 'P', 'I', 'E', '_', 'A', 'P', 'I', 'x'};
    check(std::string_view(c::lookup_n(sized, 9)) == capie::core::capie_api_text);
    check(c::lookup_n(sized, 10) == fallback && calls == 5 && seen_length == 10);
    std::string long_key(c::max_key_bytes + 1, 'x');
    long_key[0] = 'R';
    long_key[1] = 'N';
    check(c::lookup_z(long_key.c_str()) == fallback && calls == 6);
    check(c::lookup_z("RN1|bad$|name|") == fallback && calls == 7);
    check(c::lookup_z("") == fallback && calls == 8);
    check(c::lookup_z("R") == fallback && calls == 9);
    c::active = false;
    check(c::lookup_z(key) == fallback && calls == 10);
    check(std::string_view(held) == "Polska");
    std::printf("PASS: %d shared localisation checks\n", checks);
}
