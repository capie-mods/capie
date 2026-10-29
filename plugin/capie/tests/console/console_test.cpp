/**
 * @file console_test.cpp
 * @brief Offline tests of the console-command candidate (`src/features/console/`).
 *
 * Covers the line parser, the text rules, the command plans (RN1 token, inline effect),
 * and the bridge against a fake console that copies the game's proven behaviour:
 * add-or-replace by name (`24B4920`), case-insensitive lookup by name or alias (`24B54C0`),
 * and the dispatcher order "multiplayer/ironman gate, then lookup, then action"
 * (`24B4D60`). The fake string assign and fake vanilla actions run in this process.
 * No game process, no game file, no hooks.
 *
 * Build with `/DCAPIE_CONSOLE_CANDIDATE=1` for the full suite; with 0 the suite checks that
 * the bridge stays disabled.
 */
// The suite runs the full bridge by default (also under mutation_check.ps1, which passes no
// switch); run_console_tests.ps1 builds it once more with 0.
#ifndef CAPIE_CONSOLE_CANDIDATE
#define CAPIE_CONSOLE_CANDIDATE 1
#endif

#include "../../src/features/console/console_bridge.hpp"
#include "../../src/features/console/console_commands.hpp"
#include "../../src/features/console/console_layout.hpp"
#include "../../src/features/console/console_line.hpp"
#include "../../src/features/console/console_text.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace console = capie::console;
namespace layout = capie::console::layout;
namespace line = capie::console::line;
namespace text = capie::console::text;
using capie::core::uptr;
using capie::engine::MsvcString;

unsigned checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
unsigned failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param value Condition.
 * @param label Printed on failure.
 */
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label.c_str());
    }
}

// ---------------------------------------------------------------- line and text

/**
 * @brief Splits a line and returns the arguments as strings, or "!<error>@<pos>".
 * @param input Line.
 * @return Arguments joined by '|' or the error.
 */
std::string split_text(std::string_view input) {
    const auto args = line::split(input);
    if (args.error != line::LineError::none) {
        return "!" + std::string(line::describe(args.error)) + "@" + std::to_string(args.position);
    }
    std::string out;
    for (std::size_t index = 0; index < args.count; ++index) {
        out += (index != 0 ? "|" : "") + std::string(args.at(index));
    }
    return out;
}

// Expected values of the test cases are literals on purpose.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
void line_tests() {
    check(split_text("GER Reich") == "GER|Reich", "bare words");
    check(split_text("GER \"Neues Reich\" neu") == "GER|Neues Reich|neu", "quoted with space");
    check(split_text("  GER   x  ") == "GER|x", "extra spaces between arguments");
    check(split_text(R"("")").empty(), "empty quoted argument is one argument");
    check(line::split(R"("")").count == 1 && line::split(R"("")").items[0].quoted, "empty quoted counted");
    check(split_text(R"("a \"b\" c")") == R"(a "b" c)", "escaped quotes");
    check(split_text(R"("a\\b")") == R"(a\b)", "escaped backslash");
    check(split_text(R"("a\nb")") == R"(a\nb)", "other backslash kept");
    check(split_text("\"abc") == "!unterminated quote@4", "unterminated quote");
    check(split_text("ab\"c") == "!misplaced quote@3", "quote in bare word");
    check(split_text("\"ab\"c") == "!misplaced quote@5", "text after closing quote");
    check(split_text("a b c d e f g") == "!too many arguments@13", "seven arguments");
    check(split_text("a b c d e f") == "a|b|c|d|e|f", "six arguments");
    const std::string long_word(line::max_argument_bytes + 1, 'x');
    check(split_text(long_word) == "!argument longer than 128 bytes@129", "argument too long");
    check(split_text(std::string(line::max_argument_bytes, 'x')).size() == line::max_argument_bytes, "128 bytes fit");

    const std::vector<std::string_view> tokens{"GER", "\"Neues", "Reich\"", "", "x"};
    line::Line joined{};
    check(line::join(tokens.data(), tokens.size(), joined) == line::LineError::none &&
              joined.view() == "GER \"Neues Reich\"  x",
          "join keeps empty tokens as double spaces");
    const std::string big(line::max_line_bytes, 'y');
    const std::vector<std::string_view> too_big{big, "z"};
    check(line::join(too_big.data(), too_big.size(), joined) == line::LineError::line_too_long && joined.size == 0,
          "join too long");
    const std::vector<std::string_view> exact{big};
    check(
        line::join(exact.data(), exact.size(), joined) == line::LineError::none && joined.size == line::max_line_bytes,
        "join exactly at the limit");
}

void text_tests() {
    check(text::valid_tag("GER") && text::valid_tag("D01") && !text::valid_tag("ger") && !text::valid_tag("GE") &&
              !text::valid_tag("GERM") && !text::valid_tag("G-R"),
          "tag syntax");
    check(text::valid_id("demo") && text::valid_id("a1") && text::valid_id("abcdefghijkl") &&
              !text::valid_id("abcdefghijklm") && !text::valid_id("") && !text::valid_id("Demo") &&
              !text::valid_id("de_mo"),
          "id syntax");

    const auto ok = text::check_country_text("Wolna Polska");
    check(ok.error == text::TextError::none && ok.codes.length == 12, "plain name");
    const auto polish = text::check_country_text("Rzeczpospolita Żółta");
    check(polish.error == text::TextError::none && polish.codes.length == 20, "Polish letters");
    check(text::check_country_text("").error == text::TextError::empty, "empty name");
    const auto edge = text::check_country_text(" Reich");
    check(edge.error == text::TextError::edge_space && edge.position == 1, "leading space");
    const auto tail = text::check_country_text("Reich ");
    check(tail.error == text::TextError::edge_space && tail.position == 6, "trailing space");
    const auto twice = text::check_country_text("Neues  Reich");
    check(twice.error == text::TextError::double_space && twice.position == 7, "double space");
    const auto pipe = text::check_country_text("a|b");
    check(pipe.error == text::TextError::markup_character && pipe.position == 2, "pipe is markup");
    const auto dollar = text::check_country_text("a$b");
    check(dollar.error == text::TextError::markup_character, "dollar is markup");
    const auto alien = text::check_country_text("Ζεύς");
    check(alien.error == text::TextError::outside_alphabet && alien.position == 1, "Greek outside the alphabet");
    check(text::check_country_text(std::string(32, 'a')).error == text::TextError::none, "32 characters");
    const auto long_name = text::check_country_text(std::string(33, 'a'));
    check(long_name.error == text::TextError::too_long && long_name.position == 33, "33 characters");
    check(text::check_country_text(std::string(40, 'a')).error == text::TextError::too_long, "40 characters");
    check(text::check_country_text("\xC3").error == text::TextError::invalid_utf8, "truncated UTF-8");
    check(text::check_country_text("\xC0\x80").error == text::TextError::invalid_utf8, "invalid lead byte");
    check(text::check_country_text("\xE0\x81\x81").error == text::TextError::invalid_utf8, "overlong 'A' rejected");
    check(text::check_country_text("\xF0\x80\x81\x81").error == text::TextError::invalid_utf8, "overlong 4-byte");
    check(text::check_country_text("\xED\xA0\x80").error == text::TextError::invalid_utf8, "surrogate in UTF-8");
    check(text::check_country_text("a\tb").error == text::TextError::control_character, "tab");
    check(text::check_text("", true).error == text::TextError::none, "empty allowed for mod arguments");
    const auto latin = text::check_text("Ŝŝ", false);
    check(latin.error == text::TextError::none && latin.codes.length == 2 && latin.codes.codes[0] > 152,
          "Latin Extended-A codes");
}

// ---------------------------------------------------------------- plans

/**
 * @brief Plans a command from a typed line (split on spaces like the game).
 * @param id Command.
 * @param typed Everything after the command name.
 * @return Plan.
 */
console::Plan plan_typed(console::CommandId id, std::string_view typed) {
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    while (!typed.empty()) {
        const auto next = typed.find(' ', start);
        tokens.push_back(typed.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start));
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }
    return console::plan_command(id, tokens.data(), tokens.size());
}

/**
 * @brief Expected start of a name-layer effect for ASCII letters and spaces (before the commit).
 * @param tag Tag.
 * @param name Name.
 * @param adjective Adjective.
 * @return Effect text up to and including `set_variable = { capie_cn_ver = 1 } `.
 */
std::string layer_effect(const std::string& tag, const std::string& name, const std::string& adjective) {
    const auto code = [](char c) {
        if (c >= 'a' && c <= 'z') {
            return 1 + (c - 'a');
        }
        if (c >= 'A' && c <= 'Z') {
            return 27 + (c - 'A');
        }
        return c == ' ' ? 63 : 0;
    };
    std::string out = tag + " = { clear_array = capie_cn_n clear_array = capie_cn_a ";
    for (const char c : name) {
        out += "add_to_array = { capie_cn_n = " + std::to_string(code(c)) + " } ";
    }
    for (const char c : adjective) {
        out += "add_to_array = { capie_cn_a = " + std::to_string(code(c)) + " } ";
    }
    return out + "set_variable = { capie_cn_ver = 1 } ";
}

void portrait_plan_tests() {
    using console::CommandId;
    using console::Delegate;
    auto plan = plan_typed(CommandId::change_leader_portrait, "POL GFX_a");
    check(plan.delegate == Delegate::eval_effect && plan.needs_cosmetic && plan.portrait_plan &&
              plan.tag.view() == "POL" && plan.sprite.view() == "GFX_a" &&
              console::small_candidate(plan).view() == "GFX_a_small",
          "change_leader_portrait plans an eval_effect that needs the sprite check");
    console::finish_portrait_plan(plan, true, true);
    check(plan.tokens[0].view() ==
              "POL = { random_character = { limit = { is_country_leader = yes } clear_array = capie_pt_s "
              "add_to_array = { capie_pt_s = 137266 } add_to_array = { capie_pt_s = 258112 } "
              "set_variable = { capie_pt_len = 5 } clear_array = capie_pt_ss add_to_array = { capie_pt_ss = 137266 } "
              "add_to_array = { capie_pt_ss = 258175 } add_to_array = { capie_pt_ss = 78657 } "
              "add_to_array = { capie_pt_ss = 49920 } set_variable = { capie_pt_slen = 11 } "
              "set_variable = { capie_pt_ver = 1 } } set_variable = { capie_pt_rev = 1 } }",
          "portrait effect: packed large and small, leader scope, commit on the country: " +
              std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::change_leader_portrait, "POL GFX_a");
    console::finish_portrait_plan(plan, true, false);
    check(plan.tokens[0].view().find("set_variable = { capie_pt_slen = 5 } ") != std::string_view::npos &&
              plan.tokens[0].view().find("capie_pt_ss = 258175") == std::string_view::npos,
          "no _small sprite: the large one is the small one");
    plan = plan_typed(CommandId::change_leader_portrait, "POL GFX_a");
    console::finish_portrait_plan(plan, false, true);
    check(plan.delegate == Delegate::none && plan.message.view().find("not found") != std::string_view::npos,
          "missing sprite refused");
    plan = plan_typed(CommandId::change_leader_portrait, "POL " + std::string(58, 'a'));
    check(console::small_candidate(plan).size == 0, "a 58-character sprite has no _small candidate (64 > 63)");
    console::finish_portrait_plan(plan, true, true);
    check(plan.delegate == Delegate::eval_effect &&
              plan.tokens[0].view().find("set_variable = { capie_pt_slen = 58 } ") != std::string_view::npos,
          "58 characters: small = large");
    plan = plan_typed(CommandId::change_leader_portrait, "POL " + std::string(57, 'b'));
    check(console::small_candidate(plan).size == 63, "57 characters: _small fits exactly");
    for (const char* bad : {"POL GFX.a", "POL gfx/a", "POL", "POL a b", "pol GFX_a", "POL GFX-a"}) {
        plan = plan_typed(CommandId::change_leader_portrait, bad);
        check(plan.delegate == Delegate::none, std::string("portrait refused: ") + bad);
    }
    plan = plan_typed(CommandId::change_leader_portrait, "POL " + std::string(64, 'a'));
    check(plan.delegate == Delegate::none, "64-character sprite refused");
    plan = plan_typed(CommandId::reset_leader_portrait, "POL");
    check(plan.delegate == Delegate::eval_effect && plan.portrait_plan &&
              plan.tokens[0].view() ==
                  "POL = { random_character = { limit = { is_country_leader = yes } clear_array = capie_pt_s "
                  "clear_array = capie_pt_ss clear_variable = capie_pt_len clear_variable = capie_pt_slen "
                  "clear_variable = capie_pt_ver } set_variable = { capie_pt_rev = 1 } }",
          "reset_leader_portrait clears the layer: " + std::string(plan.tokens[0].view()));
    console::finish_portrait_plan(plan, false, false);
    check(plan.delegate == Delegate::eval_effect, "a reset needs no sprite");
    plan = plan_typed(CommandId::reset_leader_portrait, "POL x");
    check(plan.delegate == Delegate::none, "reset_leader_portrait: two arguments refused");
}

void flag_plan_tests() {
    using console::CommandId;
    using console::Delegate;
    auto plan = plan_typed(CommandId::change_nation_flag, "POL POL_");
    check(plan.delegate == Delegate::eval_effect && plan.needs_cosmetic && plan.flag_plan && plan.tag.view() == "POL",
          "change_nation_flag plans an eval_effect that needs the cosmetic tag");
    console::finish_plan(plan, "");
    check(plan.tokens[0].view() ==
              "POL = { clear_array = capie_cf_s add_to_array = { capie_cf_s = 174694 } "
              "add_to_array = { capie_cf_s = 258048 } set_variable = { capie_cf_len = 4 } clear_array = capie_cf_cos "
              "set_variable = { capie_cf_cos_len = 0 } set_variable = { capie_cf_ver = 1 } "
              "set_variable = { capie_cf_rev = 1 } }",
          "flag effect: packed stem (CONTRACT 17.3 example), snapshot 0, commit last: " +
              std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::change_nation_flag, "ENG ENG_dominion");
    console::finish_plan(plan, "ENG_x");
    check(plan.tokens[0].view().find("add_to_array = { capie_cf_cos = ") != std::string_view::npos &&
              plan.tokens[0].view().find("set_variable = { capie_cf_cos_len = 5 } ") != std::string_view::npos &&
              plan.tokens[0].view().ends_with(
                  "set_variable = { capie_cf_ver = 1 } set_variable = { capie_cf_rev = 1 } }"),
          "flag effect: snapshot of the current cosmetic tag");
    plan = plan_typed(CommandId::change_nation_flag, "GER GER_x");
    console::finish_plan(plan, "RN1|Neues Reich|neu|");
    check(plan.tokens[0].view().find("clear_variable = capie_cf_cos_len ") != std::string_view::npos &&
              plan.tokens[0].view().find("capie_cf_cos_len = ") == std::string_view::npos,
          "a cosmetic tag outside the alphabet: no snapshot");
    for (const char* bad : {"POL POL.tga", "POL ../x", "POL a/b", "POL \"\"", "POL", "POL a b"}) {
        plan = plan_typed(CommandId::change_nation_flag, bad);
        check(plan.delegate == Delegate::none, std::string("refused: ") + bad);
    }
    plan = plan_typed(CommandId::change_nation_flag, "POL " + std::string(64, 'a'));
    check(plan.delegate == Delegate::none, "64 characters refused");
    plan = plan_typed(CommandId::change_nation_flag, "POL " + std::string(63, 'Z'));
    console::finish_plan(plan, "");
    check(plan.delegate == Delegate::eval_effect &&
              plan.tokens[0].view().find("capie_cf_len = 63 ") != std::string_view::npos,
          "63 characters accepted");
    plan = plan_typed(CommandId::reset_nation_flag, "POL");
    console::finish_plan(plan, "POL_x");
    check(plan.delegate == Delegate::eval_effect &&
              plan.tokens[0].view() ==
                  "POL = { clear_array = capie_cf_s clear_array = capie_cf_cos "
                  "clear_variable = capie_cf_len clear_variable = capie_cf_cos_len "
                  "clear_variable = capie_cf_ver set_variable = { capie_cf_rev = 1 } }",
          "reset_nation_flag clears the layer only: " + std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::reset_nation_flag, "pol");
    check(plan.delegate == Delegate::none, "reset_nation_flag: invalid tag");
}

void plan_tests() {
    using console::CommandId;
    using console::Delegate;
    auto plan = plan_typed(CommandId::change_nation_name, "GER \"Neues Reich\" neureichisch");
    check(plan.delegate == Delegate::eval_effect && plan.token_count == 1 && plan.needs_cosmetic &&
              plan.tag.view() == "GER" && plan.tokens[0].view() == layer_effect("GER", "Neues Reich", "neureichisch"),
          "change_nation_name with adjective: " + std::string(plan.tokens[0].view()));
    console::finish_layer_plan(plan, false);
    check(!plan.needs_cosmetic && plan.tokens[0].view() == layer_effect("GER", "Neues Reich", "neureichisch") +
                                                               "set_variable = { capie_cn_rev = 1 } }",
          "finished rename: commit last, no drop");
    plan = plan_typed(CommandId::change_nation_name, "POL Polska");
    console::finish_layer_plan(plan, true);
    const std::string polska =
        "POL = { clear_array = capie_cn_n clear_array = capie_cn_a "
        "add_to_array = { capie_cn_n = 42 } add_to_array = { capie_cn_n = 15 } add_to_array = { capie_cn_n = 12 } "
        "add_to_array = { capie_cn_n = 19 } add_to_array = { capie_cn_n = 11 } add_to_array = { capie_cn_n = 1 } "
        "add_to_array = { capie_cn_a = 42 } add_to_array = { capie_cn_a = 15 } add_to_array = { capie_cn_a = 12 } "
        "add_to_array = { capie_cn_a = 19 } add_to_array = { capie_cn_a = 11 } add_to_array = { capie_cn_a = 1 } "
        "set_variable = { capie_cn_ver = 1 } drop_cosmetic_tag = yes set_variable = { capie_cn_rev = 1 } }";
    check(plan.delegate == Delegate::eval_effect && plan.tokens[0].view() == polska,
          "adjective defaults to the name; legacy RN1 tag dropped: " + std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::change_nation_name,
                      "CZE \"\xC4\x8C"
                      "esk\xC3\xA1\"");
    check(plan.delegate == Delegate::eval_effect &&
              plan.tokens[0].view().find("add_to_array = { capie_cn_n = 161 } ") != std::string_view::npos,
          "extended alphabet code (\xC4\x8C = 161): " + std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::change_nation_name,
                      "GER ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef");
    console::finish_layer_plan(plan, true);
    check(plan.delegate == Delegate::eval_effect && plan.tokens[0].view().ends_with("capie_cn_rev = 1 } }"),
          "longest names fit one command");
    plan = plan_typed(CommandId::change_nation_name, "GER");
    check(plan.delegate == Delegate::none && plan.message.view() == console::spec(CommandId::change_nation_name).usage,
          "missing name prints usage");
    plan = plan_typed(CommandId::change_nation_name, "GER a b c");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("Usage:"), "four arguments print usage");
    plan = plan_typed(CommandId::change_nation_name, "ger Reich");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("CAPIE: invalid tag"), "lower-case tag");
    plan = plan_typed(CommandId::change_nation_name, "GER \"Neues  Reich\"");
    check(plan.message.view() == "CAPIE: name: two spaces in a row at character 7", "name error message");
    plan = plan_typed(CommandId::change_nation_name, "GER Reich \"a|b\"");
    check(plan.message.view() == "CAPIE: adjective: markup character at character 2", "adjective error message");
    plan = plan_typed(CommandId::change_nation_name, "GER \"Reich");
    check(plan.message.view() == "CAPIE: unterminated quote at byte 10", "line error message");
    plan = plan_typed(CommandId::change_nation_name, "GER \"\"");
    check(plan.message.view() == "CAPIE: name: empty", "empty quoted name");

    plan = plan_typed(CommandId::reset_nation_name, "GER");
    check(plan.delegate == Delegate::eval_effect && plan.token_count == 1 && plan.needs_cosmetic &&
              plan.tag.view() == "GER",
          "reset_nation_name");
    console::finish_layer_plan(plan, false);
    check(plan.tokens[0].view() ==
              "GER = { clear_array = capie_cn_n clear_array = capie_cn_a clear_variable = capie_cn_ver "
              "set_variable = { capie_cn_rev = 1 } }",
          "reset clears the layer only: " + std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::reset_nation_name, "GER");
    console::finish_layer_plan(plan, true);
    check(plan.tokens[0].view().ends_with("clear_variable = capie_cn_ver drop_cosmetic_tag = yes "
                                          "set_variable = { capie_cn_rev = 1 } }"),
          "reset of a legacy RN1 name also drops the tag");
    plan = plan_typed(CommandId::reset_nation_name, "");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("Usage: reset_nation_name"),
          "reset without tag");
    plan = plan_typed(CommandId::reset_nation_name, "GER x");
    check(plan.delegate == Delegate::none, "reset with two arguments");

    plan = plan_typed(CommandId::mod_command, R"(demo echo "ab" "")");
    const std::string expected =
        "FROM = { set_temp_variable = { capie_cap = 7 } set_temp_variable = { capie_ver = 10503 } "
        "set_temp_variable = { capie_src = 2 } set_temp_variable = { capie_nf = 2 } "
        "set_temp_variable = { capie_f1_err = 0 } set_temp_variable = { capie_f1_at = 0 } "
        "set_temp_variable = { capie_f1_len = 2 } set_temp_variable = { capie_f1_0 = 1 } "
        "set_temp_variable = { capie_f1_1 = 2 } "
        "set_temp_variable = { capie_f2_err = 0 } set_temp_variable = { capie_f2_at = 0 } "
        "set_temp_variable = { capie_f2_len = 0 } "
        "capie_v1_cmd__demo__echo = yes }";
    check(plan.delegate == Delegate::eval_effect && plan.token_count == 1 && plan.tokens[0].view() == expected,
          "inline effect: " + std::string(plan.tokens[0].view()));
    plan = plan_typed(CommandId::mod_command, "demo ping");
    check(plan.delegate == Delegate::eval_effect &&
              plan.tokens[0].view().ends_with("capie_nf = 0 } capie_v1_cmd__demo__ping = yes }"),
          "mod command without arguments");
    plan = plan_typed(CommandId::mod_command, "capie echo");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("CAPIE: invalid mod id"),
          "reserved mod id");
    plan = plan_typed(CommandId::mod_command, "Demo echo");
    check(plan.delegate == Delegate::none, "upper-case mod id");
    plan = plan_typed(CommandId::mod_command, "\"demo\" echo");
    check(plan.delegate == Delegate::none, "quoted mod id");
    plan = plan_typed(CommandId::mod_command, "demo e-cho");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("CAPIE: invalid command id"),
          "bad command id");
    plan = plan_typed(CommandId::mod_command, R"(demo "echo")");
    check(plan.delegate == Delegate::none && plan.message.view().starts_with("CAPIE: invalid command id"),
          "quoted command id");
    plan = plan_typed(CommandId::mod_command, "demo echo");
    check(!plan.needs_cosmetic, "mod commands need no cosmetic tag");
    check(console::is_capie_cosmetic("RN1|a|b|") && !console::is_capie_cosmetic("RN1") &&
              !console::is_capie_cosmetic("GER_fascist") && !console::is_capie_cosmetic(""),
          "CAPIE cosmetic prefix");
    plan = plan_typed(CommandId::mod_command, "demo echo a b c d e");
    check(plan.delegate == Delegate::none && plan.message.view() == "CAPIE: too many arguments at byte 19",
          "five arguments");
    plan = plan_typed(CommandId::mod_command, "demo");
    check(plan.delegate == Delegate::none, "mod id only");
    plan = plan_typed(CommandId::mod_command, "demo echo a \"x{y\"");
    check(plan.message.view() == "CAPIE: argument 2: markup character at character 2", "argument error message");
    const std::string full = "demo echo " + std::string(32, 'z') + " " + std::string(32, 'z') + " " +
                             std::string(32, 'z') + " " + std::string(32, 'z');
    plan = plan_typed(CommandId::mod_command, full);
    check(plan.delegate == Delegate::eval_effect && !plan.tokens[0].overflow &&
              plan.tokens[0].view().find("capie_f4_31 = 26 }") != std::string_view::npos,
          "four full arguments fit");
}

// ---------------------------------------------------------------- fake game

/// @brief Memory policy over this process, with one blockable range.
struct FakeMemory {
    uptr blocked_lo = 0;  ///< First blocked address.
    uptr blocked_hi = 0;  ///< One past the last blocked address.
    /// @brief Reads unless the range overlaps the blocked range. @return Success.
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        if (address < blocked_hi && address + size > blocked_lo) {
            return false;
        }
        return capie::core::ProcessMemory::read(address, out, size);
    }
};

/// @brief The fake console object (layout of console +0x00..+0x18).
struct FakeConsoleHeader {
    uptr data;              ///< Entry array.
    std::int32_t capacity;  ///< Capacity.
    std::int32_t count;     ///< Count.
    uptr allocator;         ///< Unused.
};

/// @brief Recorded delegate call.
struct DelegateCall {
    std::string name;                 ///< Delegate.
    std::vector<std::string> tokens;  ///< Its argv.
};

/// @brief All fake game state.
struct FakeGame {
    FakeConsoleHeader header{};                                      ///< Console header.
    std::vector<layout::CommandEntry> entries;                       ///< Entry array (data of `header`).
    std::deque<std::string> heap;                                    ///< Strings "allocated by the game".
    std::vector<DelegateCall> calls;                                 ///< Delegate calls.
    unsigned add_calls = 0;                                          ///< Calls of add_or_replace.
    bool drop_on_add = false;                                        ///< Fake add that silently does nothing.
    bool corrupt_on_add = false;                                     ///< Fake add that stores another action.
    bool wrong_context_on_add = false;                               ///< Fake add that stores another context.
    bool multiplayer = false;                                        ///< Gate B.
    bool ironman = false;                                            ///< Gate C.
    std::deque<std::string> alias_storage;                           ///< Alias bytes (deque: addresses stay stable).
    uptr console_slot = 0;                                           ///< The singleton pointer.
    std::array<std::vector<unsigned char>, 5> countries;             ///< Country objects by tag index (1..4).
    std::uint32_t thread = 100;                                      ///< Thread id the fake game reports.
    std::set<std::string> sprites{"GFX_a", "GFX_a_small", "GFX_b"};  ///< Sprites in the fake database.
    uptr sprite_database = 0x5EED;                                   ///< Fake database pointer value.
    std::vector<std::string> log;                                    ///< Bridge log lines.
};
/**
 * @brief The fake game (function-local static: constructed on first use, not at load).
 * @return The one instance.
 */
FakeGame& world() {
    static FakeGame instance;
    return instance;
}
FakeGame* game = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake-game singleton
console::ConsoleBridge<FakeMemory>* bridge =
    nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake DLL global

/**
 * @brief Reads a test-side MsvcString (process memory).
 * @param value String.
 * @return Bytes.
 */
std::string bytes_of(const MsvcString& value) {
    if (value.capacity <= capie::engine::sso_capacity) {
        return {reinterpret_cast<const char*>(value.data.small), static_cast<std::size_t>(value.size)};
    }
    return {value.data.pointer, static_cast<std::size_t>(value.size)};
}

/**
 * @brief Fake `129CA0`: assigns into a string with "game" storage.
 * @param self Target.
 * @param data Bytes.
 * @param size Length.
 * @return `self`.
 */
MsvcString* fake_assign(MsvcString* self, const char* data, std::size_t size) {
    game->heap.emplace_back(data, size);
    *self = capie::engine::borrowed_string(game->heap.back().c_str(), size);
    return self;
}

/**
 * @brief Re-points the header at the entry vector.
 */
void sync_header() {
    game->header.data = reinterpret_cast<uptr>(game->entries.data());
    game->header.count = static_cast<std::int32_t>(game->entries.size());
    game->header.capacity = game->header.count;
}

/**
 * @brief Case-insensitive lookup by name or alias, as `24B54C0`.
 * @param name Name.
 * @return Entry index or -1.
 */
int lookup(std::string_view name) {
    for (std::size_t index = 0; index < game->entries.size(); ++index) {
        const auto& entry = game->entries[index];
        if (console::detail::same_name(bytes_of(entry.name), name)) {
            return static_cast<int>(index);
        }
        for (std::int32_t slot = 0; slot < entry.alias_count; ++slot) {
            if (console::detail::same_name(reinterpret_cast<const char*>(entry.aliases[slot]), name)) {
                return static_cast<int>(index);
            }
        }
    }
    return -1;
}

/**
 * @brief Fake `24B4920`: replace by name or append (deep copy of the strings).
 * @param console_object Console (unused; the fake has one).
 * @param source Source vector.
 */
layout::ActionResult* fake_other(layout::ActionResult* out, const layout::TempVector* argv, std::uint64_t context);

void fake_add_or_replace(void* console_object, const layout::TempVector* source) {
    (void)console_object;
    ++game->add_calls;
    if (game->drop_on_add) {
        return;
    }
    const auto* items = reinterpret_cast<const layout::CommandEntry*>(source->data);
    for (std::int32_t index = 0; index < source->count; ++index) {
        layout::CommandEntry copy = items[index];
        if (game->corrupt_on_add) {
            copy.action = reinterpret_cast<uptr>(&fake_other);
        }
        if (game->wrong_context_on_add) {
            copy.context ^= 1U;
        }
        const auto copy_string = [](MsvcString& value) {
            game->heap.push_back(bytes_of(value));
            value = capie::engine::borrowed_string(game->heap.back().c_str(), game->heap.back().size());
        };
        copy_string(copy.name);
        copy_string(copy.description);
        for (auto& arg : copy.arg_names) {
            copy_string(arg);
        }
        const int existing = lookup(bytes_of(copy.name));
        if (existing >= 0) {
            game->entries[static_cast<std::size_t>(existing)] = copy;
        } else {
            game->entries.push_back(copy);
        }
    }
    sync_header();
}

/**
 * @brief Records a vanilla delegate call and answers like the game.
 * @param name Delegate name.
 * @param out Result.
 * @param argv Tokens.
 * @return `out`.
 */
layout::ActionResult* record_delegate(const char* name, layout::ActionResult* out, const layout::TempVector* argv) {
    out->flag = 0;
    layout::construct_empty(out->message);
    DelegateCall call{name, {}};
    const auto* strings = reinterpret_cast<const MsvcString*>(argv->data);
    for (std::int32_t index = 0; index < argv->count; ++index) {
        call.tokens.push_back(bytes_of(strings[index]));
    }
    if (!call.tokens.empty() && call.tokens[0] == "XXX") {
        fake_assign(&out->message, "Invalid tag for the command", 27);
    }
    game->calls.push_back(call);
    return out;
}

layout::ActionResult* fake_set_cosmetic(layout::ActionResult* out, const layout::TempVector* argv,
                                        std::uint64_t context) {
    (void)context;
    return record_delegate("set_cosmetic_tag", out, argv);
}
layout::ActionResult* fake_drop_cosmetic(layout::ActionResult* out, const layout::TempVector* argv,
                                         std::uint64_t context) {
    (void)context;
    return record_delegate("drop_cosmetic_tag", out, argv);
}
layout::ActionResult* fake_eval_effect(layout::ActionResult* out, const layout::TempVector* argv,
                                       std::uint64_t context) {
    (void)context;
    return record_delegate("eval_effect", out, argv);
}
layout::ActionResult* fake_other(layout::ActionResult* out, const layout::TempVector* argv, std::uint64_t context) {
    (void)context;
    return record_delegate("other", out, argv);
}

/**
 * @brief The action CAPIE registers (what the DLL would export as `console_action`).
 * @param out Result storage.
 * @param argv Tokens.
 * @param context Entry context.
 * @return `out`.
 */
layout::ActionResult* capie_action(layout::ActionResult* out, const layout::TempVector* argv, std::uint64_t context) {
    return bridge->run(out, argv, context);
}

/**
 * @brief Adds a vanilla entry to the fake table.
 * @param name Name.
 * @param action Action.
 * @param alias Optional alias.
 */
void add_vanilla(const char* name, layout::ActionFn action, const char* alias = nullptr) {
    layout::CommandEntry entry{};
    entry.release_flag = 1;
    game->heap.emplace_back(name);
    entry.name = capie::engine::borrowed_string(game->heap.back().c_str(), game->heap.back().size());
    if (alias != nullptr) {
        game->alias_storage.emplace_back(alias);
        entry.alias_count = 1;
        entry.aliases[0] = reinterpret_cast<uptr>(game->alias_storage.back().c_str());
    }
    entry.action = reinterpret_cast<uptr>(action);
    layout::construct_empty(entry.description);
    for (auto& arg : entry.arg_names) {
        layout::construct_empty(arg);
    }
    game->entries.push_back(entry);
    sync_header();
}

/**
 * @brief Fake dispatcher in the proven order of `24B4D60`: gate, lookup, action.
 * @param typed Typed line.
 * @return Console message.
 */
std::string dispatch(std::string_view typed) {
    if (game->multiplayer || game->ironman) {
        return "Console not available in multiplayer or ironman mode.";
    }
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (true) {
        const auto next = typed.find(' ', start);
        tokens.emplace_back(
            typed.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start));
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }
    const int found = tokens[0].empty() ? -1 : lookup(tokens[0]);
    if (found < 0) {
        return "Unknown command";
    }
    std::vector<MsvcString> argv;
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        argv.push_back(capie::engine::borrowed_string(tokens[index].c_str(), tokens[index].size()));
    }
    const layout::TempVector vector{reinterpret_cast<uptr>(argv.data()), static_cast<std::int32_t>(argv.size()),
                                    static_cast<std::int32_t>(argv.size()), 0};
    const auto& entry = game->entries[static_cast<std::size_t>(found)];
    layout::ActionResult result{};  // the game passes uninitialised storage; zero is a valid stand-in
    const auto action = reinterpret_cast<layout::ActionFn>(entry.action);
    action(&result, &vector, entry.context);
    return bytes_of(result.message);
}

/**
 * @brief Builds a fresh fake game with the three delegates and a few vanilla commands.
 * @param state Storage.
 */
void set_cosmetic(std::size_t index, const std::string& cosmetic);

void reset_game(FakeGame& state) {
    state = FakeGame{};
    state.entries.reserve(64);  // keep entry addresses stable while a test holds none
    game = &state;
    add_vanilla("test", fake_other);
    add_vanilla("set_cosmetic_tag", fake_set_cosmetic);
    add_vanilla("drop_cosmetic_tag", fake_drop_cosmetic);
    add_vanilla("eval_effect", fake_eval_effect);
    add_vanilla("whitepeace", fake_other, "wp");
    state.console_slot = reinterpret_cast<uptr>(&state.header);
    set_cosmetic(0, "");
    set_cosmetic(1, "RN1|Neues Reich|neu|");
    set_cosmetic(2, "FRA_alt");
    set_cosmetic(3, "");
    set_cosmetic(4, "ITA_x");
}

/**
 * @brief Fake `10E10C0`: writes the tag index (GER 1, FRA 2, POL 3, ITA 4, else 0).
 * @param out Tag object.
 * @param name Tag name.
 * @return `out`.
 */
void* fake_tag_parse(void* out, const MsvcString* name) {
    const std::string tag = bytes_of(*name);
    std::int32_t index = 0;
    const std::array<const char*, 4> known{"GER", "FRA", "POL", "ITA"};
    for (std::size_t i = 0; i < known.size(); ++i) {
        index = tag == known[i] ? static_cast<std::int32_t>(i + 1) : index;
    }
    std::memcpy(out, &index, sizeof(index));
    return out;
}

/**
 * @brief Fake `BB4390`: the country object of a tag index (index 0 has an object, as in the game array).
 * @param tag Tag object.
 * @return Country, or null.
 */
void* fake_country_of(const void* tag) {
    std::int32_t index = 0;
    std::memcpy(&index, tag, sizeof(index));
    if (index < 0 ||
        static_cast<std::size_t>(index) >= game->countries.size()) {  // index 0: like the game, a slot exists
        return nullptr;
    }
    return game->countries[static_cast<std::size_t>(index)].data();
}

/**
 * @brief Sets a fake country's cosmetic tag.
 * @param index Tag index.
 * @param cosmetic Tag text.
 */
void set_cosmetic(std::size_t index, const std::string& cosmetic) {
    auto& country = game->countries[index];
    country.assign(layout::country_cosmetic_tag + sizeof(MsvcString), 0);
    game->heap.push_back(cosmetic);
    const auto value = capie::engine::borrowed_string(game->heap.back().c_str(), game->heap.back().size());
    std::memcpy(country.data() + layout::country_cosmetic_tag, &value, sizeof(value));
}

/**
 * @brief Fake `2238DF0`: a sprite of the fake database.
 * @param database Database.
 * @param name Name.
 * @return Non-null when the name is in `sprites`.
 */
void* fake_sprite_find(void* database, const MsvcString* name) {
    if (reinterpret_cast<uptr>(database) != game->sprite_database) {
        return nullptr;
    }
    return game->sprites.contains(bytes_of(*name)) ? database : nullptr;
}

/** @brief Fake thread id. @return The fake game's current thread. */
std::uint32_t fake_thread() {
    return game->thread;
}

/** @brief Fake log sink. @param line Line. */
void fake_log(const char* line) {
    game->log.emplace_back(line);
}

/**
 * @brief API for the fake game.
 * @param state Fake game.
 * @return Verified API.
 */
console::NativeApi fake_api(FakeGame& state) {
    console::NativeApi api{};
    api.console_slot = reinterpret_cast<uptr>(&state.console_slot);
    api.add_or_replace = fake_add_or_replace;
    api.assign = fake_assign;
    api.set_cosmetic_tag = reinterpret_cast<uptr>(&fake_set_cosmetic);
    api.drop_cosmetic_tag = reinterpret_cast<uptr>(&fake_drop_cosmetic);
    api.eval_effect = reinterpret_cast<uptr>(&fake_eval_effect);
    api.tag_parse = fake_tag_parse;
    api.country_of = fake_country_of;
    api.sprite_find = fake_sprite_find;
    api.sprite_database_slot = reinterpret_cast<uptr>(&state.sprite_database);
    api.current_thread = fake_thread;
    api.log = fake_log;
    api.verified = true;
    return api;
}

void bridge_disabled_tests() {
    FakeGame& state = world();
    reset_game(state);
    const FakeMemory mem{};
    console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), reinterpret_cast<uptr>(&capie_action));
    check(local.try_register() == console::Registration::disabled && state.add_calls == 0,
          "candidate off: disabled, nothing added");
}

void bridge_tests() {
    using console::Registration;
    const FakeMemory mem{};
    const auto action = reinterpret_cast<uptr>(&capie_action);
    FakeGame& state = world();

    reset_game(state);
    auto api = fake_api(state);
    api.verified = false;
    {
        console::ConsoleBridge<FakeMemory> local(mem, api, action);
        check(local.try_register() == Registration::disabled && state.add_calls == 0, "unverified API: disabled");
    }

    reset_game(state);
    const uptr saved = state.console_slot;
    state.console_slot = 0;
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::waiting && state.add_calls == 0, "no console yet: waiting");
        state.console_slot = saved;
        check(local.try_register() == Registration::registered && state.add_calls == 1, "registers once it exists");
    }

    reset_game(state);
    add_vanilla("Change_Nation_Name", fake_other);
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::name_collision && state.add_calls == 0,
              "existing name (any case): refused");
    }

    reset_game(state);
    add_vanilla("something", fake_other, "CAPIE_CMD");
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::name_collision && state.add_calls == 0, "existing alias: refused");
    }

    reset_game(state);
    api = fake_api(state);
    api.set_cosmetic_tag = reinterpret_cast<uptr>(&fake_other);
    {
        console::ConsoleBridge<FakeMemory> local(mem, api, action);
        check(local.try_register() == Registration::missing_delegate && state.add_calls == 0,
              "delegate at another address: refused");
    }

    reset_game(state);
    state.entries.erase(state.entries.begin() + 3);  // eval_effect
    sync_header();
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::missing_delegate && state.add_calls == 0,
              "missing delegate: refused");
    }

    reset_game(state);
    {
        FakeMemory blocked{};
        blocked.blocked_lo = state.header.data;
        blocked.blocked_hi = state.header.data + layout::entry_size;
        console::ConsoleBridge<FakeMemory> local(blocked, fake_api(state), action);
        check(local.try_register() == Registration::unreadable && state.add_calls == 0, "unreadable array: refused");
    }

    reset_game(state);
    state.drop_on_add = true;
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::readback_failed && state.add_calls == 1,
              "read-back catches a failed add");
        check(local.try_register() == Registration::readback_failed && state.add_calls == 1, "no second add");
    }

    reset_game(state);
    state.corrupt_on_add = true;
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::readback_failed, "read-back checks the stored action");
    }

    reset_game(state);
    state.wrong_context_on_add = true;
    {
        console::ConsoleBridge<FakeMemory> local(mem, fake_api(state), action);
        check(local.try_register() == Registration::readback_failed, "read-back checks the stored context");
    }

    // End to end.
    reset_game(state);
    console::ConsoleBridge<FakeMemory> live(mem, fake_api(state), action);
    bridge = &live;
    check(live.try_register() == Registration::registered && state.add_calls == 1, "registered");
    check(live.try_register() == Registration::registered && state.add_calls == 1, "registration is one-shot");
    const int entry = lookup("change_nation_name");
    check(entry >= 0 && state.entries[static_cast<std::size_t>(entry)].alias_count == 0 &&
              state.entries[static_cast<std::size_t>(entry)].release_flag == 1 &&
              state.entries[static_cast<std::size_t>(entry)].arg_name_count == 3 &&
              bytes_of(state.entries[static_cast<std::size_t>(entry)].arg_names[1]) == "\"<name>\"",
          "entry fields");
    check(lookup("set_cosmetic_tag") == 1 && lookup("test") == 0, "vanilla entries untouched");

    check(dispatch("change_nation_name GER \"Neues Reich\" neureichisch").empty() && state.calls.size() == 1 &&
              state.calls[0].name == "eval_effect" && state.calls[0].tokens.size() == 1 &&
              state.calls[0].tokens[0] == layer_effect("GER", "Neues Reich", "neureichisch") +
                                              "drop_cosmetic_tag = yes set_variable = { capie_cn_rev = 1 } }",
          "rename writes the layer through eval_effect; GER's legacy RN1 tag is dropped");
    check(dispatch("CHANGE_NATION_NAME POL Polska").empty() && state.calls.size() == 2 &&
              state.calls[1].tokens[0].starts_with("POL = { ") &&
              state.calls[1].tokens[0].ends_with("capie_cn_ver = 1 } set_variable = { capie_cn_rev = 1 } }"),
          "command name is case-insensitive; no cosmetic tag, nothing dropped");
    check(dispatch("change_nation_name FRA Francja").empty() &&
              state.calls.back().tokens[0].find("drop_cosmetic_tag") == std::string::npos,
          "rename keeps a story cosmetic tag (flag and colour stay)");
    check(dispatch("change_nation_name XXX Reich") == "Invalid tag for the command", "unknown tag refused");
    auto calls_now = state.calls.size();
    check(dispatch("change_nation_name GER \"Neues  Reich\"") == "CAPIE: name: two spaces in a row at character 7" &&
              state.calls.size() == calls_now,
          "CAPIE error message, no delegate call");
    check(dispatch("change_nation_name") == console::spec(console::CommandId::change_nation_name).usage,
          "usage without arguments");
    check(dispatch("reset_nation_name GER").empty() && state.calls.back().name == "eval_effect" &&
              state.calls.back().tokens[0] ==
                  "GER = { clear_array = capie_cn_n clear_array = capie_cn_a clear_variable = capie_cn_ver "
                  "drop_cosmetic_tag = yes set_variable = { capie_cn_rev = 1 } }",
          "reset of a legacy RN1 name clears the layer and drops the tag");
    check(dispatch("reset_nation_name FRA").empty() &&
              state.calls.back().tokens[0] ==
                  "FRA = { clear_array = capie_cn_n clear_array = capie_cn_a clear_variable = capie_cn_ver "
                  "set_variable = { capie_cn_rev = 1 } }",
          "reset keeps a story cosmetic tag");
    check(dispatch("reset_nation_name POL").empty() && state.calls.back().tokens[0].starts_with("POL = { "),
          "reset without a cosmetic tag clears the layer");
    calls_now = state.calls.size();
    check(dispatch("reset_nation_name SWE") == "Invalid tag for the command" && state.calls.size() == calls_now,
          "reset of an unknown tag");
    check(dispatch("change_nation_flag FRA FRA_alt").empty() && state.calls.back().name == "eval_effect" &&
              state.calls.back().tokens[0].starts_with("FRA = { clear_array = capie_cf_s ") &&
              state.calls.back().tokens[0].find("set_variable = { capie_cf_cos_len = 7 } ") != std::string::npos,
          "change_nation_flag goes through eval_effect with FRA's cosmetic tag as the snapshot");
    check(dispatch("change_nation_flag SWE SWE_x") == "Invalid tag for the command", "flag: unknown tag refused");
    check(dispatch("reset_nation_flag POL").empty() && state.calls.back().tokens[0].starts_with("POL = { clear_array"),
          "reset_nation_flag goes through eval_effect");
    check(dispatch("change_leader_portrait FRA GFX_a").empty() && state.calls.back().name == "eval_effect" &&
              state.calls.back().tokens[0].starts_with("FRA = { random_character = ") &&
              state.calls.back().tokens[0].find("set_variable = { capie_pt_slen = 11 } ") != std::string::npos,
          "change_leader_portrait goes through eval_effect with the _small sprite");
    check(dispatch("change_leader_portrait FRA GFX_b").empty() &&
              state.calls.back().tokens[0].find("set_variable = { capie_pt_slen = 5 } ") != std::string::npos,
          "no GFX_b_small in the database: small = large");
    calls_now = state.calls.size();
    check(dispatch("change_leader_portrait FRA GFX_missing").find("not found") != std::string::npos &&
              state.calls.size() == calls_now,
          "a sprite outside the database is refused before any call");
    check(dispatch("change_leader_portrait SWE GFX_a") == "Invalid tag for the command" &&
              state.calls.size() == calls_now,
          "portrait: unknown tag refused");
    const auto database = state.sprite_database;
    state.sprite_database = 0;
    check(dispatch("change_leader_portrait FRA GFX_a").find("not found") != std::string::npos &&
              state.calls.size() == calls_now,
          "no sprite database: refused");
    state.sprite_database = database;
    check(dispatch("reset_leader_portrait POL").empty() &&
              state.calls.back().tokens[0].starts_with("POL = { random_character = ") &&
              state.calls.back().tokens[0].ends_with("set_variable = { capie_pt_rev = 1 } }"),
          "reset_leader_portrait goes through eval_effect");
    check(dispatch("capie_cmd demo echo hi").empty() && state.calls.back().name == "eval_effect" &&
              state.calls.back().tokens.size() == 1 && state.calls.back().tokens[0].starts_with("FROM = { ") &&
              state.calls.back().tokens[0].ends_with("capie_v1_cmd__demo__echo = yes }"),
          "mod command goes through eval_effect");
    check(dispatch("no_such_command x") == "Unknown command", "unknown command stays vanilla");

    const auto before = state.calls.size();
    state.multiplayer = true;
    check(dispatch("change_nation_name GER Reich") == "Console not available in multiplayer or ironman mode." &&
              state.calls.size() == before,
          "multiplayer gate blocks before CAPIE runs");
    state.multiplayer = false;
    state.ironman = true;
    check(dispatch("capie_cmd demo echo") == "Console not available in multiplayer or ironman mode." &&
              state.calls.size() == before,
          "ironman gate blocks before CAPIE runs");
    state.ironman = false;

    // The game re-runs add_or_replace with its static table (RestoreDeviceObjects): CAPIE entries stay.
    add_vanilla("help", fake_other);
    check(lookup("change_nation_name") >= 0 && lookup("capie_cmd") >= 0, "entries survive later vanilla additions");

    // A delegate that changed after registration is not called.
    state.entries[static_cast<std::size_t>(lookup("eval_effect"))].action = reinterpret_cast<uptr>(&fake_other);
    check(dispatch("change_nation_name GER Reich") == "CAPIE: console delegate unavailable" &&
              state.calls.size() == before,
          "changed delegate is refused at run time");
    state.entries[static_cast<std::size_t>(lookup("eval_effect"))].action = reinterpret_cast<uptr>(&fake_eval_effect);

    // Wrong context (not a CAPIE entry) is refused.
    layout::ActionResult result{};
    const layout::TempVector empty{};
    live.run(&result, &empty, static_cast<std::uint64_t>(console::CommandId::reset_nation_name));  // id without magic
    check(bytes_of(result.message) == "CAPIE: console commands inactive", "foreign context refused");
    live.run(&result, &empty, console::context_magic | console::command_count);
    check(bytes_of(result.message) == "CAPIE: console commands inactive", "out-of-range id refused");

    // Unreadable argv.
    live.run(&result, nullptr, console::ConsoleBridge<FakeMemory>::context_of(console::CommandId::reset_nation_name));
    check(bytes_of(result.message) == "CAPIE: could not read the command line", "null argv refused");

    // Thread check: registration happened on thread 100; an action on another thread is refused and logged.
    check(!state.log.empty() && state.log[0].find("first action on thread 100") != std::string::npos,
          "first action logs the thread");
    calls_now = state.calls.size();
    state.thread = 200;
    check(dispatch("change_nation_name GER Reich").starts_with("CAPIE: console commands refused (wrong thread") &&
              state.calls.size() == calls_now &&
              state.log.back().find("thread 200 differs from Tick thread 100") != std::string::npos,
          "other thread refused and logged");
    state.thread = 100;
    check(dispatch("change_nation_name GER Reich").empty() && state.calls.size() == calls_now + 1,
          "same thread works again");
    bridge = nullptr;

    // run() before registration (valid context) is refused.
    reset_game(state);
    {
        console::ConsoleBridge<FakeMemory> idle(mem, fake_api(state), action);
        layout::ActionResult idle_result{};
        idle.run(&idle_result, &empty,
                 console::ConsoleBridge<FakeMemory>::context_of(console::CommandId::change_nation_name));
        check(bytes_of(idle_result.message) == "CAPIE: console commands inactive" && state.calls.empty(),
              "unregistered bridge refuses a valid context");
    }

    // configure() is ignored after registration started.
    reset_game(state);
    {
        console::ConsoleBridge<FakeMemory> configured(mem, console::NativeApi{}, 0);
        configured.configure(fake_api(state), action);
        check(configured.try_register() == Registration::registered, "configure before registration");
        configured.configure(console::NativeApi{}, 0);
        layout::ActionResult configured_result{};
        configured.run(&configured_result, &empty,
                       console::ConsoleBridge<FakeMemory>::context_of(console::CommandId::change_nation_name));
        check(bytes_of(configured_result.message).starts_with("Usage: change_nation_name"),
              "configure after registration is ignored");
    }
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        line_tests();
        text_tests();
        plan_tests();
        flag_plan_tests();
        portrait_plan_tests();
        if constexpr (console::candidate_enabled) {
            bridge_tests();
        } else {
            bridge_disabled_tests();
        }
    } catch (...) {
        ++failures;
        std::printf("FAIL unexpected exception\n");
    }
    std::printf("console tests (candidate=%d): %u checks, %u failures\n", console::candidate_enabled ? 1 : 0, checks,
                failures);
    return failures == 0 ? 0 : 1;
}
