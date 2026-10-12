/**
 * @file public_names.hpp
 * @brief The editor name grammar of the public contract (API level 2) and its exact parser.
 *
 * Any mod can give CAPIE a text editor by naming its scripted GUI and widgets after one
 * grammar (`plugin/capie/CONTRACT.md`, section 3):
 *
 *     scripted GUI       capie_v1_ed__<mod>__<editor>
 *     Apply effect       capie_v1_ed__<mod>__<editor>_apply_click
 *     edit box n         capie_v1_f__<mod>__<editor>__<n>     n = 1..4
 *     prefill carrier n  capie_v1_pf__<mod>__<editor>__<n>
 *     epoch carrier      capie_v1_ep__<mod>__<editor>
 *
 * with `<mod>` and `<editor>` made of 1 to 12 characters from `[a-z0-9]`. The parser
 * accepts a model name only when the whole string matches; there is no substring search
 * and no case folding, so `capie_v1_ed__MyMod__x`, `capie_v1_ed__a__b__c` or a trailing
 * space are simply not editors. Every other name an editor uses is built from the parsed
 * ids, so CAPIE compares the game's strings with names it built itself.
 *
 * Pure functions over explicit-size views and fixed buffers: no allocation, no Windows
 * API, no game memory. The offline tests run this file directly.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>

namespace capie::editor::grammar {

/// @brief Prefix of an editor's scripted GUI (model) name.
inline constexpr std::string_view model_prefix = "capie_v1_ed__";
/// @brief Prefix of an editor's edit-box names.
inline constexpr std::string_view field_prefix = "capie_v1_f__";
/// @brief Prefix of an editor's prefill carriers.
inline constexpr std::string_view prefill_prefix = "capie_v1_pf__";
/// @brief Prefix of an editor's epoch carrier.
inline constexpr std::string_view epoch_prefix = "capie_v1_ep__";
/// @brief Separator between the mod id, the editor id and the field number.
inline constexpr std::string_view separator = "__";
/// @brief Suffix of the Apply selector (the scripted GUI effect name).
inline constexpr std::string_view apply_suffix = "_apply_click";
/// @brief Longest mod or editor id.
inline constexpr std::size_t max_id = 12;
/// @brief Most fields one editor can have.
inline constexpr std::size_t max_fields = 4;
/// @brief Mod id reserved for the SDK's own windows.
inline constexpr std::string_view sdk_mod_id = "capie";

/// @brief Longest model name (39 bytes).
inline constexpr std::size_t max_model_name = model_prefix.size() + max_id + separator.size() + max_id;
/// @brief Longest Apply selector (51 bytes).
inline constexpr std::size_t max_apply_name = max_model_name + apply_suffix.size();
/// @brief Shortest model name (17 bytes).
inline constexpr std::size_t min_model_name = model_prefix.size() + 1 + separator.size() + 1;
/// @brief Capacity of a built name, terminator included.
inline constexpr std::size_t name_capacity = 64;
static_assert(max_apply_name < name_capacity);

/// @brief A NUL-terminated name built from parsed ids.
struct Name {
    std::array<char, name_capacity> text{};  ///< Bytes plus terminator.
    std::size_t size{};                      ///< Length without the terminator.
    /// @brief The bytes. @return View of `size` bytes.
    [[nodiscard]] std::string_view view() const noexcept { return {text.data(), size}; }
    /// @brief C string for game lookups. @return Terminated bytes.
    [[nodiscard]] const char* c_str() const noexcept { return text.data(); }
};

/// @brief The two ids of a parsed model name.
struct EditorId {
    std::array<char, max_id + 1> mod{};     ///< Mod id, terminated.
    std::array<char, max_id + 1> editor{};  ///< Editor id, terminated.
    std::size_t mod_size{};                 ///< Mod id length.
    std::size_t editor_size{};              ///< Editor id length.
    /// @brief Mod id. @return View.
    [[nodiscard]] std::string_view mod_view() const noexcept { return {mod.data(), mod_size}; }
    /// @brief Editor id. @return View.
    [[nodiscard]] std::string_view editor_view() const noexcept { return {editor.data(), editor_size}; }
};

/**
 * @brief Whether a string is a valid id.
 * @param id Candidate.
 * @return `true` for 1..12 characters from `[a-z0-9]`.
 */
constexpr bool valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > max_id) {
        return false;
    }
    return std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

/**
 * @brief Cheap pre-filter: could this be a model name? (prefix and length only).
 * @param name Candidate bytes.
 * @return `true` when the length is in range and the prefix matches.
 */
constexpr bool may_be_model(std::string_view name) noexcept {
    return name.size() >= min_model_name && name.size() <= max_model_name && name.starts_with(model_prefix);
}

/**
 * @brief Parses a model name exactly.
 * @param name Candidate (explicit size, no terminator needed).
 * @return The ids, or `nullopt` unless the whole string is `capie_v1_ed__<mod>__<editor>`.
 */
inline std::optional<EditorId> parse_model(std::string_view name) noexcept {
    if (!may_be_model(name)) {
        return std::nullopt;
    }
    const auto rest = name.substr(model_prefix.size());
    const auto split = rest.find(separator);
    if (split == std::string_view::npos) {
        return std::nullopt;
    }
    const auto mod = rest.substr(0, split);
    const auto editor = rest.substr(split + separator.size());
    if (!valid_id(mod) || !valid_id(editor)) {
        return std::nullopt;
    }
    EditorId id{};
    mod.copy(id.mod.data(), mod.size());
    editor.copy(id.editor.data(), editor.size());
    id.mod_size = mod.size();
    id.editor_size = editor.size();
    return id;
}

/**
 * @brief Builds `<prefix><mod>__<editor><suffix>` with an optional `__<n>` field number.
 * @param prefix Name prefix.
 * @param id Parsed ids.
 * @param field Field number 1..4, or 0 for none.
 * @param suffix Name suffix (may be empty).
 * @return The built name; empty when it would not fit (cannot happen for valid ids).
 */
inline Name build(std::string_view prefix, const EditorId& id, std::size_t field, std::string_view suffix) noexcept {
    Name out{};
    const int written =
        std::snprintf(out.text.data(), out.text.size(), "%.*s%.*s%.*s%.*s", static_cast<int>(prefix.size()),
                      prefix.data(), static_cast<int>(id.mod_size), id.mod.data(), static_cast<int>(separator.size()),
                      separator.data(), static_cast<int>(id.editor_size), id.editor.data());
    if (written <= 0 || static_cast<std::size_t>(written) >= out.text.size()) {
        return {};
    }
    auto size = static_cast<std::size_t>(written);
    int tail = 0;
    if (field != 0) {
        tail = std::snprintf(out.text.data() + size, out.text.size() - size, "%.*s%zu",
                             static_cast<int>(separator.size()), separator.data(), field);
    } else {
        tail = std::snprintf(out.text.data() + size, out.text.size() - size, "%.*s", static_cast<int>(suffix.size()),
                             suffix.data());
    }
    if (tail < 0 || size + static_cast<std::size_t>(tail) >= out.text.size()) {
        return {};
    }
    out.size = size + static_cast<std::size_t>(tail);
    return out;
}

/// @brief Model name. @param id Ids. @return `capie_v1_ed__<mod>__<editor>`.
inline Name model_name(const EditorId& id) noexcept {
    return build(model_prefix, id, 0, {});
}
/// @brief Apply selector. @param id Ids. @return `capie_v1_ed__<mod>__<editor>_apply_click`.
inline Name apply_name(const EditorId& id) noexcept {
    return build(model_prefix, id, 0, apply_suffix);
}
/// @brief Epoch carrier. @param id Ids. @return `capie_v1_ep__<mod>__<editor>`.
inline Name epoch_name(const EditorId& id) noexcept {
    return build(epoch_prefix, id, 0, {});
}
/// @brief Edit box. @param id Ids. @param n Field 1..4. @return `capie_v1_f__<mod>__<editor>__<n>`.
inline Name field_name(const EditorId& id, std::size_t n) noexcept {
    return build(field_prefix, id, n, {});
}
/// @brief Prefill carrier. @param id Ids. @param n Field 1..4. @return `capie_v1_pf__<mod>__<editor>__<n>`.
inline Name prefill_name(const EditorId& id, std::size_t n) noexcept {
    return build(prefill_prefix, id, n, {});
}

}  // namespace capie::editor::grammar
