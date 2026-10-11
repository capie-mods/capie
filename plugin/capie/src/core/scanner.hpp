/**
 * @file scanner.hpp
 * @brief Inactive skeleton of the runtime signature scanner (decision 10(a)). Not called anywhere.
 *
 * Purpose, once implemented: on a game build whose SHA-256 is not pinned, and only when
 * the player turned the option on, find every signature of `signature_table.hpp` again
 * and enable CAPIE only if every check passes (all or nothing, RE findings 6.4):
 *   1. locate each target by its anchor (RTTI, unique string, known caller) AND by its
 *      masked prefix; both must agree on exactly one address;
 *   2. compare the whole-function masked hash with the tested value;
 *   3. resolve the rel32 callees we rely on and check their hashes or anchors;
 *   4. check RIP data targets, vtable slots and token DWORDs by content;
 *   5. check the out-of-hook offset pins (RE 6.3);
 *   6. otherwise enable nothing and show the "CAPIE inactive" indicator.
 *
 * What this stage ships: the data shapes and a function that always answers "disabled".
 * No code path calls it, so an unknown SHA is still refused with code 10, exactly as
 * before. Building the scanner needs the anchors and masked hashes of RE section 6 as
 * table columns and its own review; see the stage-2 report, OPEN list.
 */
#pragma once
#include "pe_image.hpp"
#include "signatures.hpp"

namespace capie::core::scanner {

/// @brief Outcome of a scan request.
enum class ScanResult : unsigned {
    disabled,           ///< Scanner not implemented or not enabled by the player (always, in 1.0).
    all_verified,       ///< Every entry found and byte-identical (reserved).
    refused_ambiguous,  ///< At least one entry missing, moved or ambiguous (reserved).
};

/// @brief Player-facing switch; the option does not exist in 1.0, so it is always off.
inline constexpr bool scanner_enabled = false;

/**
 * @brief Placeholder for the all-or-nothing scan of an unknown build.
 * @param text `.text` of the running image (unused).
 * @return Always `ScanResult::disabled` in this stage.
 * @note Not called. Kept so the table layout and call site can be reviewed early.
 */
inline ScanResult scan_unknown_build(const TextSection& text) noexcept {
    static_cast<void>(text);
    return ScanResult::disabled;
}

}  // namespace capie::core::scanner
