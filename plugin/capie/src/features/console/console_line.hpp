/**
 * @file console_line.hpp
 * @brief Rebuilds a console line from the game's tokens and splits it into arguments.
 *
 * The game's tokenizer cuts the typed line at every space and does not know
 * quotes, so `"Neues Reich"` reaches a command as the two tokens `"Neues` and `Reich"`.
 * CAPIE joins the tokens again with single spaces and parses the result itself:
 *
 * - an argument is a bare word (no space, no quote) or a quoted string `"..."`;
 * - inside quotes `\"` is a quote and `\\` a backslash; any other backslash is kept;
 * - spaces separate arguments; a closing quote must be followed by a space or the end;
 * - a quote inside a bare word, an unterminated quote, too many or too long arguments
 *   are errors with the 1-based byte position where parsing stopped.
 *
 * The result holds bytes only. Text rules (alphabet, length in code points) are in
 * `console_text.hpp`. Pure functions over explicit-size views and fixed buffers: no
 * allocation, no Windows API, no game memory, no exceptions.
 */
#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace capie::console::line {

/// @brief Longest rebuilt line in bytes (the inline-effect delegate is built elsewhere).
inline constexpr std::size_t max_line_bytes = 1024;
/// @brief Most arguments after the command name (`capie_cmd <mod> <command>` plus 4).
inline constexpr std::size_t max_arguments = 6;
/// @brief Longest argument in bytes: 32 code points of up to 4 UTF-8 bytes each.
inline constexpr std::size_t max_argument_bytes = 128;
/// @brief Separator the game splits on and CAPIE joins with.
inline constexpr char space = ' ';
/// @brief Quote character.
inline constexpr char quote = '"';
/// @brief Escape character inside quotes.
inline constexpr char escape = '\\';

/// @brief Why a line could not be split.
enum class LineError {
    none,                ///< Parsed.
    line_too_long,       ///< Joined tokens exceed `max_line_bytes`.
    too_many_arguments,  ///< More than `max_arguments`.
    argument_too_long,   ///< One argument exceeds `max_argument_bytes`.
    unterminated_quote,  ///< A quoted string has no closing quote.
    stray_quote,         ///< A quote inside a bare word, or text right after a closing quote.
};

/// @brief One argument: bytes plus a terminator.
struct Argument {
    std::array<char, max_argument_bytes + 1> bytes{};  ///< Bytes, NUL-terminated.
    std::size_t size{};                                ///< Length without the terminator.
    bool quoted{};                                     ///< Written in quotes (may be empty).
    /// @brief The bytes. @return View of `size` bytes.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/// @brief A rebuilt line.
struct Line {
    std::array<char, max_line_bytes + 1> bytes{};  ///< Joined tokens, NUL-terminated.
    std::size_t size{};                            ///< Length without the terminator.
    /// @brief The bytes. @return View of `size` bytes.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/// @brief Result of `split`.
struct Arguments {
    std::array<Argument, max_arguments> items{};  ///< Parsed arguments.
    std::size_t count{};                          ///< Number of arguments.
    LineError error{LineError::none};             ///< First error.
    std::size_t position{};                       ///< 1-based byte position of the error, 0 when none.
    /// @brief Argument `index` as bytes. @param index Index below `count`. @return View.
    [[nodiscard]] std::string_view at(std::size_t index) const noexcept {
        return index < count ? items[index].view() : std::string_view{};
    }
};

/**
 * @brief Joins tokens with single spaces, exactly as typed when the game keeps empty tokens.
 * @param tokens Token views (the game's argv after the command name).
 * @param count Number of tokens.
 * @param out Receives the line.
 * @return `LineError::none`, or `line_too_long` (then `out` is empty).
 */
inline LineError join(const std::string_view* tokens, std::size_t count, Line& out) noexcept {
    out = Line{};
    std::size_t used = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t extra = tokens[index].size() + (index == 0 ? 0 : 1);
        if (extra > max_line_bytes - used) {
            out = Line{};
            return LineError::line_too_long;
        }
        if (index != 0) {
            out.bytes[used++] = space;
        }
        for (const char c : tokens[index]) {
            out.bytes[used++] = c;
        }
    }
    out.size = used;
    out.bytes[used] = '\0';
    return LineError::none;
}

/**
 * @brief Splits a line into bare and quoted arguments.
 * @param text Line bytes.
 * @return Arguments, or the first error with its position.
 */
inline Arguments split(std::string_view text) noexcept {
    Arguments result{};
    const auto fail = [&result](LineError error, std::size_t index) noexcept {
        result.error = error;
        result.position = index + 1;
        return result;
    };
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == space) {
            ++index;
            continue;
        }
        if (result.count == max_arguments) {
            return fail(LineError::too_many_arguments, index);
        }
        Argument& argument = result.items[result.count];
        const auto append = [&argument](char c) noexcept {
            if (argument.size == max_argument_bytes) {
                return false;
            }
            argument.bytes[argument.size++] = c;
            return true;
        };
        if (text[index] == quote) {
            argument.quoted = true;
            ++index;
            bool closed = false;
            while (index < text.size()) {
                char c = text[index];
                if (c == quote) {
                    closed = true;
                    ++index;
                    break;
                }
                if (c == escape && index + 1 < text.size() && (text[index + 1] == quote || text[index + 1] == escape)) {
                    c = text[++index];
                }
                if (!append(c)) {
                    return fail(LineError::argument_too_long, index);
                }
                ++index;
            }
            if (!closed) {
                return fail(LineError::unterminated_quote, text.size() - 1);
            }
            if (index < text.size() && text[index] != space) {
                return fail(LineError::stray_quote, index);
            }
        } else {
            while (index < text.size() && text[index] != space) {
                if (text[index] == quote) {
                    return fail(LineError::stray_quote, index);
                }
                if (!append(text[index])) {
                    return fail(LineError::argument_too_long, index);
                }
                ++index;
            }
        }
        argument.bytes[argument.size] = '\0';
        ++result.count;
    }
    return result;
}

/**
 * @brief English text of a line error for the console.
 * @param error Error.
 * @return Static message without position.
 */
constexpr std::string_view describe(LineError error) noexcept {
    switch (error) {
        case LineError::none:
            return "ok";
        case LineError::line_too_long:
            return "line too long";
        case LineError::too_many_arguments:
            return "too many arguments";
        case LineError::argument_too_long:
            return "argument longer than 128 bytes";
        case LineError::unterminated_quote:
            return "unterminated quote";
        case LineError::stray_quote:
            return "misplaced quote";
    }
    return "error";
}

}  // namespace capie::console::line
