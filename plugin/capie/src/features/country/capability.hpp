/**
 * @file capability.hpp
 * @brief The read-only handshake between `capie_core.dll` and `capie_editor.dll`.
 *
 * The editor bridge sends a country rename only if `capie_core.dll` is loaded AND its
 * `RNCountryCapabilities()` export returns `country_capabilities_v2`, which it does only
 * after a successful initialization with the name layer active (`v1`: initialized, layer
 * off). Module presence alone is not enough. The value is queried afresh before each
 * country Apply and never stored. It is process-local: not a saved flag and not evidence of
 * multiplayer compatibility.
 */
#pragma once
#include <cstdint>

namespace capie::country {

/// @brief Value returned by `RNCountryCapabilities` after a successful init without the name layer ('RNC' v1).
inline constexpr std::uint32_t country_capabilities_v1 = 0x524e4301;
/// @brief Value returned when the name layer (API level 4) is active too ('RNC' v2).
/// The editor bridge requires it for a country Apply: without the layer a stored name would not be shown.
inline constexpr std::uint32_t country_capabilities_v2 = 0x524e4302;
/// @brief Value returned when the flag layer (API level 6) is active as well ('RNC' v3).
/// A country Apply accepts v2 and v3; grammar Apply commands report v3 as `capie_flag = 1`.
inline constexpr std::uint32_t country_capabilities_v3 = 0x524e4303;
/// @brief Base of the v4 family ('RND'): at least one of the API level 7 and 8 layers (portrait, colour,
/// ruling-party ideology name) is active; the low bits name the active optional layers. The name layers are
/// always on in this family.
inline constexpr std::uint32_t country_capabilities_v4 = 0x524e4400;
/// @brief v4 bit: the flag layer is active too.
inline constexpr std::uint32_t capability_flag_bit = 0x1;
/// @brief v4 bit: the portrait layer (API level 7) is active.
inline constexpr std::uint32_t capability_portrait_bit = 0x2;
/// @brief v4 bit: the country colour layer (API level 8) is active.
inline constexpr std::uint32_t capability_colour_bit = 0x4;
/// @brief v4 bit: the ruling-party ideology name layer (API level 8) is active.
inline constexpr std::uint32_t capability_subideology_bit = 0x8;
/// @brief The v4 bits of which at least one must be set (the layers that select the v4 family).
inline constexpr std::uint32_t capability_v4_layers =
    capability_portrait_bit | capability_colour_bit | capability_subideology_bit;
/// @brief Mask of the v4 layer bits.
inline constexpr std::uint32_t capability_bits = capability_flag_bit | capability_v4_layers;

/**
 * @brief The value `RNCountryCapabilities` returns for a set of active layers.
 * @param names Name layers active.
 * @param flag Flag layer active.
 * @param portrait Portrait layer active.
 * @param colour Colour layer active.
 * @param subideology Ruling-party ideology name layer active.
 * @return v1 without the name layers (the other layers need them), else v2, v3 or a v4 value.
 */
constexpr std::uint32_t capabilities_of(bool names, bool flag, bool portrait, bool colour = false,
                                        bool subideology = false) noexcept {
    if (!names) {
        return country_capabilities_v1;
    }
    if (portrait || colour || subideology) {
        return country_capabilities_v4 | (flag ? capability_flag_bit : 0U) |
               (portrait ? capability_portrait_bit : 0U) | (colour ? capability_colour_bit : 0U) |
               (subideology ? capability_subideology_bit : 0U);
    }
    return flag ? country_capabilities_v3 : country_capabilities_v2;
}

/**
 * @brief Whether a value is a v4 family value.
 * @param value Capability value.
 * @return `true` for the v4 base with only defined bits and at least one v4 layer bit.
 */
constexpr bool is_v4(std::uint32_t value) noexcept {
    return (value & ~capability_bits) == country_capabilities_v4 && (value & capability_v4_layers) != 0;
}

/**
 * @brief Whether the name layers are active.
 * @param value Capability value.
 * @return `true` for v2, v3 or a v4 value.
 */
constexpr bool names_active(std::uint32_t value) noexcept {
    return value == country_capabilities_v2 || value == country_capabilities_v3 || is_v4(value);
}

/**
 * @brief Whether the flag layer is active.
 * @param value Capability value.
 * @return `true` for v3 or a v4 value with the flag bit.
 */
constexpr bool flag_active(std::uint32_t value) noexcept {
    return value == country_capabilities_v3 || (is_v4(value) && (value & capability_flag_bit) != 0);
}

/**
 * @brief Whether the leader portrait layer is active.
 * @param value Capability value.
 * @return `true` for a v4 value with the portrait bit.
 */
constexpr bool portrait_active(std::uint32_t value) noexcept {
    return is_v4(value) && (value & capability_portrait_bit) != 0;
}

/**
 * @brief Whether the country colour layer is active.
 * @param value Capability value.
 * @return `true` for a v4 value with the colour bit.
 */
constexpr bool colour_active(std::uint32_t value) noexcept {
    return is_v4(value) && (value & capability_colour_bit) != 0;
}

/**
 * @brief Whether the ruling-party ideology name layer is active.
 * @param value Capability value.
 * @return `true` for a v4 value with the sub-ideology bit.
 */
constexpr bool subideology_active(std::uint32_t value) noexcept {
    return is_v4(value) && (value & capability_subideology_bit) != 0;
}

/// @brief Name of the capability export.
inline constexpr char capabilities_export[] = "RNCountryCapabilities";

}  // namespace capie::country
