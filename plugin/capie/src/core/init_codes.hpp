/**
 * @file init_codes.hpp
 * @brief Result codes returned by the exported entry points (`RNInitialize`, `RNDebugRequest`).
 *
 * The numbers are part of the contract with the injector, the launcher and the debug
 * controller, and they are the same numbers the accepted rename_anything_* binaries
 * returned. Do not renumber them.
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
    module_pin_failed = 14,       ///< `GetModuleHandleExW(...PIN)` failed for the editor DLL.
    signature_mismatch = 20,      ///< A byte signature is not unique at its RVA.
    caption_pin_mismatch = 21,    ///< Caption vtable slot pin or token DWORD differs.
    focus_pin_mismatch = 22,      ///< Focus fingerprint, slot pin or RIP target differs.
    minhook_init_failed = 30,     ///< `MH_Initialize` failed.
    hook_create_failed = 31,      ///< `MH_CreateHook` failed for one target.
    hook_enable_failed = 32,      ///< Enabling hooks failed; partially enabled hooks were disabled.
    retry_refused = 33,           ///< A previous editor initialization failed; restart required.
    co_load_refused = 40,         ///< A predecessor or duplicate bridge DLL is already loaded.
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
