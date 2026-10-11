#include "../compat/rn_country.hpp"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, std::string_view label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void expect_codes(std::u32string_view input,
                  std::initializer_list<std::uint8_t> expected,
                  std::string_view label) {
    const auto result = rn::editor::encode_field(input);
    check(result.value.has_value(), label);
    check(result.error == rn::editor::InputError::none, "accepted field has no error");
    check(result.error_index == std::u32string_view::npos, "accepted field has no error index");
    if (!result.value) return;
    check(result.value->length == expected.size(), "exact output length");
    std::size_t index = 0;
    for (const auto code : expected) {
        check(result.value->codes[index++] == code, label);
    }
    for (; index < result.value->codes.size(); ++index) {
        check(result.value->codes[index] == 0, "unused command slots are zero");
    }
}

void expect_error(std::u32string_view input, rn::editor::InputError error,
                  std::size_t index, std::string_view label) {
    const auto result = rn::editor::encode_field(input);
    check(!result.value.has_value(), "rejected field exposes no partial command data");
    check(result.error == error, label);
    check(result.error_index == index, "exact error codepoint index");
}

int dump_mapping() {
    // Independently compare every accepted scalar with tools/gen.py CHARS.
    // This also detects accidentally accepted characters outside that table.
    for (std::uint32_t scalar = 0; scalar <= 0x10ffffU; ++scalar) {
        const auto point = static_cast<char32_t>(scalar);
        const auto result = rn::editor::encode_field(std::u32string_view(&point, 1));
        if (result.value) {
            std::cout << scalar << ' ' << static_cast<unsigned>(result.value->codes[0]) << '\n';
        }
    }
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--dump-mapping") return dump_mapping();
    if (argc != 1) return 2;

    using rn::editor::InputError;
    expect_error(U"", InputError::empty, 0, "empty text is rejected");

    // Literal expectations come from the existing script protocol, not a helper
    // in the converter. A shifted alphabet or reordered Polish letters fails.
    expect_codes(U"azAZ09 -.'", {1, 26, 27, 52, 53, 62, 63, 64, 65, 66}, "ASCII code boundaries");
    expect_codes(U"ąćęłńóśźż", {67, 68, 69, 70, 71, 72, 73, 74, 75}, "Polish lowercase codes");
    expect_codes(U"ĄĆĘŁŃÓŚŹŻ", {76, 77, 78, 79, 80, 81, 82, 83, 84}, "Polish uppercase codes");
    expect_codes(U"Nowy Gdańsk", {40, 15, 23, 25, 63, 33, 4, 1, 71, 19, 11}, "mixed city name");
    expect_codes(U" a ", {63, 1, 63}, "leading and trailing spaces are preserved");
    expect_codes(U"  ", {63, 63}, "nonempty spaces remain explicit user input");

    const auto maximum_ascii = rn::editor::encode_field(std::u32string(32, U'z'));
    check(maximum_ascii.value && maximum_ascii.value->length == 32, "32 ASCII codepoints accepted");
    if (maximum_ascii.value) {
        for (const auto code : maximum_ascii.value->codes) check(code == 26, "last ASCII code is retained");
    }
    const auto maximum_polish = rn::editor::encode_field(std::u32string(32, U'Ż'));
    check(maximum_polish.value && maximum_polish.value->length == 32, "32 Polish codepoints accepted");
    if (maximum_polish.value) {
        for (const auto code : maximum_polish.value->codes) check(code == 84, "last Polish code is retained");
    }
    expect_error(std::u32string(33, U'a'), InputError::too_long, 32, "33 codepoints are rejected, not truncated");
    expect_error(std::u32string(33, U'ą'), InputError::too_long, 32, "Unicode length counts codepoints");
    expect_error(std::u32string(128, U'z'), InputError::too_long, 32, "oversized text is rejected");

    // Explicit-size views preserve embedded NUL; C-string construction would
    // hide that suffix and silently change the command's intended text.
    const char32_t embedded_nul[] = {U'a', U'\0', U'b'};
    expect_error(std::u32string_view(embedded_nul, 3), InputError::control_character, 1, "embedded NUL rejected");
    for (std::uint32_t scalar = 0; scalar < 0x20U; ++scalar) {
        const char32_t input[] = {U'a', static_cast<char32_t>(scalar)};
        expect_error(std::u32string_view(input, 2), InputError::control_character, 1, "C0 control rejected");
    }
    for (std::uint32_t scalar = 0x7fU; scalar <= 0x9fU; ++scalar) {
        const char32_t input[] = {U'a', static_cast<char32_t>(scalar)};
        expect_error(std::u32string_view(input, 2), InputError::control_character, 1, "DEL/C1 control rejected");
    }

    for (const auto point : std::u32string_view(U"\"[]|$§£\\{}#@")) {
        const char32_t input[] = {U'a', point};
        expect_error(std::u32string_view(input, 2), InputError::render_delimiter, 1, "script/loc delimiter rejected");
    }
    for (const auto point : std::u32string_view(U":,_=+/*éä\u00a0\u0301\u200b\u202e\U0001f600")) {
        const char32_t input[] = {U'a', point};
        expect_error(std::u32string_view(input, 2), InputError::unsupported_character, 1, "unsupported text rejected");
    }
    expect_error(U"o\u0301", InputError::unsupported_character, 1, "decomposed accent is not silently normalized");
    for (const std::uint32_t scalar : {0xd800U, 0xdbffU, 0xdc00U, 0xdfffU, 0x110000U, 0xffffffffU}) {
        const char32_t input[] = {U'a', static_cast<char32_t>(scalar)};
        expect_error(std::u32string_view(input, 2), InputError::invalid_scalar, 1, "invalid UTF-32 scalar rejected");
    }
    auto invalid_last = std::u32string(31, U'a');
    invalid_last += U'!';
    expect_error(invalid_last, InputError::unsupported_character, 31, "invalid final slot does not return a prefix");
    expect_error(U"ok|?", InputError::render_delimiter, 2, "first invalid codepoint is reported");

    const auto first = rn::editor::encode_field(U"Ala");
    const auto second = rn::editor::encode_field(U"Ż");
    check(first.value && second.value && first.value->length == 3 && second.value->length == 1,
          "each field owns its output length");
    check(first.value && first.value->codes[0] == 27 && first.value->codes[1] == 12 && first.value->codes[2] == 1,
          "encoding another field does not overwrite the first");

    if (failures != 0) {
        std::cerr << "editor input: " << failures << " failures in " << checks << " checks\n";
        return 1;
    }
    std::cout << "editor input: " << checks << " checks passed\n";
    return 0;
}
