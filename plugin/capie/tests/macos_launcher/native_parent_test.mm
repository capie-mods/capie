/** @file native_parent_test.mm @brief Native Foundation staging tests; no game/launcher. */
#include "../../src/launcher/macos_child_staging.hpp"
#include <cstdio>

namespace {
unsigned checks = 0, failures = 0;
void check(bool result, const char* label) {
    ++checks;
    if (!result) { ++failures; std::printf("FAIL: %s\n", label); }
}
}
int main() {
    @autoreleasepool {
        namespace stage = capie::launcher::macos_child::staging;
        NSString* source = @"{\"gameId\":\"hoi4\",\"distPlatform\":\"steam\",\"rawVersion\":\"1.19.3.0\","
            "\"gameDataPath\":\"~/Documents/game\",\"exePath\":\"./hoi4.app/Contents/MacOS/hoi4\","
            "\"exeArgs\":[\"space argument\"],\"dlcPath\":\".\",\"themeFile\":\"./assets/theme.json\","
            "\"ingameSettingsLayoutPath\":\"settings-layout.json\",\"ingameSettingsPath\":\"pdx_settings.json\","
            "\"alternativeExecutables\":[{\"label\":{\"pl\":\"Debug\"},\"exePath\":\"./hoi4.app/Contents/MacOS/hoi4\","
            "\"exeArgs\":[\"-debug_mode\"]}],\"future\":{\"untouched\":true}}";
        NSString* hash = @"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        NSDictionary* original = stage::parse([source dataUsingEncoding:NSUTF8StringEncoding]);
        check(original != nil, "valid document");
        NSDictionary* result = stage::prepare(original, @"/game", @"/user", @"/owned/child", @"/owned/lib", hash);
        check(result != nil, "valid staging");
        check([result[@"gameDataPath"] isEqual:@"/user/Documents/game"], "same absolute data root");
        NSArray* args = result[@"exeArgs"];
        check([args isEqual:@[@"--capie-dev-child", @"/game/hoi4.app/Contents/MacOS/hoi4",
            @"/game/hoi4.app/Contents/MacOS", @"/user/Documents/game/dlc_load.json",
            @"/owned/lib", hash, @"--", @"space argument"]], "exact argv boundaries");
        NSDictionary* alternate = result[@"alternativeExecutables"][0];
        check([alternate[@"exePath"] isEqual:@"/owned/child"] &&
              [alternate[@"exeArgs"] lastObject] &&
              [[alternate[@"exeArgs"] lastObject] isEqual:@"-debug_mode"], "debug routed");
        check([alternate[@"label"] isEqual:@{@"pl": @"Debug"}], "translated label retained");
        check([result[@"dlcPath"] isEqual:@"/game"] &&
              [result[@"themeFile"] isEqual:@"/game/assets/theme.json"] &&
              [result[@"ingameSettingsLayoutPath"] isEqual:@"/game/settings-layout.json"], "resource roots");
        check([result[@"ingameSettingsPath"] isEqual:@"pdx_settings.json"] &&
              [result[@"future"] isEqual:original[@"future"]], "other settings preserved");
        check([original[@"exePath"] isEqual:@"./hoi4.app/Contents/MacOS/hoi4"], "input unchanged");
        for (NSString* invalid in @[@"{", @"[]", @"{\"a\":1,\"a\":2}",
             @"{\"a\":1,\"\\u0061\":2}", @"{\"x\":{\"a\":1,\"a\":2}}"])
            check(stage::parse([invalid dataUsingEncoding:NSUTF8StringEncoding]) == nil, "bad/duplicate JSON");
        check(stage::parse([@"{\"x\":{\"a\":1},\"y\":{\"a\":2}}" dataUsingEncoding:NSUTF8StringEncoding]) != nil,
              "same key in separate objects valid");
        for (NSString* key in @[@"gameDataPath", @"exePath", @"exeArgs", @"alternativeExecutables", @"gameId"] ) {
            NSMutableDictionary* bad = [original mutableCopy]; bad[key] = @"unsupported";
            check(stage::prepare(bad, @"/game", @"/user", @"/owned/child", @"/owned/lib", hash) == nil,
                  "malformed field refuses entire config");
        }
        NSMutableDictionary* bad = [original mutableCopy];
        bad[@"alternativeExecutables"] = @[@{@"exePath": @"/other", @"exeArgs": @[]}];
        check(stage::prepare(bad, @"/game", @"/user", @"/owned/child", @"/owned/lib", hash) == nil,
              "unknown alternate not dropped");
        check(stage::prepare(original, @"/game", @"/user", @"relative", @"/owned/lib", hash) == nil,
              "relative child rejected");
        check(stage::prepare(original, @"/game", @"/user", @"/child", @"/owned/lib", @"bad") == nil,
              "digest rejected");
        check(stage::absolute(@"/owned/lib:alias") == nil, "colon-delimited path rejected");
        bad = [original mutableCopy]; bad[@"exeArgs"] = @[@"--capie-dev-child"];
        check(stage::prepare(bad, @"/game", @"/user", @"/child", @"/owned/lib", hash) == nil,
              "duplicate marker argument rejected");
        bad = [original mutableCopy];
        const unichar nul = 0;
        bad[@"exeArgs"] = @[[NSString stringWithCharacters:&nul length:1]];
        check(stage::prepare(bad, @"/game", @"/user", @"/child", @"/owned/lib", hash) == nil,
              "embedded NUL argument rejected");
        NSDictionary* pair = stage::prepare(original, @"/game", @"/user", @"/owned/child", @"/owned/boot", hash,
                                             @"/owned/runtime", hash);
        NSArray* pair_args = pair[@"exeArgs"];
        check(pair != nil && [pair_args[6] isEqual:@"--capie-runtime"] &&
              [pair_args[7] isEqual:@"/owned/runtime"] && [pair_args[8] isEqual:hash] &&
              [pair_args[9] isEqual:@"--"] && [pair_args[10] isEqual:@"space argument"], "paired staging order");
        check(stage::prepare(original, @"/game", @"/user", @"/child", @"/boot", hash,
                             @"/runtime", @"bad") == nil, "paired bad runtime hash refused");
        std::printf("native staging: %u checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
    }
}
