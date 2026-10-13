/**
 * @file console_commands.hpp
 * @brief The CAPIE console commands and how each one becomes a call to a vanilla command.
 *
 * CAPIE never changes game state from a console action. Each command parses and checks its
 * arguments and then names one vanilla console command plus the exact tokens to give it
 * (a `Plan`). The native bridge looks that command up by name in the game's own table and
 * calls it (`console_bridge.hpp`):
 *
 *     change_nation_name <TAG> <name> [<adjective>]
 *         -> set_cosmetic_tag <TAG> RN1|<name>|<adjective>|      (adjective defaults to name)
 *     reset_nation_name <TAG>
 *         -> drop_cosmetic_tag <TAG>
 *     capie_cmd <mod> <command> [arg1 .. arg4]
 *         -> eval_effect <inline effect>   (records as in CONTRACT section 5.1, then
 *                                           capie_v1_cmd__<mod>__<command> = yes)
 *
 * The vanilla `set_cosmetic_tag` console action and the script effect Rename Anything uses
 * end in the same setter (`282540 -> 711260`, `361760 -> 711260`), so a console rename is
 * stored and saved exactly like an editor rename. The console itself is unavailable in
 * multiplayer and ironman; the game checks that before any command is looked up.
 *
 * Errors produce a message and no delegate. Pure functions over fixed buffers: no
 * allocation, no Windows API, no game memory, no exceptions.
 */
#pragma once
#include "../../core/game_build.hpp"
#include "console_line.hpp"
#include "console_text.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace capie::console {

/// @brief API level a console-originated mod command reports in `capie_cap` (candidate level 3).
inline constexpr unsigned console_api_level = 3;
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

/// @brief CAPIE's commands.
enum class CommandId {
    change_nation_name,  ///< Rename a country.
    reset_nation_name,   ///< Drop a country's cosmetic tag.
    mod_command,         ///< Run a mod's `capie_v1_cmd__<mod>__<command>` effect.
};
/// @brief Number of CAPIE commands.
inline constexpr std::size_t command_count = 3;

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

/// @brief The three commands CAPIE registers, in registration order.
inline constexpr std::array<CommandSpec, command_count> command_specs{{
    {CommandId::change_nation_name,
     "change_nation_name",
     "CAPIE: renames a country through an RN1 cosmetic tag, like Rename Anything. Single player only.",
     {"<TAG>", R"("<name>")", R"(["<adjective>"])"},
     3,
     R"(Usage: change_nation_name <TAG> "<name>" ["<adjective>"])"},
    {CommandId::reset_nation_name,
     "reset_nation_name",
     "CAPIE: drops the cosmetic tag of a country (any cosmetic tag, not only CAPIE names).",
     {"<TAG>", {}, {}},
     1,
     "Usage: reset_nation_name <TAG>"},
    {CommandId::mod_command,
     "capie_cmd",
     "CAPIE: runs the scripted effect capie_v1_cmd__<mod>__<command> with up to 4 text arguments.",
     {"<mod>", "<command>", R"(["<argument>" ...])"},
     3,
     R"(Usage: capie_cmd <mod> <command> ["<argument>" ...] (up to 4 arguments))"},
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

/**
 * @brief Plans `change_nation_name <TAG> <name> [<adjective>]`.
 * @param args Parsed arguments (after the command name).
 * @return Plan for `set_cosmetic_tag`, or a message.
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
    plan.delegate = Delegate::set_cosmetic_tag;
    plan.tokens[0].add(args.at(0));
    plan.tokens[1]
        .add(country::protocol_prefix)
        .add(args.at(1))
        .add(std::string_view(&country::field_delimiter, 1))
        .add(adjective_bytes)
        .add(std::string_view(&country::field_delimiter, 1));
    plan.token_count = 2;
    return plan;
}

/**
 * @brief Plans `reset_nation_name <TAG>`.
 * @param args Parsed arguments.
 * @return Plan for `drop_cosmetic_tag`, or a message.
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
    plan.delegate = Delegate::drop_cosmetic_tag;
    plan.tokens[0].add(args.at(0));
    plan.token_count = 1;
    return plan;
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
    effect.add(mod_effect_prefix).add(args.at(0)).add(id_separator).add(args.at(1)).add(" = yes");
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
    }
    return Plan{};
}

}  // namespace capie::console
