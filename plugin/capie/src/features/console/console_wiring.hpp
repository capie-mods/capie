/**
 * @file console_wiring.hpp
 * @brief Developer-build wiring of the console commands into `capie_editor.dll`.
 *
 * Included by `dll/capie_editor.cpp` only when `CAPIE_CONSOLE_CANDIDATE` is 1, which only
 * `build.ps1 -Console` sets (output `plugin/build/capie-console-dev`). Release and default
 * builds contain none of this code.
 *
 * Two entry points:
 *
 * - `prepare(text, image)` runs inside `RNInitialize` (injector thread) after the editor's
 *   own checks. It reads only the game's code bytes: 13 unique signatures, 25 instruction
 *   pins (the 22 entry/argv/result offsets, the cosmetic-tag offset and the gate order) and
 *   11 RIP/rel32 target pins (the console singleton slot, the calls from the verified
 *   `set_cosmetic_tag` body to the string assign, the tag parser, `country_of` and the
 *   cosmetic-tag setter, the dispatcher's gate message and lookup call, the game's own
 *   call of add-or-replace, and the
 *   sprite database global and sprite lookup used by the portrait validity check). Only
 *   when every check passes does it fill a `NativeApi` with `verified = true`. A failure
 *   leaves the console feature off and the editor running, with one log line.
 * - `on_tick()` runs from the Tick hook after the original Tick (game thread). It registers
 *   the commands once the console exists and logs the state and the Tick thread id. The
 *   bridge refuses any action that arrives on another thread and logs it.
 *
 * The registered action `console_action` forwards to the bridge.
 */
#pragma once
#include "../../core/log.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"
#include "console_bridge.hpp"
#include "console_layout.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace capie::console::wiring {

using core::uptr;

/// @brief Function prefixes that must be unique at their RVA (`raw/console_audit.txt`).
inline constexpr core::Signature console_signatures[] = {
    {"console.add_or_replace", core::SignatureGroup::editor_callee, layout::add_or_replace_rva,
     "415641574883EC484C8B3A4C8BF14863420C", "adds or replaces console entries by name (re-evidence 2)"},
    {"console.string_assign", core::SignatureGroup::editor_callee, layout::string_assign_rva,
     "48895C241048896C2418565741574883EC20488B6918", "MSVC std::string::assign on the game heap (9)"},
    {"console.singleton_create", core::SignatureGroup::editor_callee, 0x2537220,
     "48895C2410574883EC20B9E0000000E8????????", "creates the console and stores it in the singleton slot (1.1)"},
    {"console.dispatch", core::SignatureGroup::editor_callee, 0x2537340,
     "48895C241848895424105556574154415541564157488BEC4883EC70498BF0", "dispatcher: gate, then lookup (3)"},
    {"console.lookup", core::SignatureGroup::editor_callee, layout::lookup_rva,
     "48895C240848896C2410488974241848897C24204154415641574883EC204533F6", "lookup by name and alias (3)"},
    {"console.set_cosmetic_tag", core::SignatureGroup::editor_callee, layout::set_cosmetic_tag_rva,
     "48895C2418488974242048894C2408574883EC30488BF2488BF9", "vanilla set_cosmetic_tag action (5)"},
    {"console.drop_cosmetic_tag", core::SignatureGroup::editor_callee, layout::drop_cosmetic_tag_rva,
     "48895C241848894C24085556574154415541564157488BEC4881EC800000000F29742470488BDA",
     "vanilla drop_cosmetic_tag action (5)"},
    {"console.eval_effect", core::SignatureGroup::editor_callee, layout::eval_effect_rva,
     "48895C241048894C24085556574154415541564157488DAC2400FCFFFF", "vanilla eval_effect action (9, 10)"},
    {"console.tag_parse", core::SignatureGroup::editor_callee, layout::tag_parse_rva,
     "40534883EC50488BD9488D4C2430E8????????90", "country tag from a name string (10)"},
    {"console.country_of", core::SignatureGroup::editor_callee, layout::country_of_rva,
     "48895C2410574883EC30803D????????00488BD90F84????????803D????????00754F48833D????????007545488D05"
     "????????C6442440004889442428488D15????????488D44244041B10141B85B040000",
     "country object of a tag (10)"},
    {"console.country_set_cosmetic", core::SignatureGroup::editor_callee, layout::country_set_cosmetic_rva,
     "48895C241048896C24184889742420574883EC3048837A180F", "CCountry set cosmetic tag; +1488 (5)"},
    {"console.restore_device_objects", core::SignatureGroup::editor_callee, 0x2B9D00,
     "48895C2408488974241048897C241855488D6C24A94881EC00010000488BD9", "CGameIdler::RestoreDeviceObjects (2)"},
    {"console.sprite_find", core::SignatureGroup::editor_callee, layout::sprite_find_rva,
     "4881C100010000E9????????CCCCCCCC48895C2418", "sprite of a name in the sprite database (portrait review 1.4)"},
};

/// @brief The 22 layout pins of re-evidence.md section 7, the cosmetic-tag offset and the gate order
/// (each inside a function verified above).
inline constexpr core::InstructionPin instruction_pins[] = {
    {"entry.name", 0x2537AE9, "488d4d08"},
    {"entry.alias_count", 0x2537AFD, "397d28"},
    {"entry.aliases", 0x2537B02, "488d7530"},
    {"entry.stride", 0x2537B2D, "4981c7c8010000"},
    {"console.entry_count", 0x2537AC7, "4439710c"},
    {"entry.flag", 0x2537441, "803800"},
    {"entry.action_present", 0x2537461, "49837c246800"},
    {"entry.action", 0x253756A, "498b442468"},
    {"entry.context", 0x253756F, "4d8b442478"},
    {"argv.count", 0x253747F, "8b460c"},
    {"result.message", 0x2537584, "498d7f08"},
    {"console.gate_a", 0x2537397, "488b4950"},
    {"console.gate_c", 0x25373AE, "498b8dd0000000"},
    {"console.gate_b", 0x25373C8, "498b8d90000000"},
    {"entry.alias_count_assign", 0x25370B3, "894628"},
    {"entry.description", 0x25370AF, "488d4e48"},
    {"entry.action_assign", 0x25370F5, "48894668"},
    {"entry.context_assign", 0x253710A, "48894678"},
    {"entry.arg_name_count", 0x2537111, "898680000000"},
    {"entry.arg_name_slots", 0x25370F9, "bd0a000000"},
    {"console.allocator", 0x2536FC4, "498b4e10"},
    {"entry.size", 0x2536FE7, "4869d0c8010000"},
    {"country.cosmetic_tag", 0x759389, "488db188140000"},
    {"set_cosmetic_tag.argc", 0x299B00, "837a0c02"},
    {"drop_cosmetic_tag.argc", 0x26A734, "837a0c01"},
};

/// @brief RIP and rel32 targets (displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"console.singleton_store", 0x25372C7, 0x25372CB, layout::console_slot_rva},
    {"console.singleton_store2", 0x25372DE, 0x25372E2, layout::console_slot_rva},
    {"set_cosmetic_tag.assign", 0x299B65, 0x299B69, layout::string_assign_rva},
    {"set_cosmetic_tag.tag_parse", 0x299B0F, 0x299B13, layout::tag_parse_rva},
    {"set_cosmetic_tag.country_of", 0x299B23, 0x299B27, layout::country_of_rva},
    {"set_cosmetic_tag.setter", 0x299B2F, 0x299B33, layout::country_set_cosmetic_rva},
    {"dispatch.gate_message", 0x25373EF, 0x25373F3, layout::gate_message_rva},
    {"dispatch.lookup_after_gate", 0x2537431, 0x2537435, layout::lookup_rva},
    {"restore_device_objects.add", 0x2B9EDF, 0x2B9EE3, layout::add_or_replace_rva},
    {"portrait_valid.sprite_database", 0x1438E74, 0x1438E78, layout::sprite_database_rva},
    {"portrait_valid.sprite_find", 0x1438E79, 0x1438E7D, layout::sprite_find_rva},
};

/// @brief Exit-free log line writer for the bridge (appends CRLF).
/// @param line Line without line break.
inline void log_line(const char* line) noexcept {
    std::array<char, max_message_bytes + 3> buffer{};  // line, CR, LF, NUL: write_log stamps one line per call
    const int count = std::snprintf(buffer.data(), buffer.size(), "%s\r\n", line);
    if (count > 0) {
        core::logging::write_log(buffer.data(), core::logging::stored_bytes(count, buffer.size()));
    }
}

/// @brief Id of the calling thread. @return Thread id.
inline std::uint32_t thread_id() noexcept {
    return GetCurrentThreadId();
}

/**
 * @brief Checks every console signature, instruction pin and target pin (code bytes only).
 * @param text `.text` of the game image (the image base is `text.base`).
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), console_signatures,
                              instruction_pins, target_pins);
}

/// @brief Process memory policy used by the bridge.
inline const core::ProcessMemory memory{};
/// @brief The one bridge of this DLL (configured by `prepare`).
inline ConsoleBridge<core::ProcessMemory> bridge{memory, NativeApi{}, 0};
/// @brief Last registration state written to the log.
inline Registration logged_state = Registration::waiting;
/// @brief Set by a successful `prepare`; until then `on_tick` does nothing (no log line per Tick).
inline std::atomic<bool> prepared{false};

/**
 * @brief The action CAPIE registers for its three commands (game thread, called by the console dispatcher).
 * @param out Dispatcher's result storage.
 * @param argv Tokens after the command name.
 * @param context Entry context.
 * @return `out`.
 */
inline layout::ActionResult* console_action(layout::ActionResult* out, const layout::TempVector* argv,
                                            std::uint64_t context) noexcept {
    return bridge.run(out, argv, context);
}

/**
 * @brief Verifies every console pin and configures the bridge (injector thread, code bytes only).
 * @param text `.text` of the game image.
 * @return `true` when the console feature may register on a later Tick.
 */
inline bool prepare(const core::TextSection& text) noexcept {
    const auto image = reinterpret_cast<uptr>(text.base);
    const char* failed = first_failed_pin(text);
    std::array<char, max_message_bytes> line{};
    if (failed != nullptr) {
        static_cast<void>(std::snprintf(line.data(), line.size(),
                                        "CAPIE_CONSOLE disabled: pin %s failed; editor unaffected", failed));
        log_line(line.data());
        return false;
    }
    NativeApi api{};
    api.console_slot = image + layout::console_slot_rva;
    api.add_or_replace = reinterpret_cast<layout::AddOrReplaceFn>(image + layout::add_or_replace_rva);
    api.assign = reinterpret_cast<layout::AssignFn>(image + layout::string_assign_rva);
    api.set_cosmetic_tag = image + layout::set_cosmetic_tag_rva;
    api.drop_cosmetic_tag = image + layout::drop_cosmetic_tag_rva;
    api.eval_effect = image + layout::eval_effect_rva;
    api.tag_parse = reinterpret_cast<layout::TagParseFn>(image + layout::tag_parse_rva);
    api.country_of = reinterpret_cast<layout::CountryOfFn>(image + layout::country_of_rva);
    api.sprite_find = reinterpret_cast<layout::SpriteFindFn>(image + layout::sprite_find_rva);
    api.sprite_database_slot = image + layout::sprite_database_rva;
    api.current_thread = &thread_id;
    api.log = &log_line;
    api.verified = true;
    bridge.configure(api, reinterpret_cast<uptr>(&console_action));
    prepared.store(true, std::memory_order_release);
    static_cast<void>(std::snprintf(line.data(), line.size(),
                                    "CAPIE_CONSOLE verified: %zu signatures, %zu instruction pins, %zu target pins; "
                                    "registration on the first Tick",
                                    std::size(console_signatures), std::size(instruction_pins),
                                    std::size(target_pins)));
    log_line(line.data());
    return true;
}

/**
 * @brief Registration step; call from the Tick hook after the original Tick.
 */
inline void on_tick() noexcept {
    if (!prepared.load(std::memory_order_acquire) || bridge.state() != Registration::waiting) {
        return;
    }
    const auto state = bridge.try_register();
    if (state != logged_state) {
        logged_state = state;
        std::array<char, max_message_bytes> line{};
        static_cast<void>(std::snprintf(line.data(), line.size(), "CAPIE_CONSOLE registration=%s tick_thread=%lu",
                                        registration_name(state), GetCurrentThreadId()));
        log_line(line.data());
    }
}

}  // namespace capie::console::wiring
