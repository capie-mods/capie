/**
 * @file init_codes.hpp
 * @brief Result codes returned by the exported entry points (`RNInitialize`, `RNDebugRequest`).
 *
 * The numbers are part of the contract with the injector, the launcher and the debug
 * controller, and they are the same numbers the accepted rename_anything_* binaries
 * returned. Do not renumber them.
 *
 * Fail-closed policy. Every pin table is checked before anything is hooked or called, and a
 * failure has one of two effects, fixed per owner:
 *
 * - Refuse the whole DLL (nothing is hooked, `RNInitialize` returns the code):
 *   - `capie_core.dll`: executable hash (10), PE/`.text` (11, 12), the localisation signatures
 *     `country_signatures` (20);
 *   - `capie_editor.dll`: executable hash (10), PE/`.text` (11, 12), `editor_signatures` and
 *     `caption_signatures` (20), caption slot and token pins (21), the focus signatures, slot
 *     pins and manager RIP pin (22), the vtable classes of `editor_vtable_classes` (23).
 * - Switch one feature off, log the failing pin id, keep the rest of the DLL running
 *   (`RNInitialize` still returns `ok`):
 *   - `capie_core.dll`: the name and faction layers (`name_layer_pins.hpp`), the flag layer
 *     (`flag_layer_pins.hpp`) and the portrait layer (`portrait_layer_pins.hpp` plus the RTTI
 *     check of `core_vtable_classes`); the flag and portrait layers also need the name layer;
 *   - `capie_editor.dll`: the console commands (`console_wiring.hpp`, developer builds only)
 *     and the host probe (`host_pins.hpp`, debug flavor only).
 *
 * Code 33 (`retry_refused`) means something different per DLL:
 * - `capie_core.dll`: another `RNInitialize` call is running right now. A failed attempt
 *   returns to idle, so a later call may try again.
 * - `capie_editor.dll`: an earlier attempt started and has not succeeded (it failed, or it is
 *   still running). Initialization is attempted once per process; after a failure the game
 *   must be restarted.
 */
#pragma once
#include <cstdint>

namespace capie::core {

/// @brief Exit code of a remote-thread entry point; zero means success or "queued".
enum class InitCode : std::uint32_t {
    ok = 0,                       ///< Initialized (or a debug request was queued).
    unsupported_executable = 10,  ///< Game executable SHA-256 differs from the pinned build.
    bad_pe_header = 11,           ///< DOS/NT header unreadable, wrong machine or too many sections.
    missing_text = 12,            ///< No readable `.text` section inside the image.
    log_unavailable = 13,         ///< Editor log file could not be created next to the DLL.
    module_pin_failed = 14,       ///< `GetModuleHandleExW(...PIN)` failed for the DLL.
    signature_mismatch = 20,      ///< A byte signature is not unique at its RVA.
    caption_pin_mismatch = 21,    ///< Caption vtable slot pin or token DWORD differs.
    focus_pin_mismatch = 22,      ///< Focus fingerprint, slot pin or RIP target differs.
    rtti_mismatch = 23,           ///< A vtable constant belongs to another class (MSVC RTTI, `rtti.hpp`).
    minhook_init_failed = 30,     ///< `MH_Initialize` failed.
    hook_create_failed = 31,      ///< `MH_CreateHook` failed for one target.
    hook_enable_failed = 32,      ///< Enabling hooks failed; partially enabled hooks were disabled.
    retry_refused = 33,           ///< Editor: an earlier attempt failed (restart); core: one is running (see above).
    co_load_refused = 40,         ///< A predecessor, duplicate or renamed copy of the DLL is loaded.
    inactive = 41,                ///< Debug request while the editor bridge is not active.
    invalid_request = 42,         ///< Debug request opcode outside 1..4.
    request_exhausted = 43,       ///< Debug request serial counter exhausted.
    unexpected_exception = 99,    ///< A C++ exception escaped initialization (fail-closed).
};

/**
 * @brief Converts an `InitCode` to the `DWORD` returned by an exported entry point.
 * @param code Result code.
 * @return The numeric value.
 */
constexpr std::uint32_t to_exit_code(InitCode code) noexcept {
    return static_cast<std::uint32_t>(code);
}

}  // namespace capie::core
