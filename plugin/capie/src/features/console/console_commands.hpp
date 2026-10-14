/**
 * @file console_commands.hpp
 * @brief The CAPIE console commands and how each one becomes a call to a vanilla command.
 *
 * CAPIE never changes game state from a console action. Each command parses and checks its
 * arguments and then names one vanilla console command plus the exact tokens to give it
 * (a `Plan`). The native bridge looks that command up by name in the game's own table and
 * calls it (`console_bridge.hpp`):
 *
 *     change_nation_name <TAG> <name> [<adjective>]          (adjective defaults to name)
 *         -> eval_effect "<TAG> = { clear_array = capie_cn_n clear_array = capie_cn_a
 *                         add_to_array = { capie_cn_n = <code> } ... (name, then adjective)
 *                         set_variable = { capie_cn_ver = 1 }
 *                         [drop_cosmetic_tag = yes] set_variable = { capie_cn_rev = 1 } }"
 *     reset_nation_name <TAG>
 *         -> eval_effect "<TAG> = { clear_array = capie_cn_n clear_array = capie_cn_a
 *                         clear_variable = capie_cn_ver
 *                         [drop_cosmetic_tag = yes] set_variable = { capie_cn_rev = 1 } }"
 *     change_nation_flag <TAG> <file>                        (flag layer, API level 6)
 *         -> eval_effect "<TAG> = { clear_array = capie_cf_s add_to_array = { capie_cf_s = <packed> } ...
 *                         set_variable = { capie_cf_len = <n> } clear_array = capie_cf_cos
 *                         [add_to_array = { capie_cf_cos = <packed> } ... set_variable = { capie_cf_cos_len = <n> }]
 *                         set_variable = { capie_cf_ver = 1 } set_variable = { capie_cf_rev = 1 } }"
 *            (snapshot = the country's current cosmetic tag, read by the bridge)
 *     reset_nation_flag <TAG>
 *         -> eval_effect "<TAG> = { clear_array ... clear_variable ... set_variable = { capie_cf_rev = 1 } }"
 *     capie_cmd <mod> <command> [arg1 .. arg4]
 *         -> eval_effect "FROM = { <records> capie_v1_cmd__<mod>__<command> = yes }"
 *            (records as in CONTRACT-console.md; FROM of eval_effect is the player's
 *            country, ROOT is whatever object is selected, re-evidence.md section 10)
 *
 * Both name commands write the country name layer of API level 4 (CONTRACT.md section 14):
 * the same variables an editor rename writes, so a console rename is stored, saved and shown
 * exactly like an editor rename, and the cosmetic tag (flag, colour) is left alone. The one
 * exception is a legacy name: when the country's current cosmetic tag is an `RN1|...` tag set
 * by CAPIE 1.0/1.1, the bridge reads it first and the effect also drops it
 * (`[drop_cosmetic_tag = yes]` above), which migrates the country to the layer. The console
 * itself is unavailable in multiplayer and ironman; the game checks that before any command
 * is looked up.
 *
 * Errors produce a message and no delegate. Pure functions over fixed buffers: no
 * allocation, no Windows API, no game memory, no exceptions.
 */
#pragma once
#include "../../core/game_build.hpp"
#include "../country/flag_layer.hpp"
#include "../editor/input_codes.hpp"
#include "console_line.hpp"
#include "console_text.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace capie::console {

/// @brief API level a console-originated mod command reports in `capie_cap` (the build's level, at least 3).
inline constexpr unsigned console_api_level = core::capie_api_level;
/// @brief Value of `capie_src` for a console-originated mod command (1 would be an editor Apply).
inline constexpr unsigned console_source = 2;
/// @brief Most text arguments of a mod command (same as editor fields).
inline constexpr std::size_t max_mod_arguments = 4;
/// @brief Longest delegate token in bytes (the inline effect of `capie_cmd`).
inline constexpr std::size_t max_delegate_bytes = 8192;
/// @brief Most delegate tokens.
inline constexpr std::size_t max_delegate_tokens = 2;
/// @brief Longest console message in bytes.
inline constexpr std::size_t max_message_bytes = 240;
/// @brief Prefix of every mod command effect (CONTRACT section 2 reserved names).
inline constexpr std::string_view mod_effect_prefix = "capie_v1_cmd__";
/// @brief Separator between mod id and command id.
inline constexpr std::string_view id_separator = "__";
/// @brief Mod id reserved for CAPIE's own SDK.
inline constexpr std::string_view reserved_mod_id = "capie";
/// @brief Opens the player scope around a mod command. `eval_effect` sets FROM to the player's
/// country (`[332F260]+520`, else `+524`, the expression `pp` uses for "the player") and ROOT
/// to the selected object (re-evidence.md section 10), so CAPIE switches to FROM explicitly.
inline constexpr std::string_view player_scope_open = "FROM = { ";
/// @brief Closes the player scope.
inline constexpr std::string_view player_scope_close = " }";
/// @brief Opens the country scope of a name command (after the tag).
inline constexpr std::string_view layer_scope_open = " = { ";
/// @brief Clears both code arrays of the name layer.
inline constexpr std::string_view layer_clear = "clear_array = capie_cn_n clear_array = capie_cn_a ";
/// @brief Marks the layer as set (format 1).
inline constexpr std::string_view layer_set_version = "set_variable = { capie_cn_ver = 1 } ";
/// @brief Removes the layer's format variable.
inline constexpr std::string_view layer_clear_version = "clear_variable = capie_cn_ver ";
/// @brief Drops a legacy `RN1|...` cosmetic tag (migration; only when the bridge saw one).
inline constexpr std::string_view legacy_drop = "drop_cosmetic_tag = yes ";
/// @brief Last statement of every layer change: asks CAPIE to refresh the names.
inline constexpr std::string_view layer_commit = "set_variable = { capie_cn_rev = 1 } }";

/**
 * @brief Whether a cosmetic tag is one CAPIE 1.0/1.1 (or Rename Anything) set: a legacy name.
 * @param cosmetic Current cosmetic tag bytes.
 * @return `true` for the `RN1|` protocol prefix.
 */
constexpr bool is_capie_cosmetic(std::string_view cosmetic) noexcept {
    return cosmetic.starts_with(country::protocol_prefix);
}

/// @brief CAPIE's commands.
enum class CommandId {
    change_nation_name,  ///< Rename a country.
    reset_nation_name,   ///< Drop a country's cosmetic tag.
    mod_command,         ///< Run a mod's `capie_v1_cmd__<mod>__<command>` effect.
    change_nation_flag,  ///< Give a country a flag file (flag layer, API level 6).
    reset_nation_flag,   ///< Remove the flag layer.
};
/// @brief Number of CAPIE commands.
inline constexpr std::size_t command_count = 5;

/// @brief Vanilla console commands CAPIE delegates to.
enum class Delegate {
    none,               ///< Nothing to call (error or usage).
    set_cosmetic_tag,   ///< `set_cosmetic_tag <TAG> <cosmetic>` (action 282540).
    drop_cosmetic_tag,  ///< `drop_cosmetic_tag <TAG>` (action 253220).
    eval_effect,        ///< `eval_effect <script>` (action 263690).
};

/**
 * @brief Name of a delegate as it appears in the game's command table.
 * @param delegate Delegate.
 * @return Name, empty for `none`.
 */
constexpr std::string_view delegate_name(Delegate delegate) noexcept {
    switch (delegate) {
        case Delegate::none:
            return {};
        case Delegate::set_cosmetic_tag:
            return "set_cosmetic_tag";
        case Delegate::drop_cosmetic_tag:
            return "drop_cosmetic_tag";
        case Delegate::eval_effect:
            return "eval_effect";
    }
    return {};
}

/// @brief Help data of one command, copied into the game's table at registration.
struct CommandSpec {
    CommandId id;                          ///< Command.
    std::string_view name;                 ///< Console name.
    std::string_view description;          ///< `help` text.
    std::array<std::string_view, 3> args;  ///< Argument names for `help`.
    std::size_t arg_count;                 ///< Used entries of `args`.
    std::string_view usage;                ///< Usage line printed on a wrong call.
};

/// @brief The commands CAPIE registers, in registration order.
inline constexpr std::array<CommandSpec, command_count> command_specs{{
    {CommandId::change_nation_name,
     "change_nation_name",
     "CAPIE: gives a country a name (its own layer; flag and colour unchanged), like Rename Anything. Single "
     "player only.",
     {"<TAG>", R"("<name>")", R"(["<adjective>"])"},
     3,
     R"(Usage: change_nation_name <TAG> "<name>" ["<adjective>"])"},
    {CommandId::reset_nation_name,
     "reset_nation_name",
     "CAPIE: removes a name set by change_nation_name or Rename Anything; the game's name comes back.",
     {"<TAG>", {}, {}},
     1,
     "Usage: reset_nation_name <TAG>"},
    {CommandId::mod_command,
     "capie_cmd",
     "CAPIE: runs scripted effect capie_v1_cmd__<mod>__<command> in the player's country, up to 4 text args.",
     {"<mod>", "<command>", R"(["<argument>" ...])"},
     3,
     R"(Usage: capie_cmd <mod> <command> ["<argument>" ...] (up to 4 arguments))"},
    {CommandId::change_nation_flag,
     "change_nation_flag",
     "CAPIE: shows gfx/flags/<file>.tga as a country's flag (its own layer; name and colour unchanged). Single "
     "player only.",
     {"<TAG>", "<file>", {}},
     2,
     "Usage: change_nation_flag <TAG> <file name without .tga, a-z A-Z 0-9 _>"},
    {CommandId::reset_nation_flag,
     "reset_nation_flag",
     "CAPIE: removes a flag set by change_nation_flag or a mod; the game's flag comes back.",
     {"<TAG>", {}, {}},
     1,
     "Usage: reset_nation_flag <TAG>"},
}};

/**
 * @brief Finds the spec of a command.
 * @param id Command.
 * @return Spec.
 */
constexpr const CommandSpec& spec(CommandId id) noexcept {
    return command_specs[static_cast<std::size_t>(id)];
}

/// @brief Bounded text builder used for messages and delegate tokens.
template <std::size_t Capacity>
struct Text {
    std::array<char, Capacity + 1> bytes{};  ///< Bytes, NUL-terminated.
    std::size_t size{};                      ///< Length without the terminator.
    bool overflow{};                         ///< An append did not fit; the text is incomplete.

    /// @brief Appends bytes. @param part Bytes. @return `*this`.
    Text& add(std::string_view part) noexcept {
        if (overflow || part.size() > Capacity - size) {
            overflow = true;
            return *this;
        }
        for (const char c : part) {
            bytes[size++] = c;
        }
        bytes[size] = '\0';
        return *this;
    }
    /// @brief Appends a decimal number. @param value Number. @return `*this`.
    Text& add_number(std::size_t value) noexcept {
        constexpr std::size_t digits_max = 20;
        constexpr std::size_t base = 10;
        std::array<char, digits_max> digits{};
        std::size_t count = 0;
        digits[count++] = static_cast<char>('0' + (value % base));
        value /= base;
        while (value != 0 && count < digits_max) {
            digits[count++] = static_cast<char>('0' + (value % base));
            value /= base;
        }
        while (count != 0) {
            const std::array<char, 1> one{digits[--count]};
            add(std::string_view(one.data(), 1));
        }
        return *this;
    }
    /// @brief The bytes. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/// @brief A console message.
using Message = Text<max_message_bytes>;

/// @brief What a command decided: a message, or a delegate and its tokens.
struct Plan {
    Delegate delegate{Delegate::none};                                   ///< Vanilla command to call.
    std::array<Text<max_delegate_bytes>, max_delegate_tokens> tokens{};  ///< Its tokens.
    std::size_t token_count{};                                           ///< Used tokens.
    Message message{};                                                   ///< Error or usage text (when `none`).
    bool needs_cosmetic{};         ///< The bridge must read `tag`'s cosmetic tag and call `finish_plan`.
    bool flag_plan{};              ///< A flag-layer plan (`finish_flag_plan`), else a name-layer plan.
    Text<text::tag_length> tag{};  ///< Country tag of a name command.
};

namespace detail {

/**
 * @brief Writes a text error as "CAPIE: <what>: <reason> at character <n>".
 * @param out Message.
 * @param what Argument label.
 * @param check Failed check.
 */
inline void text_error(Message& out, std::string_view what, const text::TextCheck& check) noexcept {
    out.add("CAPIE: ").add(what).add(": ").add(text::describe(check.error));
    if (check.position != 0) {
        out.add(" at character ").add_number(check.position);
    }
}

/**
 * @brief Writes "CAPIE: <line error> at byte <n>".
 * @param out Message.
 * @param args Failed split.
 */
inline void line_error(Message& out, const line::Arguments& args) noexcept {
    out.add("CAPIE: ").add(line::describe(args.error)).add(" at byte ").add_number(args.position);
}

/**
 * @brief Appends one `set_temp_variable` record to an inline effect.
 * @param out Effect text.
 * @param name Record name.
 * @param value Record value.
 */
template <std::size_t Capacity>
void record(Text<Capacity>& out, std::string_view name, std::size_t value) noexcept {
    out.add("set_temp_variable = { ").add(name).add(" = ").add_number(value).add(" } ");
}

/**
 * @brief Appends a field record `capie_f<n>_<suffix>`.
 * @param out Effect text.
 * @param field 1-based field number.
 * @param suffix `err`, `at`, `len` or a character index.
 * @param value Value.
 */
template <std::size_t Capacity>
void field_record(Text<Capacity>& out, std::size_t field, std::string_view suffix, std::size_t value) noexcept {
    out.add("set_temp_variable = { capie_f")
        .add_number(field)
        .add("_")
        .add(suffix)
        .add(" = ")
        .add_number(value)
        .add(" } ");
}

}  // namespace detail

namespace detail {

/**
 * @brief Appends `add_to_array = { <array> = <code> }` for every code of a text.
 * @param out Effect text.
 * @param array Array name.
 * @param codes Codes.
 */
template <std::size_t Capacity>
void add_codes(Text<Capacity>& out, std::string_view array, const editor::FieldCodes& codes) noexcept {
    for (std::size_t index = 0; index < codes.length; ++index) {
        out.add("add_to_array = { ").add(array).add(" = ").add_number(codes.codes[index]).add(" } ");
    }
}

/**
 * @brief Starts a name-layer effect: `<TAG> = { clear_array ... clear_array ... `.
 * @param plan Plan (delegate, tag and the first token are set).
 * @param tag Validated tag.
 */
inline void start_layer_plan(Plan& plan, std::string_view tag) noexcept {
    plan.delegate = Delegate::eval_effect;
    plan.token_count = 1;
    plan.needs_cosmetic = true;
    plan.tag.add(tag);
    plan.tokens[0].add(tag).add(layer_scope_open).add(layer_clear);
}

}  // namespace detail

/**
 * @brief Plans `change_nation_name <TAG> <name> [<adjective>]`.
 * @param args Parsed arguments (after the command name).
 * @return Plan for `eval_effect` (unfinished: the bridge calls `finish_layer_plan`), or a message.
 */
inline Plan plan_change_nation_name(const line::Arguments& args) noexcept {
    Plan plan{};
    if (args.count < 2 || args.count > 3) {
        plan.message.add(spec(CommandId::change_nation_name).usage);
        return plan;
    }
    if (!text::valid_tag(args.at(0))) {
        plan.message.add("CAPIE: invalid tag (expected 3 characters A-Z or 0-9)");
        return plan;
    }
    const auto name = text::check_country_text(args.at(1));
    if (name.error != text::TextError::none) {
        detail::text_error(plan.message, "name", name);
        return plan;
    }
    const std::string_view adjective_bytes = args.count == 3 ? args.at(2) : args.at(1);
    const auto adjective = text::check_country_text(adjective_bytes);
    if (adjective.error != text::TextError::none) {
        detail::text_error(plan.message, "adjective", adjective);
        return plan;
    }
    detail::start_layer_plan(plan, args.at(0));
    detail::add_codes(plan.tokens[0], "capie_cn_n", name.codes);
    detail::add_codes(plan.tokens[0], "capie_cn_a", adjective.codes);
    plan.tokens[0].add(layer_set_version);
    return plan;
}

/**
 * @brief Plans `reset_nation_name <TAG>`: removes the name layer (and a legacy RN1 tag).
 * @param args Parsed arguments.
 * @return Plan for `eval_effect` (unfinished: the bridge calls `finish_layer_plan`), or a message.
 */
inline Plan plan_reset_nation_name(const line::Arguments& args) noexcept {
    Plan plan{};
    if (args.count != 1) {
        plan.message.add(spec(CommandId::reset_nation_name).usage);
        return plan;
    }
    if (!text::valid_tag(args.at(0))) {
        plan.message.add("CAPIE: invalid tag (expected 3 characters A-Z or 0-9)");
        return plan;
    }
    detail::start_layer_plan(plan, args.at(0));
    plan.tokens[0].add(layer_clear_version);
    return plan;
}

/**
 * @brief Completes a name-layer effect once the bridge has read the country's cosmetic tag.
 * @param plan Plan from `plan_change_nation_name` or `plan_reset_nation_name`.
 * @param legacy_rn1 The current cosmetic tag is a legacy `RN1|...` tag (`is_capie_cosmetic`).
 * @note Drops only CAPIE's own legacy tag; a cosmetic tag of the game or a mod is never touched.
 */
inline void finish_layer_plan(Plan& plan, bool legacy_rn1) noexcept {
    if (legacy_rn1) {
        plan.tokens[0].add(legacy_drop);
    }
    plan.tokens[0].add(layer_commit);
    plan.needs_cosmetic = false;
    if (plan.tokens[0].overflow) {
        plan = Plan{};
        plan.message.add("CAPIE: name too long for one command");
    }
}

/// @brief Opens a flag-layer effect after the tag: clears the stem array.
inline constexpr std::string_view flag_clear_stem = " = { clear_array = capie_cf_s ";
/// @brief Removes the whole flag layer.
inline constexpr std::string_view flag_clear_all =
    "clear_array = capie_cf_s clear_array = capie_cf_cos clear_variable = capie_cf_len "
    "clear_variable = capie_cf_cos_len clear_variable = capie_cf_ver ";
/// @brief Last statement of every flag-layer change.
inline constexpr std::string_view flag_commit = "set_variable = { capie_cf_rev = 1 } }";

namespace detail {

/**
 * @brief Appends `add_to_array = { <array> = <element> }` for a text packed three characters per element.
 * @param out Effect text.
 * @param array Array name.
 * @param text Text in the file-name alphabet (checked by the caller).
 */
template <std::size_t Capacity>
void add_packed(Text<Capacity>& out, std::string_view array, std::string_view text) noexcept {
    for (std::size_t start = 0; start < text.size(); start += country::flag::chars_per_element) {
        std::int64_t element = 0;
        for (std::size_t slot = 0; slot < country::flag::chars_per_element; ++slot) {
            const std::size_t at = start + slot;
            element = (element * country::flag::pack_base) + (at < text.size() ? country::flag::code_of(text[at]) : 0);
        }
        out.add("add_to_array = { ").add(array).add(" = ").add_number(static_cast<std::size_t>(element)).add(" } ");
    }
}

/**
 * @brief Whether a text can be stored in the file-name alphabet.
 * @param text Text.
 * @param allow_empty `true` for the cosmetic snapshot.
 * @return `true` for 0/1..63 characters from `a-z A-Z 0-9 _`.
 */
constexpr bool packable(std::string_view text, bool allow_empty) noexcept {
    if ((!allow_empty && text.empty()) || text.size() > country::flag::max_chars) {
        return false;
    }
    for (const char c : text) {
        if (country::flag::code_of(c) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace detail

/**
 * @brief Plans `change_nation_flag <TAG> <file>`.
 * @param args Parsed arguments.
 * @return Plan for `eval_effect` (unfinished: the bridge calls `finish_plan`), or a message.
 */
inline Plan plan_change_nation_flag(const line::Arguments& args) noexcept {
    Plan plan{};
    if (args.count != 2) {
        plan.message.add(spec(CommandId::change_nation_flag).usage);
        return plan;
    }
    if (!text::valid_tag(args.at(0))) {
        plan.message.add("CAPIE: invalid tag (expected 3 characters A-Z or 0-9)");
        return plan;
    }
    if (!detail::packable(args.at(1), false)) {
        plan.message.add("CAPIE: invalid flag file (1-63 characters a-z A-Z 0-9 _, without .tga)");
        return plan;
    }
    plan.delegate = Delegate::eval_effect;
    plan.token_count = 1;
    plan.needs_cosmetic = true;
    plan.flag_plan = true;
    plan.tag.add(args.at(0));
    auto& effect = plan.tokens[0];
    effect.add(args.at(0)).add(flag_clear_stem);
    detail::add_packed(effect, "capie_cf_s", args.at(1));
    effect.add("set_variable = { capie_cf_len = ").add_number(args.at(1).size()).add(" } ");
    return plan;
}

/**
 * @brief Plans `reset_nation_flag <TAG>`: removes the flag layer.
 * @param args Parsed arguments.
 * @return Plan for `eval_effect` (unfinished), or a message.
 */
inline Plan plan_reset_nation_flag(const line::Arguments& args) noexcept {
    Plan plan{};
    if (args.count != 1) {
        plan.message.add(spec(CommandId::reset_nation_flag).usage);
        return plan;
    }
    if (!text::valid_tag(args.at(0))) {
        plan.message.add("CAPIE: invalid tag (expected 3 characters A-Z or 0-9)");
        return plan;
    }
    plan.delegate = Delegate::eval_effect;
    plan.token_count = 1;
    plan.needs_cosmetic = true;
    plan.flag_plan = true;
    plan.tag.add(args.at(0));
    plan.tokens[0].add(args.at(0)).add(" = { ").add(flag_clear_all).add(flag_commit);
    return plan;
}

/**
 * @brief Completes a flag effect with the cosmetic snapshot (CONTRACT.md 17.4) and the commit.
 * @param plan Plan from `plan_change_nation_flag` (a reset plan is already complete).
 * @param cosmetic The country's current cosmetic tag.
 */
inline void finish_flag_plan(Plan& plan, std::string_view cosmetic) noexcept {
    plan.needs_cosmetic = false;
    auto& effect = plan.tokens[0];
    if (effect.view().ends_with(flag_commit)) {
        return;  // reset: no snapshot
    }
    effect.add("clear_array = capie_cf_cos ");
    if (detail::packable(cosmetic, true)) {
        detail::add_packed(effect, "capie_cf_cos", cosmetic);
        effect.add("set_variable = { capie_cf_cos_len = ").add_number(cosmetic.size()).add(" } ");
    } else {
        effect.add("clear_variable = capie_cf_cos_len ");  // a tag the alphabet cannot hold: no snapshot
    }
    effect.add("set_variable = { capie_cf_ver = 1 } ").add(flag_commit);
    if (effect.overflow) {
        plan = Plan{};
        plan.message.add("CAPIE: flag file too long for one command");
    }
}

/**
 * @brief Completes a name or flag plan once the bridge has read the country's cosmetic tag.
 * @param plan Plan with `needs_cosmetic`.
 * @param cosmetic Current cosmetic tag.
 */
inline void finish_plan(Plan& plan, std::string_view cosmetic) noexcept {
    if (plan.flag_plan) {
        finish_flag_plan(plan, cosmetic);
    } else {
        finish_layer_plan(plan, is_capie_cosmetic(cosmetic));
    }
}

/**
 * @brief Plans `capie_cmd <mod> <command> [arg1 .. arg4]` as one inline effect.
 * @param args Parsed arguments.
 * @return Plan for `eval_effect`, or a message.
 */
inline Plan plan_mod_command(const line::Arguments& args) noexcept {
    Plan plan{};
    if (args.count < 2 || args.count > 2 + max_mod_arguments) {
        plan.message.add(spec(CommandId::mod_command).usage);
        return plan;
    }
    if (args.items[0].quoted || !text::valid_id(args.at(0)) || args.at(0) == reserved_mod_id) {
        plan.message.add("CAPIE: invalid mod id (1-12 characters a-z or 0-9, not 'capie')");
        return plan;
    }
    if (args.items[1].quoted || !text::valid_id(args.at(1))) {
        plan.message.add("CAPIE: invalid command id (1-12 characters a-z or 0-9)");
        return plan;
    }
    const std::size_t fields = args.count - 2;
    std::array<text::TextCheck, max_mod_arguments> checks{};
    for (std::size_t field = 0; field < fields; ++field) {
        checks[field] = text::check_text(args.at(2 + field), true);
        if (checks[field].error != text::TextError::none) {
            Text<max_message_bytes> label{};
            label.add("argument ").add_number(field + 1);
            detail::text_error(plan.message, label.view(), checks[field]);
            return plan;
        }
    }
    auto& effect = plan.tokens[0];
    effect.add(player_scope_open);
    detail::record(effect, "capie_cap", console_api_level);
    detail::record(effect, "capie_ver", core::capie_build_number);
    detail::record(effect, "capie_src", console_source);
    detail::record(effect, "capie_nf", fields);
    for (std::size_t field = 0; field < fields; ++field) {
        const auto& codes = checks[field].codes;
        detail::field_record(effect, field + 1, "err", 0);
        detail::field_record(effect, field + 1, "at", 0);
        detail::field_record(effect, field + 1, "len", codes.length);
        for (std::size_t index = 0; index < codes.length; ++index) {
            Text<text::max_id> suffix{};
            suffix.add_number(index);
            detail::field_record(effect, field + 1, suffix.view(), codes.codes[index]);
        }
    }
    effect.add(mod_effect_prefix)
        .add(args.at(0))
        .add(id_separator)
        .add(args.at(1))
        .add(" = yes")
        .add(player_scope_close);
    if (effect.overflow) {
        plan = Plan{};
        plan.message.add("CAPIE: arguments too long for one command");
        return plan;
    }
    plan.delegate = Delegate::eval_effect;
    plan.token_count = 1;
    return plan;
}

/**
 * @brief Plans one command from the game's tokens (everything after the command name).
 * @param id Command.
 * @param tokens Token views.
 * @param count Number of tokens.
 * @return Plan or message.
 */
inline Plan plan_command(CommandId id, const std::string_view* tokens, std::size_t count) noexcept {
    line::Line joined{};
    if (const auto error = line::join(tokens, count, joined); error != line::LineError::none) {
        Plan plan{};
        plan.message.add("CAPIE: ").add(line::describe(error));
        return plan;
    }
    const auto args = line::split(joined.view());
    if (args.error != line::LineError::none) {
        Plan plan{};
        detail::line_error(plan.message, args);
        return plan;
    }
    switch (id) {
        case CommandId::change_nation_name:
            return plan_change_nation_name(args);
        case CommandId::reset_nation_name:
            return plan_reset_nation_name(args);
        case CommandId::mod_command:
            return plan_mod_command(args);
        case CommandId::change_nation_flag:
            return plan_change_nation_flag(args);
        case CommandId::reset_nation_flag:
            return plan_reset_nation_flag(args);
    }
    return Plan{};
}

}  // namespace capie::console
