/**
 * @file catalog_build_test.cpp
 * @brief Offline tests of the flag and portrait catalogs: build, filters, paging, pick records (spec 12.2 items 2 to 5).
 *
 * Inputs are listings and sprite lists as the game reports them; no game, no EXE.
 */
#include "../src/features/catalog/catalog_build.hpp"
#include "../src/features/catalog/catalog_pick.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace build = capie::catalog::build;
namespace pick = capie::catalog::pick;

int checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
int failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

// Literal test data.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Names of a catalog, in order.
 * @param catalog Catalog.
 * @return Names.
 */
std::vector<std::string> names_of(const build::Catalog& catalog) {
    std::vector<std::string> out;
    for (const auto& item : catalog.items) {
        out.push_back(item.name);
    }
    return out;
}

void flag_tests() {
    const std::vector<std::string> large = {"gfx/flags/GER.tga", "gfx/flags/SOV.tga", "gfx/flags/GER_fascism.tga",
                                            "gfx/flags/ONLY_LARGE.tga", "gfx/flags/medium/GER.tga",
                                            "gfx/flags/bad-name.tga", "gfx/flags/readme.txt", "gfx/flags/GER.tga",
                                            "gfx/flags/" + std::string(64, 'A') + ".tga", "GFX/FLAGS/ENG.TGA"};
    const std::vector<std::string> medium = {"gfx/flags/medium/GER.tga", "gfx/flags/medium/SOV.tga",
                                             "gfx/flags/medium/GER_fascism.tga", "gfx/flags/medium/ENG.tga",
                                             "gfx/flags/medium/bad-name.tga"};
    const std::vector<std::string> small = {"gfx/flags/small/GER.tga", "gfx/flags/small/SOV.tga",
                                            "gfx/flags/small/GER_fascism.tga", "gfx/flags/small/ENG.tga",
                                            "gfx/flags/small/ONLY_LARGE.tga"};
    const auto catalog = build::flags(large, medium, small);
    check(names_of(catalog) == std::vector<std::string>{"ENG", "GER", "GER_fascism", "SOV"},
          "flags: stems in all three sizes, 17.3 only, sorted, unique (any case of the folder and extension)");
    check(catalog.items[1].kind == build::Kind::stem && catalog.items[1].texture == "gfx/flags/medium/GER.tga",
          "flag slot shows the medium file");
    // Filters.
    const auto own = build::flag_own(catalog, "GER", "");
    check(own.size() == 2 && catalog.items[own[0]].name == "GER" && catalog.items[own[1]].name == "GER_fascism",
          "flagown GER: GER and GER_*");
    std::vector<std::string> cos_large = {"gfx/flags/GER_KAISERREICH.tga", "gfx/flags/GER_KAISERREICH_x.tga",
                                          "gfx/flags/GERX.tga", "gfx/flags/D01.tga", "gfx/flags/D01_a.tga"};
    std::vector<std::string> cos_medium;
    std::vector<std::string> cos_small;
    for (const auto& entry : cos_large) {
        cos_medium.push_back("gfx/flags/medium/" + entry.substr(10));
        cos_small.push_back("gfx/flags/small/" + entry.substr(10));
    }
    const auto krx = build::flags(cos_large, cos_medium, cos_small);
    check(build::flag_own(krx, "XXX", "GER_KAISERREICH").size() == 2, "flagown: the cosmetic tag and its _ variants");
    check(build::flag_own(krx, "GER", "").size() == 2, "GER_* matches, GERX is not a GER flag");
    check(build::flag_own(krx, "D01", "").size() == 2, "dynamic tag D01");
    check(build::flag_own(krx, "", "GER_KAISERREICH").empty(), "not a country scope: an empty view");
    check(build::all(krx).size() == 5, "flag: every item");
}

void portrait_tests() {
    const std::vector<build::Sprite> sprites = {
        {"GFX_portrait_POL_ignacy_moscicki", "gfx/leaders/POL/moscicki.dds"},
        {"GFX_portrait_POL_ignacy_moscicki_small", "gfx/leaders/POL/small/moscicki.dds"},
        {"gfx_Leader_ABK_x", "gfx/leaders/ABK/x.png"},
        {"GFX_idea_foo", "gfx/interface/ideas/foo.dds"},
        {"GFX_portrait_d'Annunzio", "gfx/leaders/ITA/dannunzio.dds"},
        {"GFX_portrait_bad name", "gfx/leaders/x.dds"},
        {"GFX_portrait_POL_ignacy_moscicki", "gfx/leaders/POL/moscicki.dds"},
    };
    const std::vector<std::string> files = {"gfx/leaders/POL/moscicki.dds", "gfx/leaders/ABK/Portrait_ABK_Lakoba.png",
                                            "gfx/leaders/ABK/x.png", "gfx/leaders/ABK/lakoba.png", "gfx/leaders/ABK/notes.txt",
                                            "gfx/interface/x.png", "gfx/leaders/R56/\xC3\xA9.png",
                                            "gfx/leaders/ABK/Portrait_ABK_Lakoba.png"};
    const auto catalog = build::portraits(sprites, files);
    check(names_of(catalog) == std::vector<std::string>{"GFX_portrait_POL_ignacy_moscicki", "GFX_portrait_d'Annunzio",
                                                        "gfx/leaders/ABK/Portrait_ABK_Lakoba.png", "gfx/leaders/ABK/lakoba.png",
                                                        "gfx_Leader_ABK_x"},
          "portraits: prefixes in any case, no _small, a sprite's file dropped, path rules, sorted by bytes, unique");
    check(catalog.items[0].kind == build::Kind::sprite && catalog.items[0].texture == "gfx/leaders/POL/moscicki.dds" &&
              catalog.items[2].kind == build::Kind::path && catalog.items[2].texture == catalog.items[2].name,
          "kinds and textures");
    const auto abk = build::portrait_own(catalog, "ABK");
    check(abk.size() == 3, "portraitown ABK: /ABK/ paths (also without _ABK_ in the file name) and the _ABK_ sprite");
    check(build::portrait_own(catalog, "POL").size() == 1, "portraitown POL: _POL_");
    check(build::portrait_own(catalog, "").empty(), "not a country: an empty view");
}

void cap_tests() {
    std::vector<std::string> large;
    std::vector<std::string> medium;
    std::vector<std::string> small;
    for (int i = 0; i < 65540; ++i) {
        char stem[16]{};
        static_cast<void>(std::snprintf(stem, sizeof(stem), "F%05d", i));
        large.push_back(std::string("gfx/flags/") + stem + ".tga");
        medium.push_back(std::string("gfx/flags/medium/") + stem + ".tga");
        small.push_back(std::string("gfx/flags/small/") + stem + ".tga");
    }
    const auto catalog = build::flags(large, medium, small);
    check(catalog.items.size() == 65535 && catalog.dropped == 5 && catalog.items.back().name == "F65534",
          "cap of 65 535 items");
}

void paging_tests() {
    auto page = build::page_of(57, 40, 1);
    check(page.page == 1 && page.pages == 2 && page.first == 0 && page.count == 40, "page 1 of 2");
    page = build::page_of(57, 40, 2);
    check(page.page == 2 && page.first == 40 && page.count == 17, "last page: pn = 17");
    page = build::page_of(57, 40, 9);
    check(page.page == 2, "next at the end stays on the last page");
    page = build::page_of(57, 40, 0);
    check(page.page == 1, "prev at 1 stays on 1");
    page = build::page_of(0, 40, 1);
    check(page.page == 0 && page.pages == 0 && page.count == 0, "empty view: 0 / 0");
    check(build::page_of(10, 49, 1).pages == 0 && build::page_of(10, 0, 1).pages == 0, "page size 1..48");
    check(build::page_of(48, 48, 1).pages == 1 && build::page_of(49, 48, 2).count == 1, "page boundaries");
}

void pick_tests() {
    const auto base = pick::page(pick::catalog_flag, 57, build::page_of(57, 40, 2), 1, 0);
    std::int64_t value = 0;
    check(base.count == 10 && base.find("capie_cat", value) && value == 1 && base.find("capie_pg", value) &&
              value == 2 && base.find("capie_pn", value) && value == 17 && base.find("capie_par", value) && value == 1,
          "page header");
    auto got = pick::flag_pick(base, "POL_", "");
    check(!got.overflow && got.find("capie_sel_fmt", value) && value == 1 && got.find("capie_sel_len", value) &&
              value == 4 && got.find("capie_sel_0", value) && value == 174694 && got.find("capie_sel_1", value) &&
              value == 258048 && got.find("capie_cos_len", value) && value == 0 && !got.find("capie_cos_0", value),
          "flag pick: the stem in 17.3 (CONTRACT 17.3 example), no cosmetic tag");
    got = pick::flag_pick(base, "GER", "GER_KAISERREICH");
    check(got.find("capie_cos_len", value) && value == 15 && got.find("capie_cos_4", value) && !got.overflow,
          "flag pick: the cosmetic snapshot");
    got = pick::flag_pick(base, "GER", "RN1|x|y|");
    check(got.find("capie_cos_len", value) && value == -1, "cosmetic tag outside 17.3: -1");
    check(pick::flag_pick(base, "bad-stem", "").overflow, "a stem outside 17.3 is refused");
    const auto pbase = pick::page(pick::catalog_portrait, 3, build::page_of(3, 24, 1), 0, 1);
    got = pick::portrait_pick(pbase, "gfx/a.png", "gfx/a.png");
    check(!got.overflow && got.find("capie_sel_fmt", value) && value == 2 && got.find("capie_sel_0", value) &&
              value == 1700728 && got.find("capie_sml_2", value) && value == 1849191 && got.find("capie_sml_len", value) &&
              value == 9,
          "portrait pick: name and small name in packing B");
    const std::string longest = "gfx/leaders/" + std::string(110, 'x') + ".png";
    got = pick::portrait_pick(pbase, longest, longest);
    check(!got.overflow && got.count == 10 + 1 + 43 + 43 && got.find("capie_sel_41", value) && got.find("capie_sml_41", value),
          "126 characters: 97 records, the largest pick");
    check(pick::portrait_pick(pbase, "gfx/../a.png", "gfx/../a.png").overflow, "a bad path is refused");
    got = pick::portrait_pick(pbase, "GFX_portrait_x", "GFX_portrait_x_small");
    check(got.find("capie_sel_len", value) && value == 14 && got.find("capie_sml_len", value) && value == 20,
          "a sprite pick carries its _small sprite");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        flag_tests();
        portrait_tests();
        cap_tests();
        paging_tests();
        pick_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("catalog build tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
