/** @file platform_game_layout.hpp
 * @brief Explicit platform selection; macOS exposes only its independently evidenced constants.
 */
#pragma once
#if defined(__APPLE__)
#include "macos_game_layout.hpp"
namespace capie::core {
namespace rva = macos::rva;
namespace off = macos::off;
namespace flag = macos::flag;
namespace value = macos::value;
}
#else
#include "game_layout.hpp"
#endif
