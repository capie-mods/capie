/**
 * @file rn_caption_focus.hpp
 * @brief Maps `rn::caption` and `rn::focus` (accepted suites) onto `capie::caption` / `capie::focus`.
 */
#pragma once
#include "../../src/features/caption/caption_refresh.hpp"
#include "../../src/features/focus/focus_release.hpp"

/// @brief Old caption namespace.
namespace rn::caption {
using namespace capie::caption;  // NOLINT(google-build-using-namespace): test compatibility shim
}
/// @brief Old focus namespace.
namespace rn::focus {
using namespace capie::focus;  // NOLINT(google-build-using-namespace): test compatibility shim
}
