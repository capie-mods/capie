/**
 * @file catalog_text.hpp
 * @brief The text keys of catalog views (`CAPIE_V1_T__<MOD>__<VIEW>__<k>`), answered by `capie_core.dll`.
 *
 * `capie_editor.dll` computes a view's labels on the UI thread (for the VP view: the shown name of
 * each VP province, read through the game's `ProvinceName`) and publishes them through the core
 * export `RNCatalogPublish(key, text)`. The localisation hooks of the core (`country_names.hpp`)
 * then answer the key with the published text. A key is answered only after its view published it,
 * so a key of an unregistered view goes to the original lookup and the SDK fallback shows.
 *
 * Texts are interned and never freed (the game may keep a returned pointer, as for the `RN1|`
 * answers); a key's answer is replaced by a pointer swap under an exclusive lock. The hook side
 * takes only a shared lock and never reads the game. A text keeps its bytes, except that control
 * characters and the markup characters `$ [ ]` become a space (a player-given province name
 * cannot open a localisation macro). Texts are cut at 255 bytes on a UTF-8 boundary.
 */
#pragma once
#include "catalog_names.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace capie::catalog::text {

/// @brief Longest published text in bytes.
inline constexpr std::size_t max_text = 255;
/// @brief Most distinct keys (16 views x (48 rows + 3)).
inline constexpr std::size_t max_keys = 16 * (48 + 3);
/// @brief Name of the core export.
inline constexpr char publish_export[] = "RNCatalogPublish";

/// @brief `int RNCatalogPublish(const char* key, const char* text)`: 1 when stored.
using PublishFn = int(__stdcall*)(const char*, const char*);

/**
 * @brief The text a key shows: markup and control characters made harmless, length bounded.
 * @param raw Text bytes.
 * @return Cleaned text.
 */
inline std::string clean(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() < max_text ? raw.size() : max_text);
    for (const char c : raw) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(byte < 0x20 || c == '$' || c == '[' || c == ']' ? ' ' : c);  // NOLINT(readability-magic-numbers)
    }
    if (out.size() > max_text) {
        std::size_t cut = max_text;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0U) == 0x80U) {  // NOLINT(readability-magic-numbers)
            --cut;  // do not split a UTF-8 sequence
        }
        out.resize(cut);
    }
    return out;
}

/// @brief Key -> interned text.
class Store {
public:
    /**
     * @brief Publishes a key's text (UI thread of the editor DLL, through the export).
     * @param key A catalog key (`CAPIE_V1_T__…`).
     * @param raw Text.
     * @return `false` for a key outside the grammar, the key cap, or an allocation failure.
     */
    bool publish(std::string_view key, std::string_view raw) noexcept {
        if (!key.starts_with(names::key_prefix) || key.size() > names::max_key) {
            return false;
        }
        try {
            const std::string text = clean(raw);
            const std::unique_lock lock(mutex_);
            const auto* interned = &*pool_.insert(text).first;
            auto found = answers_.find(key);
            if (found == answers_.end()) {
                if (answers_.size() >= max_keys) {
                    return false;
                }
                answers_.emplace(std::string(key), interned);
            } else {
                found->second = interned;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief The answer of a key (any thread; shared lock only).
     * @param key Key.
     * @return Interned text, or null when the key was never published.
     */
    const char* answer(std::string_view key) const noexcept {
        if (!key.starts_with(names::key_prefix)) {
            return nullptr;
        }
        try {
            const std::shared_lock lock(mutex_);
            const auto found = answers_.find(key);
            return found != answers_.end() ? found->second->c_str() : nullptr;
        } catch (...) {
            return nullptr;
        }
    }

    /// @brief Number of published keys (tests). @return Count.
    std::size_t size() const noexcept {
        try {
            const std::shared_lock lock(mutex_);
            return answers_.size();
        } catch (...) {
            return 0;
        }
    }

private:
    /// @brief Transparent hash for `string_view` lookups.
    struct Hash {
        using is_transparent = void;  ///< Heterogeneous lookup.
        /// @brief Hash. @param key Key. @return Value.
        std::size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
    };
    mutable std::shared_mutex mutex_;                                                  ///< Guards both tables.
    std::set<std::string, std::less<>> pool_;                                          ///< Interned texts (never erased).
    std::unordered_map<std::string, const std::string*, Hash, std::equal_to<>> answers_;  ///< Key -> text.
};

/**
 * @brief The process-wide store (created on first use, inside a hook or export).
 * @return Store.
 */
inline Store& store() {
    static Store instance;
    return instance;
}

}  // namespace capie::catalog::text
