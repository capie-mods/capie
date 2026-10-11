/**
 * @file rn_country.hpp
 * @brief Maps `rn::` (country protocol and capability) and `rn::editor` (input codes) onto CAPIE.
 */
#pragma once
#include "../../src/features/country/capability.hpp"
#include "../../src/features/country/country_protocol.hpp"
#include "../../src/features/editor/input_codes.hpp"

/// @brief Old country namespace.
namespace rn {
using namespace capie::country;  // NOLINT(google-build-using-namespace): test compatibility shim
/// @brief Old editor namespace (input codes only).
namespace editor {
using namespace capie::editor;  // NOLINT(google-build-using-namespace): test compatibility shim
}
}  // namespace rn
