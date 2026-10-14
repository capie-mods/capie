// Offline test of the PATCHED country.cpp (CAPIE status text). Includes the patched copy staged by
// build.ps1; never loads into a game process. Hooks are not installed: originals stay null, so only
// keys the DLL itself answers are looked up through lookup_z / lookup_n.
#include "../../src/dll/capie_core.cpp"
using namespace capie::country;  // NOLINT(google-build-using-namespace): test compatibility shim
#include <cstdio>
#include <cstring>

static int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    // Inactive (failed/unstarted init): nothing is overridden.
    CHECK(override_value("RN_PLUGIN_STATUS") == nullptr && override_value("RN_PLUGIN_STATUS_PL") == nullptr);
    active.store(true);
    CHECK(std::strcmp(override_value("RN_PLUGIN_STATUS"), "CAPIE 1.3 loaded") == 0);
    CHECK(std::strcmp(override_value("RN_PLUGIN_STATUS_EN"), "CAPIE 1.3 loaded") == 0);
    // UTF-8 bytes of "załadowane": l-stroke = C5 82.
    CHECK(std::strcmp(override_value("RN_PLUGIN_STATUS_PL"), "CAPIE 1.3 za\xC5\x82" "adowane") == 0);
    CHECK(std::strcmp(lookup_z("RN_PLUGIN_STATUS"), "CAPIE 1.3 loaded") == 0);
    CHECK(std::strcmp(lookup_z("RN_PLUGIN_STATUS_EN"), "CAPIE 1.3 loaded") == 0);
    CHECK(std::strcmp(lookup_z("RN_PLUGIN_STATUS_PL"), "CAPIE 1.3 za\xC5\x82" "adowane") == 0);
    CHECK(std::strcmp(lookup_n("RN_PLUGIN_STATUS_PL", 19), "CAPIE 1.3 za\xC5\x82" "adowane") == 0);
    // Old text is gone; lookalike keys are not claimed (they would fall through to the game).
    CHECK(override_value("RN_PLUGIN_STATUS_DE") == nullptr && override_value("RN_PLUGIN_STATUSX") == nullptr);
    // RN1 protocol unchanged.
    CHECK(std::strcmp(override_value("RN1|Polska|polski|"), "Polska") == 0);
    CHECK(std::strcmp(override_value("RN1|Polska|polski|_ADJ"), "polski") == 0);
    CHECK(override_value("RN1|Polska|polski|_GER") == nullptr);
    std::printf("country status candidate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
