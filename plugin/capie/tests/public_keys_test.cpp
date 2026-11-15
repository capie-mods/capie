/**
 * @file public_keys_test.cpp
 * @brief Offline test of the public localisation keys `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD` (C4).
 *
 * Includes the `capie_core.dll` composition root; hooks are not installed. The three
 * original lookups are replaced by fakes that count calls, so the test can prove that
 * CAPIE answers exactly its keys and forwards everything else exactly once.
 */
#include "../src/dll/capie_core.cpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

using capie::country::active;
using capie::country::lookup_n;
using capie::country::lookup_z;
using capie::country::override_value;

unsigned checks = 0;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
unsigned failures = 0;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
unsigned forwarded = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): original-call counter

/// @brief Text the fake originals return.
constexpr char game_text[] = "game text";

/**
 * @brief Records one check.
 * @param value Condition.
 * @param label Printed on failure.
 */
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

/**
 * @brief Fake original `lookup(key, length)`.
 * @return `game_text`.
 */
const char* fake_n(const char* /*key*/, int /*length*/) {
    ++forwarded;
    return game_text;
}

/**
 * @brief Fake original `lookup(key)`.
 * @return `game_text`.
 */
const char* fake_z(const char* /*key*/) {
    ++forwarded;
    return game_text;
}

/**
 * @brief Whether a C string equals an expected text.
 * @param value Returned pointer.
 * @param expected Expected text.
 * @return `true` on equality.
 */
bool is(const char* value, std::string_view expected) {
    return value != nullptr && std::string_view(value) == expected;
}

}  // namespace

int main() {
    capie::country::original_n = fake_n;
    capie::country::original_z = fake_z;

    // Inactive: nothing is answered, every key reaches the game.
    check(override_value("CAPIE_STATUS") == nullptr, "inactive: no CAPIE_STATUS");
    check(is(lookup_z("CAPIE_STATUS"), game_text) && forwarded == 1, "inactive: forwarded once");

    active.store(true);
    // CAPIE 1.7: the status keys say "editor off" until the editor reports a start, after a
    // failed start and after a game-call fault (shared latch); API and build never change.
    using capie::country::editor_state;
    check(is(override_value("CAPIE_STATUS"), "CAPIE 1.7: editor off") &&
              is(override_value("RN_PLUGIN_STATUS"), "CAPIE 1.7: editor off, restart the game") &&
              is(override_value("RN_PLUGIN_STATUS_EN"), "CAPIE 1.7: editor off, restart the game") &&
              is(override_value("RN_PLUGIN_STATUS_PL"),
                 "CAPIE 1.7: edytor wy\xC5\x82\xC4\x85"
                 "czony, uruchom gr\xC4\x99 ponownie") &&
              is(override_value("CAPIE_API"), "9"),
          "editor not reported: editor off");
    editor_state.store(20);  // signature_mismatch
    check(is(override_value("CAPIE_STATUS"), "CAPIE 1.7: editor off"), "editor start failed: editor off");
    RNEditorReport(0);  // what capie_editor.dll calls after a successful start
    capie::engine::game_call_faulted.store(true);
    check(is(override_value("CAPIE_STATUS"), "CAPIE 1.7: editor off") &&
              is(override_value("RN_PLUGIN_STATUS"), "CAPIE 1.7: editor off, restart the game"),
          "fault latch: editor off");
    capie::engine::game_call_faulted.store(false);
    check(is(override_value("CAPIE_STATUS"), "CAPIE 1.7"), "CAPIE_STATUS");
    check(is(override_value("CAPIE_API"), "9"), "CAPIE_API");
    check(is(override_value("CAPIE_BUILD"), "1.7.1"), "CAPIE_BUILD");
    forwarded = 0;
    check(is(lookup_z("CAPIE_STATUS"), "CAPIE 1.7") && is(lookup_z("CAPIE_API"), "9") &&
              is(lookup_z("CAPIE_BUILD"), "1.7.1"),
          "lookup_z answers the public keys");
    check(is(lookup_n("CAPIE_BUILD", 11), "1.7.1") && is(lookup_n("CAPIE_STATUSXYZ", 12), "CAPIE 1.7"),
          "lookup_n uses the explicit length");
    check(forwarded == 0, "answered keys never reach the game");

    // Lookalikes and other CA... keys are forwarded exactly once.
    const char* const others[] = {"CAPIE_STATUS_TT", "CAPIE_",    "CA",           "C",
                                  "CAPITAL",         "CAPIE_api", "capie_status", "CAPIE_VERSION"};
    for (const char* key : others) {
        forwarded = 0;
        check(is(lookup_z(key), game_text) && forwarded == 1, key);
        forwarded = 0;
        check(is(lookup_n(key, static_cast<int>(std::strlen(key))), game_text) && forwarded == 1, key);
    }
    // Rename Anything's keys are unchanged (with the 1.4 version text).
    check(is(override_value("RN_PLUGIN_STATUS"), "CAPIE 1.7 loaded"), "RN_PLUGIN_STATUS");
    check(is(override_value("RN1|Polska|polski|_ADJ"), "polski"), "RN1 unchanged");
    std::printf("public keys tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
