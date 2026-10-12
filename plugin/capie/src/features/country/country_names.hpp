/**
 * @file country_names.hpp
 * @brief Localisation overrides served by `capie_core.dll`: RN1 country names and the CAPIE status text.
 *
 * The three hooked localisation entry points (`lookup_n`, `lookup_z`, `exists`) run for
 * every localised string the game draws, from any thread, many times per frame. For
 * almost all of them the answer is "not ours", so the first thing each hook does is the
 * cheapest possible test: is the key at least two bytes long and does it start with `RN`?
 * Every key CAPIE answers (`RN1|...`, `RN_PLUGIN_STATUS*`) does; everything else goes to
 * the original lookup after two byte compares, with no hashing, decoding or locking.
 *
 * For our keys:
 *   - `RN_PLUGIN_STATUS` and `RN_PLUGIN_STATUS_EN` return "CAPIE 1.0 loaded",
 *     `RN_PLUGIN_STATUS_PL` returns "CAPIE 1.0 załadowane" (the mod's localisation picks
 *     the language through `$RN_PLUGIN_STATUS_EN$` / `$RN_PLUGIN_STATUS_PL$`, R5-MINOR-2);
 *   - `RN1|Name|Adjective|` (+ `_DEF`, `_ADJ`) returns the name or adjective.
 * Decoded RN1 keys are cached by key: a shared lock and one hash lookup without
 * allocation answer repeat queries; a miss decodes, validates and inserts under an
 * exclusive lock. Returned strings are owned by the cache and never freed or moved while
 * the process lives, because the game may keep the pointer.
 *
 * Inactive (before or after a failed initialization): every hook forwards to the original.
 * Exceptions (allocation failure) fall back to the original lookup; a hook never throws.
 *
 * Contract of the hook bodies: the original is called exactly once whenever CAPIE does
 * not answer, no lock is held while it runs, and its result is returned unchanged.
 */
#pragma once
#include "capability.hpp"
#include "country_protocol.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace capie::country {

/// @brief Longest key CAPIE inspects; longer keys are always forwarded.
inline constexpr std::size_t max_key_bytes = 360;
/// @brief Bare status key (kept for older mod localisation).
inline constexpr std::string_view status_key = "RN_PLUGIN_STATUS";
/// @brief English status key referenced by `RN_COUNTRY_PLUGIN` in every non-Polish language.
inline constexpr std::string_view status_key_en = "RN_PLUGIN_STATUS_EN";
/// @brief Polish status key.
inline constexpr std::string_view status_key_pl = "RN_PLUGIN_STATUS_PL";
/// @brief Status text, English (also for the bare key).
inline constexpr char status_text_en[] = "CAPIE 1.0 loaded";
/// @brief Status text, Polish, UTF-8 ("załadowane": ł = C5 82).
inline constexpr char status_text_pl[] =
    "CAPIE 1.0 za\xC5\x82"
    "adowane";
/// @brief The two leading bytes shared by every key CAPIE answers.
inline constexpr char claim_prefix[2] = {'R', 'N'};

/// @brief `true` after a successful initialization; read by every hook.
inline std::atomic<bool> active = false;

/// @brief Transparent hash so the cache can be searched with a `string_view` (no allocation).
struct KeyHash {
    using is_transparent = void;  ///< Enables heterogeneous lookup.
    /**
     * @brief Hashes key bytes.
     * @param key Key bytes.
     * @return Hash value.
     */
    std::size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
};

/// @brief Guards `key_cache()`; shared for lookups, exclusive for inserts. Never held across a game call.
inline std::shared_mutex cache_mutex;
/// @brief Type of the RN1 key cache.
using KeyCache = std::unordered_map<std::string, std::unique_ptr<const std::string>, KeyHash, std::equal_to<>>;

/**
 * @brief RN1 key -> interned answer. Entries are never erased (the game may keep the pointers).
 * @return The process-wide cache, created on first use.
 * @note Created inside a hook body (which catches allocation failure), not during DLL load:
 *       the MSVC unordered_map constructor allocates and could throw at static initialization.
 */
inline KeyCache& key_cache() {
    static KeyCache cache;
    return cache;
}

/**
 * @brief Cheapest "could this be ours" test, applied before anything else.
 * @param key Key bytes (at least `size` readable).
 * @param size Key length.
 * @return `false` for every key CAPIE never answers.
 */
inline bool may_claim(const char* key, std::size_t size) noexcept {
    return size >= sizeof(claim_prefix) && key[0] == claim_prefix[0] && key[1] == claim_prefix[1];
}

/**
 * @brief Status text for a status key.
 * @param key Key.
 * @return Static text, or null when `key` is not a status key.
 */
inline const char* status_text(std::string_view key) noexcept {
    if (key == status_key || key == status_key_en) {
        return status_text_en;
    }
    if (key == status_key_pl) {
        return status_text_pl;
    }
    return nullptr;
}

/**
 * @brief Answer for an RN1 key: cached when seen before, decoded and inserted otherwise.
 * @param key Key bytes.
 * @return Interned name/adjective, or null when the key is not a valid protocol key.
 * @throws std::bad_alloc on allocation failure (callers fall back to the original lookup).
 */
inline const char* country_value(std::string_view key) {
    if (!key.starts_with(protocol_prefix)) {
        return nullptr;
    }
    {
        const std::shared_lock lock(cache_mutex);
        if (const auto found = key_cache().find(key); found != key_cache().end()) {
            return found->second->c_str();
        }
    }
    const auto decoded = decode_country_key(key);
    if (!decoded) {
        return nullptr;
    }
    const auto& value = decoded->adjective_key ? decoded->adjective : decoded->name;
    const std::unique_lock lock(cache_mutex);
    auto& cache = key_cache();
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(std::string(key), std::make_unique<const std::string>(value)).first;
    }
    return found->second->c_str();
}

/**
 * @brief Text CAPIE substitutes for `key`, if any.
 * @param key Key bytes.
 * @return Status text, RN1 answer or null (forward to the original).
 */
inline const char* override_value(std::string_view key) {
    if (!active.load()) {
        return nullptr;
    }
    if (const auto* status = status_text(key)) {
        return status;
    }
    return country_value(key);
}

/// @brief `const char* lookup(const char* key, int length)`.
using LookupN = const char* (*)(const char*, int);
/// @brief `const char* lookup(const char* key)`.
using LookupZ = const char* (*)(const char*);
/// @brief `bool exists(const char* key)`.
using Exists = bool (*)(const char*);

/// @brief Trampoline of `lookup_n`.
inline LookupN original_n = nullptr;
/// @brief Trampoline of `lookup_z`.
inline LookupZ original_z = nullptr;
/// @brief Trampoline of `exists`.
inline Exists original_exists = nullptr;

/**
 * @brief Hook body of `lookup(key, length)`.
 * @param key Key bytes (not necessarily terminated).
 * @param length Key length.
 * @return CAPIE text or the original result.
 */
inline const char* lookup_n(const char* key, int length) noexcept {
    try {
        if (key != nullptr && length >= 0 && std::cmp_less_equal(length, max_key_bytes) &&
            may_claim(key, static_cast<std::size_t>(length))) {
            if (const auto* result = override_value(std::string_view(key, static_cast<std::size_t>(length)))) {
                return result;
            }
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure falls back to the game path
    }
    return original_n(key, length);
}

/**
 * @brief Hook body of `lookup(key)`.
 * @param key NUL-terminated key.
 * @return CAPIE text or the original result.
 */
inline const char* lookup_z(const char* key) noexcept {
    try {
        if (key != nullptr && key[0] == claim_prefix[0] && key[1] == claim_prefix[1]) {
            const auto n = strnlen_s(key, max_key_bytes + 1);
            if (n <= max_key_bytes) {
                if (const auto* result = override_value(std::string_view(key, n))) {
                    return result;
                }
            }
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure falls back to the game path
    }
    return original_z(key);
}

/**
 * @brief Hook body of `exists(key)`: valid RN1 keys exist; status keys are left to the game.
 * @param key NUL-terminated key.
 * @return `true` for a valid RN1 key while active, otherwise the original result.
 */
inline bool exists(const char* key) noexcept {
    try {
        if (key != nullptr && key[0] == claim_prefix[0] && key[1] == claim_prefix[1] && active.load()) {
            const auto n = strnlen_s(key, max_key_bytes + 1);
            if (n <= max_key_bytes && country_value(std::string_view(key, n)) != nullptr) {
                return true;
            }
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): allocation failure falls back to the game path
    }
    return original_exists(key);
}

}  // namespace capie::country
