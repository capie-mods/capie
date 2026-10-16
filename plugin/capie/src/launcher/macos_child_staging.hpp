/** @file macos_child_staging.hpp
 * @brief Native Foundation staging policy. No process launches, file writes or game calls.
 */
#pragma once
#import <Foundation/Foundation.h>
#include <filesystem>
#include <string>

namespace capie::launcher::macos_child::staging {
/// @brief Require a nonempty UTF-8 string without filesystem/loader control characters.
inline bool path_string(id value) {
    if (![value isKindOfClass:[NSString class]] || [value length] == 0)
        return false;
    NSCharacterSet* forbidden = [NSCharacterSet characterSetWithCharactersInString:@"\0\r\n:"];
    return [value rangeOfCharacterFromSet:forbidden].location == NSNotFound && [value UTF8String] != nullptr;
}
/// @brief Normalize an absolute path lexically; do not resolve or read user files.
inline NSString* absolute(id value) {
    if (!path_string(value) || ![value hasPrefix:@"/"])
        return nil;
    auto path = std::filesystem::path([value UTF8String]).lexically_normal().string();
    if (path.size() > 1 && path.back() == '/')
        path.pop_back();
    return [NSString stringWithUTF8String:path.c_str()];
}
/// @brief Resolve a game-relative resource without expanding shell/user syntax.
inline NSString* resource(id value, NSString* game) {
    if (!path_string(value) || [value hasPrefix:@"~"])
        return nil;
    return absolute([value hasPrefix:@"/"] ? value : [game stringByAppendingPathComponent:value]);
}

/**
 * @brief Parse bounded JSON using Foundation, then reject duplicate decoded object keys.
 * Foundation validates all grammar first. This scanner only tracks delimiters and already
 * validated string tokens, decoding keys via Foundation; it is not a second JSON parser.
 * @return Dictionary or nil for malformed/duplicate/non-object/oversize input.
 */
inline NSDictionary* parse(NSData* data) {
    if (!data || data.length > 1024 * 1024)
        return nil;
    id document = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:nil];
    if (![document isKindOfClass:[NSDictionary class]])
        return nil;
    // UTF-8 is the settings contract; Foundation can also accept UTF-16, which we refuse.
    NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!text)
        return nil;
    NSMutableArray* frames = [NSMutableArray array];
    const auto* bytes = static_cast<const unsigned char*>(data.bytes);
    for (std::size_t at = 0; at < data.length; ++at) {
        const auto ch = bytes[at];
        if (ch == '{')
            [frames addObject:[NSMutableSet set]];
        else if (ch == '[')
            [frames addObject:[NSNull null]];
        else if (ch == '}' || ch == ']') {
            if (frames.count == 0)
                return nil;
            [frames removeLastObject];
        } else if (ch == '"') {
            const auto start = at++;
            while (at < data.length && bytes[at] != '"') {
                if (bytes[at] == '\\')
                    ++at;
                ++at;
            }
            if (at >= data.length)
                return nil;
            auto next = at + 1;
            while (next < data.length &&
                   (bytes[next] == ' ' || bytes[next] == '\t' || bytes[next] == '\r' || bytes[next] == '\n'))
                ++next;
            if (next < data.length && bytes[next] == ':') {
                id keys = frames.lastObject;
                if (![keys isKindOfClass:[NSMutableSet class]])
                    return nil;
                NSData* token = [data subdataWithRange:NSMakeRange(start, at - start + 1)];
                id key = [NSJSONSerialization JSONObjectWithData:token options:NSJSONReadingFragmentsAllowed error:nil];
                if (![key isKindOfClass:[NSString class]] || [keys containsObject:key])
                    return nil;
                [keys addObject:key];
            }
        }
    }
    return document;
}

/** @brief Copy validated settings, preserve all fields and route every supported executable.
 * @return Independent dictionary or nil; never silently drops an unsupported alternate.
 */
inline NSDictionary* prepare(NSDictionary* source, NSString* game, NSString* home, NSString* child, NSString* library,
                             NSString* hash, NSString* runtime = nil, NSString* runtime_hash = nil) {
    game = absolute(game);
    home = absolute(home);
    child = absolute(child);
    library = absolute(library);
    if (!game || !home || !child || !library || ![source isKindOfClass:[NSDictionary class]] ||
        ![source[@"gameId"] isEqual:@"hoi4"] || ![source[@"distPlatform"] isEqual:@"steam"] ||
        ![source[@"rawVersion"] isEqual:@"1.19.3.0"] || hash.length != 64 ||
        [hash rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"]
                                          invertedSet]]
                .location != NSNotFound)
        return nil;
    const bool paired = runtime != nil || runtime_hash != nil;
    if (paired) {
        runtime = absolute(runtime);
        if (!runtime || [runtime isEqual:library] || runtime_hash.length != 64 ||
            [runtime_hash
                rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"]
                                            invertedSet]]
                    .location != NSNotFound)
            return nil;
    }
    // Serialization gives independent mutable nested containers, preserving JSON values.
    NSData* copy = [NSJSONSerialization dataWithJSONObject:source options:0 error:nil];
    NSMutableDictionary* out =
        [NSJSONSerialization JSONObjectWithData:copy options:NSJSONReadingMutableContainers error:nil];
    id data = out[@"gameDataPath"];
    if (!path_string(data))
        return nil;
    data = absolute([data hasPrefix:@"~/"] ? [home stringByAppendingPathComponent:[data substringFromIndex:2]] : data);
    if (!data)
        return nil;
    out[@"gameDataPath"] = data;
    NSString* executable = [game stringByAppendingPathComponent:@"hoi4.app/Contents/MacOS/hoi4"];
    id alternates = out[@"alternativeExecutables"] ?: @[];
    if (![alternates isKindOfClass:[NSArray class]])
        return nil;
    NSMutableArray* modes = [NSMutableArray arrayWithObject:out];
    [modes addObjectsFromArray:alternates];
    for (id mode in modes) {
        if (![mode isKindOfClass:[NSMutableDictionary class]] || ![resource(mode[@"exePath"], game) isEqual:executable])
            return nil;
        id args = mode[@"exeArgs"] ?: @[];
        if (![args isKindOfClass:[NSArray class]])
            return nil;
        for (id arg in args) {
            if (![arg isKindOfClass:[NSString class]] || [arg isEqual:@"--capie-dev-child"] ||
                [arg rangeOfString:@"\0"].location != NSNotFound)
                return nil;
        }
        NSMutableArray* replacement = [NSMutableArray arrayWithArray:@[
            @"--capie-dev-child", executable, [executable stringByDeletingLastPathComponent],
            [data stringByAppendingPathComponent:@"dlc_load.json"], library, hash
        ]];
        if (paired)
            [replacement addObjectsFromArray:@[ @"--capie-runtime", runtime, runtime_hash ]];
        [replacement addObject:@"--"];
        [replacement addObjectsFromArray:args];
        mode[@"exePath"] = child;
        mode[@"exeArgs"] = replacement;
    }
    for (NSString* field in @[ @"dlcPath", @"themeFile", @"ingameSettingsLayoutPath" ]) {
        if (out[field]) {
            NSString* path = resource(out[field], game);
            if (!path)
                return nil;
            out[field] = path;
        }
    }
    return out;
}
}  // namespace capie::launcher::macos_child::staging
