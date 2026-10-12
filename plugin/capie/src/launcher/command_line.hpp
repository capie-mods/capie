/**
 * @file command_line.hpp
 * @brief Splitting the launcher's own command line and rebuilding the wrapped one without changing it.
 *
 * Steam runs `"...\RenameAnythingLauncher.exe" %command%`, where `%command%` expands to the
 * game's own start command (today the Paradox Launcher, `dowser.exe`, with its arguments).
 * The launcher must pass that command on exactly as Steam produced it. Re-quoting parsed
 * arguments could change them in corner cases (embedded quotes, trailing backslashes), so
 * the launcher never does that for the wrapped command: it finds where the program token
 * starts in the raw command line, reads the program path with the same rule Windows uses
 * for a program name (`argv[0]`: quotes delimit, backslashes are literal) and keeps every
 * character after it verbatim. The child's command line is the program in quotes plus that
 * verbatim tail, and the child parses its arguments exactly as it would have under Steam.
 *
 * Launcher options (`--help`, `--verify-only`, `--game ABS`, `--no-dialogs`) may only come
 * before the program. They are parsed with the standard argument rules (the ones of
 * `CommandLineToArgvW`), implemented here so the parser also knows where each token starts.
 * The tests compare this tokenizer with `CommandLineToArgvW` on hostile inputs.
 *
 * If Steam ever passed an unquoted program path with spaces, the first token would be a
 * fragment such as `C:\Program`. `resolve_program` then tries the longer prefixes, the way
 * `CreateProcess` resolves an unquoted name, but only accepts a prefix that ends in
 * `\dowser.exe` or `\hoi4.exe` and exists. It never searches PATH or the working folder.
 */
#pragma once
#include "config.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace capie::launcher {

/// @brief One argument and where it is in the raw command line.
struct Token {
    std::size_t begin = 0;  ///< index of the first character
    std::size_t end = 0;    ///< index after the last character
    std::wstring value;     ///< decoded value
};

/**
 * @brief Whether a character separates arguments.
 * @param ch Character.
 * @return `true` for space and tab.
 */
constexpr bool is_blank(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t';
}

/**
 * @brief End of a program-name token (`argv[0]` rule: quotes delimit, no escapes).
 * @param raw Command line.
 * @param begin Token start.
 * @param value Receives the decoded program name.
 * @return Index after the token.
 */
inline std::size_t program_token_end(std::wstring_view raw, std::size_t begin, std::wstring& value) {
    value.clear();
    std::size_t pos = begin;
    if (pos < raw.size() && raw[pos] == L'"') {
        const std::size_t close = raw.find(L'"', pos + 1);
        const std::size_t stop = close == std::wstring_view::npos ? raw.size() : close;
        value.assign(raw.substr(pos + 1, stop - pos - 1));
        return close == std::wstring_view::npos ? raw.size() : close + 1;
    }
    while (pos < raw.size() && !is_blank(raw[pos])) {
        ++pos;
    }
    value.assign(raw.substr(begin, pos - begin));
    return pos;
}

/**
 * @brief Splits arguments with the `CommandLineToArgvW` rules (for everything after `argv[0]`).
 * @param raw Command line.
 * @param from Index where splitting starts.
 * @return Tokens with positions.
 */
inline std::vector<Token> tokenize(std::wstring_view raw, std::size_t from) {
    std::vector<Token> tokens;
    std::size_t pos = from;
    for (;;) {
        while (pos < raw.size() && is_blank(raw[pos])) {
            ++pos;
        }
        if (pos >= raw.size()) {
            return tokens;
        }
        Token token;
        token.begin = pos;
        bool quoted = false;
        while (pos < raw.size() && (quoted || !is_blank(raw[pos]))) {
            std::size_t slashes = 0;
            while (pos < raw.size() && raw[pos] == L'\\') {
                ++slashes;
                ++pos;
            }
            if (pos < raw.size() && raw[pos] == L'"') {
                token.value.append(slashes / 2, L'\\');
                if (slashes % 2 != 0) {
                    token.value.push_back(L'"');
                } else if (quoted && pos + 1 < raw.size() && raw[pos + 1] == L'"') {
                    token.value.push_back(L'"');  // "" inside quotes: literal quote, quoting ends
                    quoted = false;
                    ++pos;
                } else {
                    quoted = !quoted;
                }
                ++pos;
            } else {
                token.value.append(slashes, L'\\');
                if (pos < raw.size() && (quoted || !is_blank(raw[pos]))) {
                    token.value.push_back(raw[pos]);
                    ++pos;
                }
            }
        }
        token.end = pos;
        tokens.push_back(std::move(token));
    }
}

/// @brief Launcher options (only before the wrapped program).
struct Options {
    bool help = false;         ///< print usage
    bool verify_only = false;  ///< check files only
    bool no_dialogs = false;   ///< notices go to the log only
    std::wstring game;         ///< explicit hoi4.exe (shortcut mode / verify)
};

/// @brief The wrapped command as found in the raw command line.
struct WrappedCommand {
    std::wstring program;   ///< decoded program path
    std::size_t begin = 0;  ///< index of the program token in the raw line
    std::wstring tail;      ///< raw text after the program token, verbatim
};

/// @brief Result of parsing the launcher's command line.
struct Invocation {
    Options options;                        ///< launcher options
    std::optional<WrappedCommand> wrapped;  ///< present in wrapper mode
};

/// @brief Option: print usage.
inline constexpr std::wstring_view option_help = L"--help";
/// @brief Option: verify files only.
inline constexpr std::wstring_view option_verify = L"--verify-only";
/// @brief Option: explicit game executable.
inline constexpr std::wstring_view option_game = L"--game";
/// @brief Option: no dialogs.
inline constexpr std::wstring_view option_no_dialogs = L"--no-dialogs";
/// @brief Prefix of every launcher option.
inline constexpr std::wstring_view option_prefix = L"--";

/**
 * @brief Parses the launcher's raw command line.
 * @param raw Full command line (`GetCommandLineW()`), starting with the launcher's own path.
 * @return Options and, in wrapper mode, the wrapped command.
 * @throws LauncherError (usage) on unknown, repeated or contradictory options.
 */
inline Invocation parse_invocation(std::wstring_view raw) {
    Invocation result;
    std::wstring own;
    const std::size_t own_end = program_token_end(raw, 0, own);
    const auto tokens = tokenize(raw, own_end);
    bool game_seen = false;
    std::size_t index = 0;
    for (; index < tokens.size(); ++index) {
        const std::wstring& value = tokens[index].value;
        if (!value.starts_with(option_prefix)) {
            break;
        }
        bool* flag = nullptr;
        if (value == option_help) {
            flag = &result.options.help;
        } else if (value == option_verify) {
            flag = &result.options.verify_only;
        } else if (value == option_no_dialogs) {
            flag = &result.options.no_dialogs;
        } else if (value == option_game && !game_seen && index + 1 < tokens.size()) {
            game_seen = true;
            result.options.game = tokens[++index].value;
            continue;
        }
        if (flag == nullptr || *flag) {
            throw LauncherError(ExitCode::usage, L"Unknown or repeated launcher option: " + value);
        }
        *flag = true;
    }
    if (index < tokens.size()) {
        WrappedCommand wrapped;
        wrapped.begin = tokens[index].begin;
        const std::size_t end = program_token_end(raw, wrapped.begin, wrapped.program);
        wrapped.tail.assign(raw.substr(end));
        result.wrapped = std::move(wrapped);
    }
    const auto& options = result.options;
    if (options.help && (options.verify_only || options.no_dialogs || game_seen || result.wrapped)) {
        throw LauncherError(ExitCode::usage, L"--help takes no other arguments.");
    }
    if (options.verify_only && (!game_seen || result.wrapped)) {
        throw LauncherError(ExitCode::usage, L"--verify-only needs --game ABSOLUTE_PATH and nothing else.");
    }
    if (game_seen && result.wrapped) {
        throw LauncherError(ExitCode::usage, L"--game cannot be combined with a wrapped command.");
    }
    return result;
}

/**
 * @brief Case-insensitive "ends with".
 * @param text Text.
 * @param suffix Suffix.
 * @return `true` when `text` ends with `suffix`, ignoring ASCII and Unicode simple case.
 */
inline bool ends_with_nocase(std::wstring_view text, std::wstring_view suffix) noexcept {
    return text.size() >= suffix.size() &&
           CompareStringOrdinal(text.data() + (text.size() - suffix.size()), static_cast<int>(suffix.size()),
                                suffix.data(), static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

/// @brief Path suffix of the Paradox Launcher (separator plus `paradox_launcher_exe_name`).
inline constexpr std::wstring_view paradox_launcher_suffix = L"\\dowser.exe";
/// @brief Path suffix of the game (separator plus `game_exe_name`).
inline constexpr std::wstring_view game_exe_suffix = L"\\hoi4.exe";

/**
 * @brief Whether a path names one of the two accepted game programs by file name.
 * @param path Path.
 * @return `true` for `...\dowser.exe` and `...\hoi4.exe` (any case).
 */
inline bool names_game_program(std::wstring_view path) noexcept {
    return ends_with_nocase(path, paradox_launcher_suffix) || ends_with_nocase(path, game_exe_suffix);
}

/// @brief File-existence predicate (replaced in tests).
using ExistsFn = std::function<bool(const std::wstring&)>;

/**
 * @brief Resolves an unquoted program path that contains spaces.
 * @param raw Raw command line.
 * @param wrapped Parsed wrapped command; updated when a longer existing prefix is found.
 * @param exists File-existence predicate.
 * @note Only applies when the program token was not quoted and is not itself a game program.
 *       A candidate must contain no quote, end in `\dowser.exe` or `\hoi4.exe` and exist.
 */
inline void resolve_program(std::wstring_view raw, WrappedCommand& wrapped, const ExistsFn& exists) {
    if (wrapped.begin >= raw.size() || raw[wrapped.begin] == L'"' ||
        (names_game_program(wrapped.program) && exists(wrapped.program))) {
        return;
    }
    for (std::size_t end = wrapped.begin; end <= raw.size(); ++end) {
        if (end < raw.size() && raw[end] == L'"') {
            return;
        }
        if (end != raw.size() && !is_blank(raw[end])) {
            continue;
        }
        std::wstring candidate(raw.substr(wrapped.begin, end - wrapped.begin));
        if (names_game_program(candidate) && exists(candidate)) {
            wrapped.program = std::move(candidate);
            wrapped.tail.assign(raw.substr(end));
            return;
        }
    }
}

/**
 * @brief Command line for the wrapped program: the program in quotes plus the verbatim tail.
 * @param program Program path (must not contain a quote).
 * @param tail Raw text that followed the program token.
 * @return Command line.
 * @throws LauncherError (usage) for a quote in the program or a line over the Windows limit.
 */
inline std::wstring wrapped_command_line(const std::wstring& program, const std::wstring& tail) {
    if (program.find(L'"') != std::wstring::npos || program.find(L'\0') != std::wstring::npos) {
        throw LauncherError(ExitCode::usage, L"The program path contains a quote or NUL.");
    }
    std::wstring line = L"\"" + program + L"\"";
    if (!tail.empty() && !is_blank(tail.front())) {
        line.push_back(L' ');  // keep the program and the first argument apart
    }
    line += tail;
    if (line.size() >= command_line_limit) {
        throw LauncherError(ExitCode::usage, L"The wrapped command line is too long.");
    }
    return line;
}

/**
 * @brief Quotes one argument for the standard argument parser.
 * @param value Argument.
 * @return Quoted argument that `CommandLineToArgvW` decodes back to `value`.
 */
inline std::wstring quote_arg(std::wstring_view value) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(ch == L'"' ? (slashes * 2) + 1 : slashes, L'\\');
        result.push_back(ch);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

/**
 * @brief Builds a mutable command line from separate arguments (used for the injector).
 * @param args Arguments, program first.
 * @return NUL-terminated buffer for `CreateProcessW`.
 * @throws LauncherError (usage) on NUL or an over-long line.
 */
inline std::vector<wchar_t> command_line(const std::vector<std::wstring>& args) {
    std::wstring line;
    for (const auto& arg : args) {
        if (arg.find(L'\0') != std::wstring::npos) {
            throw LauncherError(ExitCode::usage, L"An argument contains NUL.");
        }
        if (!line.empty()) {
            line.push_back(L' ');
        }
        line += quote_arg(arg);
    }
    if (line.size() >= command_line_limit) {
        throw LauncherError(ExitCode::usage, L"Command line too long.");
    }
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');
    return buffer;
}

}  // namespace capie::launcher
