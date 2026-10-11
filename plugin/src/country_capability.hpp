#pragma once
#include <cstdint>

namespace rn {
// Process-local, read-only handshake after successful exact-image initialization.
// This value is never a saved country flag or proof of multiplayer compatibility.
inline constexpr std::uint32_t country_capabilities_v1 = 0x524e4301;
}
