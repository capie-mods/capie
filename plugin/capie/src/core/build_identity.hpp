/**
 * @file build_identity.hpp
 * @brief Keeps the build identity (supported game hash and CAPIE release) in every shipped binary.
 *
 * The release kit refuses a package whose PE files do not all contain `game_exe_sha256` and
 * `capie_release` of `game_build.hpp` (tools/release/releasekit/package.py): a manifest must never
 * claim a game build its binaries were not built for. A binary that never reads `capie_release`
 * would otherwise not contain it. Include this header once in the translation unit of each
 * binary's entry point (DLL, injector, launcher, proxy).
 */
#pragma once
#include "game_build.hpp"

/// @brief Pointers to the two identity strings; the linker keeps it (and so both strings) by name.
extern "C" __declspec(selectany) const char* const capie_build_identity[2] = {capie::core::game_exe_sha256,
                                                                              capie::core::capie_release};
#pragma comment(linker, "/INCLUDE:capie_build_identity")
