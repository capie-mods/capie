/**
 * @file macos_child_policy.hpp
 * @brief Pure policy for the development macOS child launcher; no process/file access.
 */
#pragma once
#include "mod_identity.hpp"
#include "playset_json.hpp"

namespace capie::launcher::macos_child {
/// @brief Hash values from bounded regular-file reads or trusted expected configuration.
struct ArtifactHashes {
    std::string_view game, bootstrap, runtime;
};
/// @brief Pair admission requires both distinct library checks; missing runtime hash never becomes legacy mode.
inline bool artifacts_verified(ArtifactHashes actual, ArtifactHashes expected, bool paired) noexcept {
    const auto match = [](std::string_view a, std::string_view e) { return e.size() == 64 && a == e; };
    return match(actual.game, expected.game) && match(actual.bootstrap, expected.bootstrap) &&
           (!paired || match(actual.runtime, expected.runtime));
}

/// @brief Locate the delimiter for a legacy or paired marked configuration; never inspect game args after it.
inline std::optional<int> config_delimiter(int argc, const char* const* argv, int marker) {
    if (marker < 1 || argc - marker < 7)
        return std::nullopt;
    if (std::string_view(argv[marker + 6]) == "--")
        return marker + 6;
    if (argc - marker >= 10 && std::string_view(argv[marker + 6]) == "--capie-runtime" &&
        std::string_view(argv[marker + 9]) == "--")
        return marker + 9;
    return std::nullopt;
}

/**
 * @brief Locate exactly one complete marked configuration block among original game arguments.
 * @param argc Process argument count including argv[0].
 * @param argv Process arguments; no values are logged.
 * @return Marker index, or no value for absent, duplicate, or malformed configuration.
 */
inline std::optional<int> config_start(int argc, const char* const* argv) {
    std::optional<int> marker;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--capie-dev-child") {
            if (marker) {
                return std::nullopt;
            }
            marker = i;
        }
    }
    if (!marker || !config_delimiter(argc, argv, *marker)) {
        return std::nullopt;
    }
    return marker;
}

/**
 * @brief Validate every raw byte as strict UTF-8, including JSON fields the parser ignores.
 * @param text Complete document bytes, optionally including its UTF-8 BOM.
 * @return False for invalid leads/tails, overlong forms, surrogates, truncation or scalars above U+10FFFF.
 */
inline bool valid_utf8(std::string_view text) noexcept {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at++]);
        if (lead < 0x80) {
            continue;
        }
        unsigned tails = 0;
        std::uint32_t scalar = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            tails = 1;
            scalar = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            tails = 2;
            scalar = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            tails = 3;
            scalar = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - at < tails) {
            return false;
        }
        for (unsigned i = 0; i < tails; ++i) {
            const auto tail = static_cast<unsigned char>(text[at++]);
            if ((tail & 0xc0) != 0x80) {
                return false;
            }
            scalar = (scalar << 6) | (tail & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Decide whether a strict playset document enables the shared CAPIE identity.
 * @param json Complete bounded dlc_load.json content.
 * @return False for invalid JSON, absent identity, or a disabled playset.
 */
inline bool enabled(std::string_view json) {
    if (!valid_utf8(json)) {
        return false;
    }
    const auto mods = parse_enabled_mods(json);
    if (!mods) {
        return false;
    }
    for (const auto& mod : *mods) {
        const auto path = detail::normalise_descriptor(mod);
        if (path == capie_mod_descriptor || path == std::string("mod/ugc_") + capie_mod_workshop_id + ".mod") {
            return true;
        }
    }
    return false;
}

/**
 * @brief Preserve foreign/Steam libraries, deduplicate, and gate only the configured CAPIE path.
 * @param existing Current DYLD_INSERT_LIBRARIES value.
 * @param steam STEAM_DYLD_INSERT_LIBRARIES value, preserved separately by the caller.
 * @param own Configured absolute CAPIE library path, containing no colon when inject is true.
 * @param inject Whether to append the configured CAPIE library after existing entries.
 * @param own_alias Predicate identifying other spellings of the configured library.
 * @return Colon-delimited child loader value with configured identity removed before gating.
 */
template <class OwnAlias>
inline std::string libraries(std::string_view existing, std::string_view steam, std::string_view own, bool inject,
                             const OwnAlias& own_alias) {
    std::vector<std::string> entries;
    for (const auto input : {existing, steam}) {
        std::size_t at = 0;
        while (at < input.size()) {
            const auto colon = input.find(':', at);
            const auto part = input.substr(at, colon == input.npos ? input.size() - at : colon - at);
            if (!part.empty() && part != own && !own_alias(part) &&
                std::find(entries.begin(), entries.end(), part) == entries.end()) {
                entries.emplace_back(part);
            }
            if (colon == input.npos) {
                break;
            }
            at = colon + 1;
        }
    }
    if (inject) {
        entries.emplace_back(own);
    }
    std::string out;
    for (const auto& entry : entries) {
        if (!out.empty()) {
            out += ':';
        }
        out += entry;
    }
    return out;
}

/// @brief Pure exact-path variant for callers without filesystem identity information.
inline std::string libraries(std::string_view existing, std::string_view steam, std::string_view own, bool inject) {
    return libraries(existing, steam, own, inject, [](std::string_view) { return false; });
}

/** @brief Pair order is bootstrap, retained foreign/Steam entries, runtime.
 * Foreign entries are preserved, not trusted; the bootstrap verifies its allowed closure.
 */
template <class OwnAlias>
inline std::string libraries_pair(std::string_view existing, std::string_view steam, std::string_view bootstrap,
                                  std::string_view runtime, bool inject, const OwnAlias& own_alias) {
    const auto middle = libraries(existing, steam, bootstrap, false,
                                  [&](std::string_view path) { return path == runtime || own_alias(path); });
    if (!inject)
        return middle;
    return std::string(bootstrap) + ':' + (middle.empty() ? "" : middle + ':') + std::string(runtime);
}
}  // namespace capie::launcher::macos_child
