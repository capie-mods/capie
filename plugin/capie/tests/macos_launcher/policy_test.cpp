#include "../../src/launcher/macos_child_policy.hpp"

#include <cassert>
using namespace capie::launcher::macos_child;
int main() {
    assert(enabled(R"({"enabled_mods":["mod/capie.mod"]})"));
    assert(enabled(R"({"enabled_mods":["MOD\\CAPIE.MOD"]})"));
    assert(enabled(R"({"enabled_mods":["mod/ugc_3819929471.mod"]})"));
    assert(!enabled(R"({"enabled_mods":[]})"));
    assert(!enabled(R"({"enabled_mods":["mod/capie.mod"],"enabled_mods":[]})"));
    assert(!enabled(R"({"enabled_mods":["mod/capie.mod",4]})"));
    assert(!enabled(R"({"enabled_mods":["mod/capie.mod"]}junk)"));
    assert(!enabled(R"({"enabled_mods":["mod/not-capie.mod"]})"));
    assert(!enabled(""));
    assert(libraries("/steam/a.dylib:/capie.dylib", "/steam/b.dylib", "/capie.dylib", false) ==
           "/steam/a.dylib:/steam/b.dylib");
    assert(libraries("/steam/a.dylib", "/steam/a.dylib:/steam/b.dylib", "/capie.dylib", true) ==
           "/steam/a.dylib:/steam/b.dylib:/capie.dylib");
    assert(libraries("", "", "/capie.dylib", false).empty());
    const char* args[] = {"child",    "--pdxfake", "value", "--capie-dev-child", "/game", "/cwd", "/playset",
                          "/library", "hash",      "--",    "space argument"};
    assert(config_start(11, args) == 3);
    const char* duplicate[] = {"child", "--capie-dev-child", "--capie-dev-child"};
    assert(!config_start(3, duplicate));
    const char* missing[] = {"child", "/game", "/cwd", "/playset", "/library", "hash", "--"};
    assert(!config_start(7, missing));
    const char* malformed[] = {"child", "--capie-dev-child", "/game", "/cwd", "/playset", "/library", "hash", "bad"};
    assert(!config_start(8, malformed));
    assert(!enabled(std::string("{\"enabled_mods\":[\"mod/capie.mod\"],\"ignored\":\"") + char(0xff) + "\"}"));
    assert(enabled(R"({"enabled_mods":["mod/capie.mod"],"ignored":"Zażółć 😀"})"));
    for (const auto& bad : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
                            std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)}) {
        assert(!enabled(std::string("{\"enabled_mods\":[\"mod/capie.mod\"],\"ignored\":\"") + bad + "\"}"));
    }
    const char* paired[] = {"child",     "--capie-dev-child", "/game",    "/cwd", "/playset", "/boot",
                            "hash",      "--capie-runtime",   "/runtime", "hash", "--",       "--capie-runtime",
                            "game value"};
    assert(config_start(13, paired) == 1);
    assert(config_delimiter(13, paired, 1) == 10);
    assert(libraries_pair("/foreign:/boot", "/runtime:/steam:/foreign", "/boot", "/runtime", true,
                          [](std::string_view) { return false; }) == "/boot:/foreign:/steam:/runtime");
    assert(libraries_pair("/foreign:/boot", "/runtime:/steam", "/boot", "/runtime", false,
                          [](std::string_view) { return false; }) == "/foreign:/steam");
    assert(!config_start(10, paired));
    const std::string good(64, 'a'), other(64, 'b');
    assert(artifacts_verified({good, good, good}, {good, good, good}, true));
    assert(!artifacts_verified({good, other, good}, {good, good, good}, true));
    assert(!artifacts_verified({good, good, other}, {good, good, good}, true));
    assert(!artifacts_verified({good, good, ""}, {good, good, ""}, true));
    assert(artifacts_verified({good, good, ""}, {good, good, ""}, false));
}
