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
/// @brief Name of the capability export.
inline constexpr char capabilities_export[] = "RNCountryCapabilities";

}  // namespace capie::country
