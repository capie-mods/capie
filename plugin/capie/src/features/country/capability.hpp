/**
 * @file capability.hpp
 * @brief The read-only handshake between `capie_core.dll` and `capie_editor.dll`.
 *
 * The editor bridge sends a country rename only if `capie_core.dll` is loaded AND its
 * `RNCountryCapabilities()` export returns `country_capabilities_v1`, which it does only
 * after a successful initialization. Module presence alone is not enough. The value is
 * queried afresh before each country Apply and never stored. It is process-local: not a
 * saved flag and not evidence of multiplayer compatibility.
 */
#pragma once
#include <cstdint>

namespace capie::country {

/// @brief Value returned by `RNCountryCapabilities` after a successful init ('RNC' v1).
inline constexpr std::uint32_t country_capabilities_v1 = 0x524e4301;
/// @brief Name of the capability export.
inline constexpr char capabilities_export[] = "RNCountryCapabilities";

}  // namespace capie::country
