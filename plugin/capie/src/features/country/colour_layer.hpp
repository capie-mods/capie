/**
 * @file colour_layer.hpp
 * @brief The player-chosen country colour as its own layer (API level 8): variables, colour maths, colour block.
 *
 * A mod stores the chosen colour in the country's own variables (CONTRACT.md section 19):
 *
 *     capie_cc_ver      1 = the layer is set (format 1); absent = no layer
 *     capie_cc_r/g/b    the colour as in colors.txt `rgb { R G B }`, whole numbers 0..255
 *     capie_cc_cos_len  snapshot of the cosmetic tag at the choice: absent = none, 0 = no tag, 1..63
 *     capie_cc_cos      that cosmetic tag, packed as in 17.3 (like capie_cf_cos)
 *     capie_cc_rev      set last by every change (repaint trigger, value unused)
 *
 * `read_colour_layer` decodes them with the variable lookup of `name_layer.hpp` and the packed
 * snapshot reader of `flag_layer.hpp`; `colour_verdict` applies the cosmetic-tag gate.
 *
 * `adjust` reproduces, operation for operation in `float`, what the game does when it reads a
 * `colors.txt` colour (token reader `CCountryColors`, `rgb` parse, then RGB to HSV, `H += hue`,
 * `S *= saturation`, `V *= brightness`, HSV to RGB, alpha 1; checked in Ghidra on 1.19.4.0):
 * - RGB to HSV: `V = max`; zero gives (0, 0, 0); the channels divided by V give `S = max - min`;
 *   zero `S` gives hue 0; otherwise the channels are normalised by `S` and the hue is
 *   `60 * (g - b)` (+360 when negative), `60 * (b - r) + 120` or `60 * (r - g) + 240` for the
 *   first channel equal to the maximum in the order r, g, b, divided by 360;
 * - HSV to RGB: `S == 0` gives (V, V, V); `i = min(floor(6H), 5)` (no lower clamp: a negative
 *   sector takes the last case), `f = 6H - i`, the usual six cases with `p = (1 - S)V`,
 *   `q = (1 - fS)V`, `t = (1 - (1 - f)S)V`. There is no wrap and no clamp.
 *
 * `make_block` fills a `ColourBlock` laid out exactly as the game's `CCountryColors` (0x60 bytes),
 * the object `GetMapColors` returns. Pure functions; no writes to game memory, no game calls.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "flag_layer.hpp"
#include "name_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace capie::country::colour {

using core::uptr;
using flag::Text;
using layer::Lookup;
using layer::Status;
using layer::Table;

/// @brief Format variable; `1` while the layer is set.
inline constexpr std::string_view version_variable = "capie_cc_ver";
/// @brief Red channel.
inline constexpr std::string_view red_variable = "capie_cc_r";
/// @brief Green channel.
inline constexpr std::string_view green_variable = "capie_cc_g";
/// @brief Blue channel.
inline constexpr std::string_view blue_variable = "capie_cc_b";
/// @brief Length of the cosmetic snapshot (absent = no snapshot).
inline constexpr std::string_view cosmetic_length_variable = "capie_cc_cos_len";
/// @brief Packed cosmetic snapshot array.
inline constexpr std::string_view cosmetic_array = "capie_cc_cos";
/// @brief Commit variable; every change ends with `set_variable = { capie_cc_rev = 1 }`.
inline constexpr char commit_variable[] = "capie_cc_rev";
/// @brief Largest channel value.
inline constexpr std::int64_t max_channel = 255;

/// @brief What a country shows.
enum class Verdict : unsigned {
    valid,       ///< The layer is shown.
    absent,      ///< No layer.
    invalid,     ///< A value is out of range.
    unreadable,  ///< The variables could not be read.
    hidden,      ///< Valid, but the cosmetic tag no longer matches the snapshot.
};

/// @brief The colour layer of one country.
struct Layer {
    Status status{Status::absent};  ///< Result of reading the variables.
    std::uint32_t rgb{};            ///< `0xRRGGBB` when valid.
    bool has_snapshot{};            ///< `capie_cc_cos_len` present.
    Text snapshot;                  ///< Cosmetic tag at the choice (empty = it had none).
};

/**
 * @brief Reads one channel.
 * @param mem Memory policy.
 * @param table Variable table.
 * @param name Variable name.
 * @param out Receives 0..255.
 * @return `valid`, `invalid` (missing, a fraction or out of range) or `unreadable`.
 */
template <class M>
Status read_channel(const M& mem, const Table& table, std::string_view name, std::int64_t& out) noexcept {
    std::int64_t raw = 0;
    const auto found = layer::find_variable(mem, table, name, raw);
    if (found == Lookup::unreadable) {
        return Status::unreadable;
    }
    if (found == Lookup::absent || !layer::whole(raw, out) || out < 0 || out > max_channel) {
        return Status::invalid;
    }
    return Status::valid;
}

/**
 * @brief Reads the colour layer of a country.
 * @param mem Memory policy.
 * @param country Country object address.
 * @return The layer; `status != valid` means "no layer".
 */
template <class M>
Layer read_colour_layer(const M& mem, uptr country) noexcept {
    Layer out{};
    layer::Layer opened{};
    Table table{};
    layer::open_layer(mem, country, version_variable, table, opened);
    out.status = opened.status;
    if (out.status != Status::valid) {
        return out;
    }
    std::int64_t channels[3]{};
    const std::string_view names[3] = {red_variable, green_variable, blue_variable};
    for (std::size_t i = 0; i < 3 && out.status == Status::valid; ++i) {
        out.status = read_channel(mem, table, names[i], channels[i]);
    }
    if (out.status == Status::valid) {
        const auto snapshot = flag::read_packed(mem, table, cosmetic_length_variable, cosmetic_array, true, out.snapshot);
        if (snapshot == Status::valid) {
            out.has_snapshot = true;
        } else if (snapshot != Status::absent) {
            out.status = snapshot;
        }
    }
    if (out.status != Status::valid) {
        out.snapshot = Text{};
        out.has_snapshot = false;
        return out;
    }
    constexpr unsigned byte_bits = 8;
    out.rgb = (static_cast<std::uint32_t>(channels[0]) << (2 * byte_bits)) |
              (static_cast<std::uint32_t>(channels[1]) << byte_bits) | static_cast<std::uint32_t>(channels[2]);
    return out;
}

/**
 * @brief What a country shows, given its current cosmetic tag (CONTRACT 19.4).
 * @param colour Layer read from the variables.
 * @param cosmetic Current cosmetic tag.
 * @param cosmetic_too_long The current tag is longer than any snapshot.
 * @return `valid` only when the layer is valid and its snapshot (if any) still matches.
 */
constexpr Verdict colour_verdict(const Layer& colour, std::string_view cosmetic, bool cosmetic_too_long) noexcept {
    switch (colour.status) {
        case Status::absent:
            return Verdict::absent;
        case Status::invalid:
            return Verdict::invalid;
        case Status::unreadable:
            return Verdict::unreadable;
        case Status::valid:
            break;
    }
    if (!colour.has_snapshot) {
        return Verdict::valid;
    }
    return !cosmetic_too_long && colour.snapshot.view() == cosmetic ? Verdict::valid : Verdict::hidden;
}

/**
 * @brief Short name of a verdict for log lines.
 * @param verdict Verdict.
 * @return Text.
 */
constexpr const char* verdict_name(Verdict verdict) noexcept {
    switch (verdict) {
        case Verdict::valid:
            return "valid";
        case Verdict::absent:
            return "absent";
        case Verdict::invalid:
            return "invalid";
        case Verdict::unreadable:
            return "unreadable";
        case Verdict::hidden:
            return "hidden";
    }
    return "?";
}

// ---- Colour maths (the game's float operations) -------------------------------------------

/// @brief Hue, saturation and value modifiers of one define set.
struct Defines {
    float hue{};         ///< Added to the hue (fraction of a turn).
    float saturation{};  ///< Multiplies the saturation.
    float value{};       ///< Multiplies the value.
};

/// @brief An RGB colour in floats (alpha is always 1).
struct Rgb {
    float r{};  ///< Red.
    float g{};  ///< Green.
    float b{};  ///< Blue.
};

/// @brief The game's `maxss` (first operand unless the second is greater).
/// @param a First. @param b Second. @return Maximum.
constexpr float max_ss(float a, float b) noexcept {
    return a > b ? a : b;
}

/// @brief The game's `minss` (first operand unless the second is smaller).
/// @param a First. @param b Second. @return Minimum.
constexpr float min_ss(float a, float b) noexcept {
    return a < b ? a : b;
}

/**
 * @brief `floorf` as the game's CRT computes it, usable in constant expressions.
 * @param x Value.
 * @return Largest integral value not greater than `x` (NaN and integral magnitudes unchanged).
 */
constexpr float floor_f(float x) noexcept {
    constexpr float integral_from = 8388608.0F;  // 2^23: every float from here on is integral
    if (x != x || x >= integral_from || x <= -integral_from) {
        return x;
    }
    const auto truncated = static_cast<float>(static_cast<std::int64_t>(x));
    return truncated > x ? truncated - 1.0F : truncated;
}

/// @brief Hue, saturation, value.
struct Hsv {
    float h{};  ///< Hue in [0, 1).
    float s{};  ///< Saturation.
    float v{};  ///< Value.
};

/**
 * @brief RGB to HSV, operation for operation as the game's converter.
 * @param in Colour (0..1 per channel).
 * @return HSV.
 */
constexpr Hsv to_hsv(Rgb in) noexcept {
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers): degrees of the hue sectors
    const float v = max_ss(max_ss(in.r, in.g), in.b);
    if (v == 0.0F) {
        return {};
    }
    const float r1 = in.r / v;
    const float g1 = in.g / v;
    const float b1 = in.b / v;
    const float high = max_ss(max_ss(r1, g1), b1);
    const float low = min_ss(min_ss(r1, g1), b1);
    const float s = high - low;
    if (s == 0.0F) {
        return {0.0F, 0.0F, v};
    }
    const float r2 = (r1 - low) / s;
    const float g2 = (g1 - low) / s;
    const float b2 = (b1 - low) / s;
    const float top = max_ss(max_ss(r2, g2), b2);
    float degrees = 0.0F;
    if (r2 == top) {
        degrees = ((g2 - b2) * 60.0F) + 0.0F;
        if (0.0F > degrees) {
            degrees += 360.0F;
        }
    } else if (g2 == top) {
        degrees = ((b2 - r2) * 60.0F) + 120.0F;
    } else {
        degrees = ((r2 - g2) * 60.0F) + 240.0F;
    }
    return {degrees / 360.0F, s, v};
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
}

/**
 * @brief HSV to RGB, operation for operation as the game's converter.
 * @param in HSV (no range assumed).
 * @return RGB.
 */
constexpr Rgb to_rgb(Hsv in) noexcept {
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers): the six hue sectors
    if (in.s == 0.0F) {
        return {in.v, in.v, in.v};
    }
    const float h6 = in.h * 6.0F;
    const float sector = min_ss(5.0F, floor_f(h6));
    const float f = h6 - sector;
    const float p = (1.0F - in.s) * in.v;
    const float q = (1.0F - (f * in.s)) * in.v;
    const float t = (1.0F - ((1.0F - f) * in.s)) * in.v;
    if (sector == 0.0F) {
        return {in.v, t, p};
    }
    if (sector == 1.0F) {
        return {q, in.v, p};
    }
    if (sector == 2.0F) {
        return {p, in.v, t};
    }
    if (sector == 3.0F) {
        return {p, q, in.v};
    }
    if (sector == 4.0F) {
        return {t, p, in.v};
    }
    return {in.v, p, q};
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
}

/**
 * @brief Converts `0xRRGGBB` to floats the way the `rgb { }` parser does (each channel / 255).
 * @param rgb Packed colour.
 * @return Floats.
 */
constexpr Rgb from_packed(std::uint32_t rgb) noexcept {
    constexpr float channel_max = 255.0F;
    constexpr unsigned byte_bits = 8;
    constexpr std::uint32_t byte_mask = 0xFFU;
    return {static_cast<float>((rgb >> (2 * byte_bits)) & byte_mask) / channel_max,
            static_cast<float>((rgb >> byte_bits) & byte_mask) / channel_max,
            static_cast<float>(rgb & byte_mask) / channel_max};
}

/**
 * @brief The game's colour adjustment: RGB to HSV, apply the defines, back to RGB.
 * @param rgb Packed colour `0xRRGGBB`.
 * @param defines Modifiers (the map set or the UI set).
 * @return Adjusted colour.
 */
constexpr Rgb adjust(std::uint32_t rgb, Defines defines) noexcept {
    Hsv hsv = to_hsv(from_packed(rgb));
    hsv.h += defines.hue;
    hsv.s *= defines.saturation;
    hsv.v *= defines.value;
    return to_rgb(hsv);
}

// ---- Colour block (the game's CCountryColors) ------------------------------------------------

/// @brief One `CColor`: vtable, an unused word, RGBA floats (0x20 bytes).
struct ColorObject {
    uptr vtable;                   ///< `rva::vt_color`.
    uptr pad;                      ///< Unused by every reader (zero).
    std::array<float, 4> rgba;     ///< Red, green, blue, alpha.
};

/// @brief A colour block laid out exactly as `CCountryColors` (0x60 bytes).
struct ColourBlock {
    uptr vtable;                        ///< `rva::vt_country_colors`.
    uptr pad08;                         ///< Unused by every reader (zero).
    ColorObject color;                  ///< +0x10: map colour (`color`).
    ColorObject color_ui;               ///< +0x30: UI colour (`color_ui`).
    std::uint8_t flags;                 ///< +0x50: 1 `color` read, 2 `color_ui` read.
    std::array<std::uint8_t, 15> pad51;  ///< Unused.
};
/// @brief Size of `CCountryColors`.
inline constexpr std::size_t block_size = 0x60;
static_assert(sizeof(ColorObject) == 0x20 && offsetof(ColorObject, rgba) == core::off::color_rgba);
static_assert(sizeof(ColourBlock) == block_size && offsetof(ColourBlock, color) == core::off::colors_color &&
              offsetof(ColourBlock, color_ui) == core::off::colors_color_ui &&
              offsetof(ColourBlock, flags) == core::off::colors_flags);

/**
 * @brief Fills a colour block for a picked colour.
 * @param image Image base (vtables).
 * @param rgb Packed colour.
 * @param map_defines `COUNTRY_COLOR_*` modifiers (for `color`).
 * @param ui_defines `COUNTRY_UI_COLOR_*` modifiers (for `color_ui`).
 * @return The block.
 */
constexpr ColourBlock make_block(uptr image, std::uint32_t rgb, Defines map_defines, Defines ui_defines) noexcept {
    const Rgb map = adjust(rgb, map_defines);
    const Rgb ui = adjust(rgb, ui_defines);
    ColourBlock block{};
    block.vtable = image + core::rva::vt_country_colors;
    block.color = {image + core::rva::vt_color, 0, {map.r, map.g, map.b, 1.0F}};
    block.color_ui = {image + core::rva::vt_color, 0, {ui.r, ui.g, ui.b, 1.0F}};
    block.flags = core::value::colors_both_read;
    return block;
}

/**
 * @brief Reads the six colour defines from the running game.
 * @param mem Memory policy.
 * @param image Image base.
 * @param map_defines Receives `COUNTRY_COLOR_*`.
 * @param ui_defines Receives `COUNTRY_UI_COLOR_*`.
 * @return `false` when one is unreadable.
 */
template <class M>
bool read_defines(const M& mem, uptr image, Defines& map_defines, Defines& ui_defines) noexcept {
    return core::rd(mem, image, core::rva::define_color_hue, map_defines.hue) &&
           core::rd(mem, image, core::rva::define_color_saturation, map_defines.saturation) &&
           core::rd(mem, image, core::rva::define_color_value, map_defines.value) &&
           core::rd(mem, image, core::rva::define_ui_color_hue, ui_defines.hue) &&
           core::rd(mem, image, core::rva::define_ui_color_saturation, ui_defines.saturation) &&
           core::rd(mem, image, core::rva::define_ui_color_value, ui_defines.value);
}

}  // namespace capie::country::colour
