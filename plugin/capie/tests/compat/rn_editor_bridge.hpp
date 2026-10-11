/**
 * @file rn_editor_bridge.hpp
 * @brief Makes the old global names of the generated rev7 bridge resolve to the CAPIE editor DLL.
 *
 * The accepted suites included `caption_refresh_bridge.cpp` and used its internal names
 * (lifecycle, inspect_window, original_tick, image_base, ...) unqualified. They now
 * include the composition root `capie_editor.cpp` through this header.
 */
#pragma once
#include "../../src/dll/capie_editor.cpp"
#include "rn_caption_focus.hpp"

// NOLINTBEGIN(google-build-using-namespace): test compatibility shim
using namespace capie::editor;
using namespace capie::engine;
using namespace capie::core::logging;
// NOLINTEND(google-build-using-namespace)
using capie::caption::caption_armed;
using capie::caption::caption_bind;
using capie::caption::caption_dispatch_allowed;
using capie::caption::caption_dispatch_verdict;
using capie::caption::caption_prefill;
using capie::caption::caption_service;
using capie::caption::caption_tick;
using capie::focus::focus_armed;
using capie::focus::focus_bind;
using capie::focus::focus_tick;

/// @brief Old trace namespace.
namespace rn::debug {
using namespace capie::core::trace;  // NOLINT(google-build-using-namespace): test compatibility shim
}
