/**
 * @file rn_trace.hpp
 * @brief Maps `rn::debug` (trace ring suite) onto `capie::core::trace`.
 */
#pragma once
#include "../../src/core/trace_ring.hpp"

/// @brief Old trace namespace.
namespace rn::debug {
using namespace capie::core::trace;  // NOLINT(google-build-using-namespace): test compatibility shim
}
