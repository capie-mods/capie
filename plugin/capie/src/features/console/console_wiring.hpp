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
 *   11 RIP/rel32 target pins (the console singleton slot `35E1A80`, the calls from the
 *   verified `set_cosmetic_tag` body to `129CA0`, `10E10C0`, `BB4390`, `711260`, the
 *   dispatcher's gate message and lookup call, the game's own call of `24B4920`, and the
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
    {"console.add_or_replace", core::SignatureGroup::editor_callee, 0x24B4920, "415641574883EC484C8B3A4C8BF14863420C",
     "adds or replaces console entries by name (re-evidence 2)"},
    {"console.string_assign", core::SignatureGroup::editor_callee, 0x129CA0,
     "48895C241048896C2418565741574883EC20488B6918", "MSVC std::string::assign on the game heap (9)"},
    {"console.singleton_create", core::SignatureGroup::editor_callee, 0x24B4C40,
     "48895C2410574883EC20B9E0000000E8????????", "creates the console and stores it at 35E1A80 (1.1)"},
    {"console.dispatch", core::SignatureGroup::editor_callee, 0x24B4D60,
     "48895C241848895424105556574154415541564157488BEC4883EC70498BF0", "dispatcher: gate, then lookup (3)"},
    {"console.lookup", core::SignatureGroup::editor_callee, 0x24B54C0,
     "48895C240848896C2410488974241848897C24204154415641574883EC204533F6", "lookup by name and alias (3)"},
    {"console.set_cosmetic_tag", core::SignatureGroup::editor_callee, 0x282540,
     "48895C2418488974242048894C2408574883EC30488BF2488BF9", "vanilla set_cosmetic_tag action (5)"},
    {"console.drop_cosmetic_tag", core::SignatureGroup::editor_callee, 0x253220,
     "48895C241848894C24085556574154415541564157488BEC4881EC800000000F29742470488BDA",
     "vanilla drop_cosmetic_tag action (5)"},
    {"console.eval_effect", core::SignatureGroup::editor_callee, 0x263690,
     "48895C241048894C24085556574154415541564157488DAC2400FCFFFF", "vanilla eval_effect action (9, 10)"},
    {"console.tag_parse", core::SignatureGroup::editor_callee, 0x10E10C0, "40534883EC50488BD9488D4C2430E8????????90",
     "country tag from a name string (10)"},
    {"console.country_of", core::SignatureGroup::editor_callee, 0xBB4390,
     "48895C2410574883EC30803D????????00488BD90F84????????803D????????00754F48833D????????007545488D05????????"
     "C6442440004889442428488D15????????488D44244041B10141B85C040000",
     "country object of a tag (10)"},
    {"console.country_set_cosmetic", core::SignatureGroup::editor_callee, 0x711260,
     "48895C241048896C24184889742420574883EC3048837A180F", "CCountry set cosmetic tag; +1488 (5)"},
    {"console.restore_device_objects", core::SignatureGroup::editor_callee, 0x2A2570,
     "48895C2408488974241048897C241855488D6C24A94881EC00010000", "CGameIdler::RestoreDeviceObjects (2)"},
    {"console.sprite_find", core::SignatureGroup::editor_callee, 0x2238DF0,
     "4881C100010000E9????????CCCCCCCC48895C2418", "sprite of a name in the sprite database (portrait review 1.4)"},
};

/// @brief Exact instruction bytes at an RVA (inside a function verified above).
struct InstructionPin {
    const char* id;   ///< Identifier for the log.
    uptr rva;         ///< Instruction RVA.
    const char* hex;  ///< Exact bytes.
};

/// @brief The 22 layout pins of re-evidence.md section 7, the cosmetic-tag offset and the gate order.
inline constexpr InstructionPin instruction_pins[] = {
    {"entry.name", 0x24B5509, "488d4d08"},
    {"entry.alias_count", 0x24B551D, "397d28"},
    {"entry.aliases", 0x24B5522, "488d7530"},
    {"entry.stride", 0x24B554D, "4981c7c8010000"},
    {"console.entry_count", 0x24B54E7, "4439710c"},
    {"entry.flag", 0x24B4E61, "803800"},
    {"entry.action_present", 0x24B4E81, "49837c246800"},
    {"entry.action", 0x24B4F8A, "498b442468"},
    {"entry.context", 0x24B4F8F, "4d8b442478"},
    {"argv.count", 0x24B4E9F, "8b460c"},
    {"result.message", 0x24B4FA4, "498d7f08"},
    {"console.gate_a", 0x24B4DB7, "488b4950"},
    {"console.gate_c", 0x24B4DCE, "498b8dd0000000"},
    {"console.gate_b", 0x24B4DE8, "498b8d90000000"},
    {"entry.alias_count_assign", 0x24B4AD3, "894628"},
    {"entry.description", 0x24B4ACF, "488d4e48"},
    {"entry.action_assign", 0x24B4B15, "48894668"},
    {"entry.context_assign", 0x24B4B2A, "48894678"},
    {"entry.arg_name_count", 0x24B4B31, "898680000000"},
    {"entry.arg_name_slots", 0x24B4B19, "bd0a000000"},
    {"console.allocator", 0x24B49E4, "498b4e10"},
    {"entry.size", 0x24B4A07, "4869d0c8010000"},
    {"country.cosmetic_tag", 0x711279, "488db188140000"},
    {"set_cosmetic_tag.argc", 0x282580, "837a0c02"},
    {"drop_cosmetic_tag.argc", 0x253274, "837a0c01"},
};

/// @brief RIP and rel32 targets (displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"console.singleton_store", 0x24B4CE7, 0x24B4CEB, layout::console_slot_rva},
    {"console.singleton_store2", 0x24B4CFE, 0x24B4D02, layout::console_slot_rva},
    {"set_cosmetic_tag.assign", 0x2825E5, 0x2825E9, layout::string_assign_rva},
    {"set_cosmetic_tag.tag_parse", 0x28258F, 0x282593, layout::tag_parse_rva},
    {"set_cosmetic_tag.country_of", 0x2825A3, 0x2825A7, layout::country_of_rva},
    {"set_cosmetic_tag.setter", 0x2825AF, 0x2825B3, 0x711260},
    {"dispatch.gate_message", 0x24B4E0F, 0x24B4E13, 0x2B91060},
    {"dispatch.lookup_after_gate", 0x24B4E51, 0x24B4E55, 0x24B54C0},
    {"restore_device_objects.add", 0x2A274F, 0x2A2753, layout::add_or_replace_rva},
    {"portrait_valid.sprite_database", 0x139D214, 0x139D218, layout::sprite_database_rva},
    {"portrait_valid.sprite_find", 0x139D219, 0x139D21D, layout::sprite_find_rva},
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
 * @brief Checks one instruction pin.
 * @param image Image base.
 * @param pin Pin.
 * @return `true` when the bytes at `image + rva` equal the pin.
 */
inline bool verify_instruction(uptr image, const InstructionPin& pin) noexcept {
    std::array<unsigned char, core::max_pattern_bytes> expected{};
    const auto size = core::decode_hex(pin.hex, expected.data(), expected.size());
    std::array<unsigned char, core::max_pattern_bytes> actual{};
    return size != 0 && core::ProcessMemory::read(image + pin.rva, actual.data(), size) &&
           std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(size), actual.begin());
}

/**
 * @brief Checks every console signature, instruction pin and target pin (code bytes only).
 * @param text `.text` of the game image (the image base is `text.base`).
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_pin(const core::TextSection& text) noexcept {
    const auto image = reinterpret_cast<uptr>(text.base);
    const core::ProcessMemory reader{};
    for (const auto& signature : console_signatures) {
        if (!core::verify_signature(text, signature)) {
            return signature.id;
        }
    }
    for (const auto& pin : instruction_pins) {
        if (!verify_instruction(image, pin)) {
            return pin.id;
        }
    }
    for (const auto& pin : target_pins) {
        if (!core::verify_rip_pin(reader, image, pin)) {
            return pin.id;
        }
    }
    return nullptr;
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
 * @brief The action CAPIE registers for its three commands (game thread, called by `24B4D60`).
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
