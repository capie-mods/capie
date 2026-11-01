/**
 * @file catalog_names.hpp
 * @brief Name grammar of the runtime catalog views (API level 9, CONTRACT.md 21.2) and its exact parser.
 *
 *     scripted GUI   capie_v1_cat__<mod>__<view>                  (ids [a-z0-9]{1,12})
 *     action         capie_v1_cat__<mod>__<view>__<action>_click
 *     actions        flag | flagown | portrait | portraitown | vp | first | prev | next | last | pick
 *     text keys      CAPIE_V1_T__<MOD>__<VIEW>__<k> | __SEL | __PAGE | __COUNT   (upper case, k decimal)
 *
 * As for the editor grammar (`editor/public_names.hpp`), a name is accepted only when the whole
 * string matches: no substring search, no case folding. Every name CAPIE compares later is built
 * from the parsed ids. Pure functions over explicit-size views; no allocation, no game memory.
 */
#pragma once
#include "../editor/public_names.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>

namespace capie::catalog::names {

using editor::grammar::EditorId;
using editor::grammar::Name;

/// @brief Prefix of a catalog view's scripted GUI (model) name.
inline constexpr std::string_view model_prefix = "capie_v1_cat__";
/// @brief Separator between ids and the action.
inline constexpr std::string_view separator = "__";
/// @brief Suffix of every action selector.
inline constexpr std::string_view click_suffix = "_click";
/// @brief Prefix of the text keys CAPIE answers for a view.
inline constexpr std::string_view key_prefix = "CAPIE_V1_T__";
/// @brief Longest id.
inline constexpr std::size_t max_id = editor::grammar::max_id;
/// @brief Shortest model name.
inline constexpr std::size_t min_model_name = model_prefix.size() + 1 + separator.size() + 1;
/// @brief Longest model name (40 bytes).
inline constexpr std::size_t max_model_name = model_prefix.size() + max_id + separator.size() + max_id;
/// @brief Longest action name (`portraitown`).
inline constexpr std::size_t max_action = 11;
/// @brief Longest selector (59 bytes).
inline constexpr std::size_t max_selector = max_model_name + separator.size() + max_action + click_suffix.size();
static_assert(max_selector < editor::grammar::name_capacity);

/// @brief Actions of a catalog view (CONTRACT.md 21.2).
enum class Action : unsigned {
    flag,         ///< Flag catalog, all (pending spikes).
    flagown,      ///< Flag catalog, the scope country's own (pending spikes).
    portrait,     ///< Portrait catalog, all (pending spikes).
    portraitown,  ///< Portrait catalog, own (pending spikes).
    vp,           ///< The VP provinces of the scope state (implemented).
    first,        ///< Page (pending spikes).
    prev,         ///< Page (pending spikes).
    next,         ///< Page (pending spikes).
    last,         ///< Page (pending spikes).
    pick,         ///< Pick a slot (pending spikes).
    count,        ///< Number of actions.
};

/// @brief Action names, in `Action` order.
inline constexpr std::array<std::string_view, static_cast<std::size_t>(Action::count)> action_names = {
    "flag", "flagown", "portrait", "portraitown", "vp", "first", "prev", "next", "last", "pick"};

/**
 * @brief Cheap pre-filter: could this be a catalog model name? (prefix and length only).
 * @param name Candidate.
 * @return `true` when the length is in range and the prefix matches.
 */
constexpr bool may_be_model(std::string_view name) noexcept {
    return name.size() >= min_model_name && name.size() <= max_model_name && name.starts_with(model_prefix);
}

/**
 * @brief Parses a catalog model name exactly.
 * @param name Candidate.
 * @return The ids (`editor` holds the view id), or `nullopt`.
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
    const auto view = rest.substr(split + separator.size());
    if (!editor::grammar::valid_id(mod) || !editor::grammar::valid_id(view)) {
        return std::nullopt;
    }
    EditorId id{};
    mod.copy(id.mod.data(), mod.size());
    view.copy(id.editor.data(), view.size());
    id.mod_size = mod.size();
    id.editor_size = view.size();
    return id;
}

/**
 * @brief The model name of a view.
 * @param id Ids.
 * @return `capie_v1_cat__<mod>__<view>`.
 */
inline Name view_model(const EditorId& id) noexcept {
    return editor::grammar::build(model_prefix, id, 0, {});
}

/**
 * @brief The selector of an action.
 * @param id Ids.
 * @param action Action.
 * @return `capie_v1_cat__<mod>__<view>__<action>_click`.
 */
inline Name view_action(const EditorId& id, Action action) noexcept {
    Name out = view_model(id);
    if (out.size == 0 || action >= Action::count) {
        return {};
    }
    const auto verb = action_names[static_cast<std::size_t>(action)];
    const int tail = std::snprintf(out.text.data() + out.size, out.text.size() - out.size, "%.*s%.*s%.*s",
                                   static_cast<int>(separator.size()), separator.data(), static_cast<int>(verb.size()),
                                   verb.data(), static_cast<int>(click_suffix.size()), click_suffix.data());
    if (tail <= 0 || out.size + static_cast<std::size_t>(tail) >= out.text.size()) {
        return {};
    }
    out.size += static_cast<std::size_t>(tail);
    return out;
}

/**
 * @brief Parses the action of a selector against a parsed model.
 * @param id Ids of the clicked view.
 * @param selector Selector bytes.
 * @return The action, or `nullopt` when the selector is not exactly one action of that view.
 */
inline std::optional<Action> parse_action(const EditorId& id, std::string_view selector) noexcept {
    const auto model = view_model(id);
    if (model.size == 0 || !selector.starts_with(model.view())) {
        return std::nullopt;
    }
    auto rest = selector.substr(model.size);
    if (!rest.starts_with(separator) || !rest.ends_with(click_suffix)) {
        return std::nullopt;
    }
    rest = rest.substr(separator.size(), rest.size() - separator.size() - click_suffix.size());
    for (std::size_t i = 0; i < action_names.size(); ++i) {
        if (rest == action_names[i]) {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

/// @brief Longest text key: prefix, two ids, two separators and `COUNT`.
inline constexpr std::size_t max_key = key_prefix.size() + max_id + separator.size() + max_id + separator.size() + 5;

/// @brief A built text key.
struct Key {
    std::array<char, max_key + 1> text{};  ///< Bytes plus terminator.
    std::size_t size{};                    ///< Length.
    /// @brief The key. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {text.data(), size}; }
    /// @brief C string. @return Terminated bytes.
    [[nodiscard]] const char* c_str() const noexcept { return text.data(); }
};

/**
 * @brief Builds `CAPIE_V1_T__<MOD>__<VIEW>__<suffix>` with upper-case ids.
 * @param id Ids.
 * @param suffix `0`..`47`, `SEL`, `PAGE` or `COUNT`.
 * @return The key; empty when it would not fit.
 */
inline Key text_key(const EditorId& id, std::string_view suffix) noexcept {
    Key out{};
    const auto put = [&out](std::string_view part, bool upper) {
        for (const char c : part) {
            if (out.size >= max_key) {
                return false;
            }
            out.text[out.size++] = upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        return true;
    };
    if (suffix.empty() || suffix.size() > 5 || !put(key_prefix, false) || !put(id.mod_view(), true) ||  // NOLINT(readability-magic-numbers)
        !put(separator, false) || !put(id.editor_view(), true) || !put(separator, false) || !put(suffix, false)) {
        return {};
    }
    out.text[out.size] = '\0';
    return out;
}

/**
 * @brief Builds the key of slot (row) `k`.
 * @param id Ids.
 * @param k Row 0..47.
 * @return The key.
 */
inline Key row_key(const EditorId& id, std::size_t k) noexcept {
    std::array<char, 4> digits{};
    const int written = std::snprintf(digits.data(), digits.size(), "%zu", k);
    if (written <= 0 || static_cast<std::size_t>(written) >= digits.size()) {
        return {};
    }
    return text_key(id, std::string_view(digits.data(), static_cast<std::size_t>(written)));
}

}  // namespace capie::catalog::names
