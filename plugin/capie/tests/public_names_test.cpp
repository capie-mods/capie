/**
 * @file public_names_test.cpp
 * @brief Offline tests of the public editor grammar (C1) and the extended alphabet (C3).
 *
 * Pure functions only: `public_names.hpp` and `input_codes.hpp`. No game process, no
 * hooks, no fake image.
 */
#include "../src/features/editor/input_codes.hpp"
#include "../src/features/editor/public_names.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace grammar = capie::editor::grammar;
using capie::editor::Alphabet;
using capie::editor::encode_field;
using capie::editor::InputError;

unsigned checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
unsigned failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param value Condition.
 * @param label Printed on failure.
 */
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

/**
 * @brief Expects a valid model name and its ids.
 * @param name Model name.
 * @param mod Expected mod id.
 * @param editor Expected editor id.
 */
void expect_model(std::string_view name, std::string_view mod, std::string_view editor) {
    const auto id = grammar::parse_model(name);
    check(id.has_value() && id->mod_view() == mod && id->editor_view() == editor, std::string(name).c_str());
    if (id) {
        check(grammar::model_name(*id).view() == name, "model name round trip");
    }
}

/**
 * @brief Expects a rejected model name.
 * @param name Candidate.
 */
void expect_rejected(std::string_view name) {
    check(!grammar::parse_model(name).has_value(), std::string(name).c_str());
}

/**
 * @brief Expects the codes of an extended-alphabet field.
 * @param text Field text.
 * @param codes Expected codes.
 * @param label Label.
 */
void expect_extended(std::u32string_view text, const std::vector<unsigned>& codes, const char* label) {
    const auto result = encode_field(text, Alphabet::extended);
    check(result.value.has_value() && result.value->length == codes.size(), label);
    if (!result.value) {
        return;
    }
    for (std::size_t i = 0; i < codes.size(); ++i) {
        check(result.value->codes[i] == codes[i], label);
    }
}

/// @brief Grammar: accepted names, rejected lookalikes, built names and limits.
void grammar_tests() {
    expect_model("capie_v1_ed__mymod__faction", "mymod", "faction");
    expect_model("capie_v1_ed__a__b", "a", "b");
    expect_model("capie_v1_ed__abcdefghijkl__0123456789ab", "abcdefghijkl", "0123456789ab");
    expect_model("capie_v1_ed__capie__hello", "capie", "hello");

    expect_rejected("");
    expect_rejected("capie_v1_ed__");
    expect_rejected("capie_v1_ed____x");                           // empty mod id
    expect_rejected("capie_v1_ed__x__");                           // empty editor id
    expect_rejected("capie_v1_ed__MyMod__x");                      // upper case
    expect_rejected("capie_v1_ed__my_mod__x");                     // underscore in an id
    expect_rejected("capie_v1_ed__a__b__c");                       // a third part
    expect_rejected("capie_v1_ed__a__b ");                         // trailing space
    expect_rejected("capie_v1_ed__abcdefghijklm__b");              // 13-character mod id
    expect_rejected("capie_v1_ed__a__abcdefghijklm");              // 13-character editor id
    expect_rejected("capie_v2_ed__a__b");                          // other major version
    expect_rejected("Capie_v1_ed__a__b");                          // case of the prefix
    expect_rejected("xcapie_v1_ed__a__b");                         // prefix not at the start
    expect_rejected("capie_v1_ed__a__b_apply_click");              // a selector is not a model
    expect_rejected("capie_v1_f__a__b__1");                        // a field is not a model
    expect_rejected("rn_ip_leader_gui");                           // Rename Anything's legacy name
    expect_rejected(std::string_view("capie_v1_ed__a\0__b", 18));  // embedded NUL

    const auto id = grammar::parse_model("capie_v1_ed__mymod__faction");
    check(id.has_value(), "parse for built names");
    if (id) {
        check(grammar::apply_name(*id).view() == "capie_v1_ed__mymod__faction_apply_click", "apply name");
        check(grammar::epoch_name(*id).view() == "capie_v1_ep__mymod__faction", "epoch name");
        check(grammar::field_name(*id, 1).view() == "capie_v1_f__mymod__faction__1", "field 1 name");
        check(grammar::field_name(*id, 4).view() == "capie_v1_f__mymod__faction__4", "field 4 name");
        check(grammar::prefill_name(*id, 2).view() == "capie_v1_pf__mymod__faction__2", "prefill 2 name");
        check(grammar::apply_name(*id).c_str()[grammar::apply_name(*id).size] == '\0', "built names are terminated");
    }
    const auto longest = grammar::parse_model("capie_v1_ed__abcdefghijkl__0123456789ab");
    check(longest.has_value() && grammar::model_name(*longest).size == grammar::max_model_name &&
              grammar::max_model_name == 39,
          "longest model name is 39 bytes");
    check(longest.has_value() && grammar::apply_name(*longest).size == grammar::max_apply_name &&
              grammar::max_apply_name == 51,
          "longest Apply selector is 51 bytes");
    check(grammar::may_be_model("capie_v1_ed__a__b") && !grammar::may_be_model("capie_v1_ed__a") &&
              !grammar::may_be_model("rn_ip_country_gui") &&
              !grammar::may_be_model("capie_v1_ed__abcdefghijkl__0123456789abc"),
          "pre-filter: prefix and length only");
}

/// @brief Alphabet: legacy table unchanged, extended table, rejections.
void alphabet_tests() {
    // Codes 1..84 are identical in both tables.
    for (std::size_t i = 0; i < capie::editor::alphabet_size; ++i) {
        const std::u32string one(1, capie::editor::alphabet[i]);
        const auto legacy = encode_field(one);
        const auto extended = encode_field(one, Alphabet::extended);
        check(legacy.value && extended.value && legacy.value->codes[0] == i + 1 && extended.value->codes[0] == i + 1,
              "codes 1..84 equal in both tables");
    }
    expect_extended(U",!?()&:/", {85, 86, 87, 88, 89, 90, 91, 92}, "punctuation 85..92");
    expect_extended(U"ÀÁ", {93, 94}, "Latin-1 starts at 93");
    expect_extended(U"ÖØ", {114, 115}, "multiplication sign skipped");
    expect_extended(U"ÿ", {152}, "Latin-1 ends at 152");
    expect_extended(U"Ā", {153}, "Extended-A starts at 153");
    expect_extended(U"ſ", {264}, "Extended-A ends at 264");
    expect_extended(U"ó", {72}, "Polish o-acute keeps its legacy code");
    expect_extended(U"Ł", {79}, "Polish L-stroke keeps its legacy code");
    expect_extended(U"Ville-Marie, Québec", {48, 9, 12, 12, 5, 64, 39, 1, 18, 9, 5, 85, 63, 43, 21, 132, 2, 5, 3},
                    "mixed name");

    // The legacy table still rejects every addition (Rename Anything's decoder knows 84 codes).
    for (const char32_t point : {U'é', U',', U'ß', U'Ž', U'ſ'}) {
        const auto legacy = encode_field(std::u32string(1, point));
        check(!legacy.value && legacy.error == InputError::unsupported_character, "legacy table rejects additions");
    }
    // Rejections in the extended table.
    const auto multiply = encode_field(U"a×b", Alphabet::extended);
    check(!multiply.value && multiply.error == InputError::unsupported_character && multiply.error_index == 1,
          "multiplication sign rejected at index 1");
    const auto cyrillic = encode_field(U"Москва", Alphabet::extended);
    check(!cyrillic.value && cyrillic.error == InputError::unsupported_character && cyrillic.error_index == 0,
          "Cyrillic rejected (OPEN)");
    const auto pipe = encode_field(U"a|b", Alphabet::extended);
    check(!pipe.value && pipe.error == InputError::render_delimiter, "pipe is a delimiter");
    const auto section = encode_field(U"§Y", Alphabet::extended);
    check(!section.value && section.error == InputError::render_delimiter, "section sign is a delimiter");
    const auto tab = encode_field(U"a\tb", Alphabet::extended);
    check(!tab.value && tab.error == InputError::control_character, "control character");
    const auto empty = encode_field(U"", Alphabet::extended);
    check(!empty.value && empty.error == InputError::empty, "empty field");
    const auto too_long = encode_field(std::u32string(33, U'é'), Alphabet::extended);
    check(!too_long.value && too_long.error == InputError::too_long && too_long.error_index == 32, "33 characters");
    const auto full = encode_field(std::u32string(32, U'ſ'), Alphabet::extended);
    check(full.value && full.value->length == 32 && full.value->codes[31] == 264, "32 characters of the last code");
}

}  // namespace

int main() {
    grammar_tests();
    alphabet_tests();
    std::printf("public names tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
