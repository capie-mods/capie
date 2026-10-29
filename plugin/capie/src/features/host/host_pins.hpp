/**
 * @file host_pins.hpp
 * @brief Pins of the host probe (`host_probe.hpp`), checked once at editor start in the debug flavor.
 *
 * "report" below is the host-detect report (session 2026-10-14); the current values are in
 * docs/research/port-1.19.4.1.md. `GetMachineId` keeps one leading padding byte so its pattern
 * is unique (`CC8B8184000000C3`). None of the pinned ranges overlaps a DIR64 relocation
 * (checked on the 1.19.4.1 file by tools/port/gen_tables.py).
 *
 * A failed pin turns the probe off (one log line naming the pin); the editor keeps working.
 */
#pragma once
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"
#include "host_probe.hpp"

#include <cstddef>
#include <iterator>

namespace capie::host::pins {

/// @brief Functions whose bytes establish the chain (report 3, "Sygnatury").
inline constexpr core::Signature signatures[] = {
    {"host.get_local_human", core::SignatureGroup::editor_callee, 0x1F1A40,
     "40534883EC20488BD9488B0D????????488B01FF9088000000488BC8E8????????48638B04010000",
     "CGameState::GetLocalHuman: session vslot 0x88, humans +0x104 (report 1.4)"},
    {"host.get_session", core::SignatureGroup::editor_callee, layout::get_session, "488B8180030000C3",
     "CGameApplication::GetSession: mov rax,[rcx+380h] (report 1.1)"},
    {"host.get_machine_id", core::SignatureGroup::editor_callee, 0x148D9AF, "CC8B8184000000C3",
     "CSession::GetMachineId: mov eax,[rcx+84h] (port 1.19.4.0; padding byte for uniqueness)"},
    {"host.session_init", core::SignatureGroup::editor_callee, 0x22C3AB0, "40534883EC204889742438418D40FF33F6",
     "CSession::Init prologue: is_server = (mode-1) <= 1 (report 1.2)"},
    {"host.is_from_host", core::SignatureGroup::editor_callee, 0x22D8E90, "83790C010F94C0C3",
     "host-only command check: machine id 1 is the host (report 1.4)"},
};

/// @brief Offsets read by the probe, pinned in the game's own instructions.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"session.is_server", 0x22C3AE4, "88413C"},                      // mov [rcx+3C],al
    {"session.mode", 0x22C3AEA, "44894134"},                         // mov [rcx+34],r8d
    {"session.machine_id_1", 0x22C3AF2, "C7818400000001000000"},     // mov dword [rcx+84],1
    {"session.backend", 0x22C3B06, "48895140"},                      // mov [rcx+40],rdx
    {"lobby.session_is_server", 0x1D638CF, "FF908800000080783C00"},  // call [rax+88]; cmp byte [rax+3C],0
    {"gamestate.human_count", 0x1F1A61, "48638B04010000"},           // movsxd rcx,[rbx+104]
    {"gamestate.humans", 0x1F1A6F, "4C8B93F8000000"},                // mov r10,[rbx+F8]
    {"human.machine_id", 0x1F1A7C, "498D8A98000000"},                // lea rcx,[r10+98]
    {"human.stride", 0x1F1A8D, "4881C1A0000000"},                    // add rcx,0A0
    {"human.name", 0x1A4169B, "488D5320"},                           // lea rdx,[rbx+20] (USER_NAME)
    {"human.ctor_machine_id", 0xC101D3, "41898698000000"},           // mov [r14+98],eax
};

/// @brief RIP targets: the application global read by GetApplication.
inline constexpr core::RipPin target_pins[] = {
    {"app.global", 0x19A6223, 0x19A6227, layout::app_global},
};

/// @brief Vtable slots: GetSession of the application, idler slot 0x88 (report 1.1).
inline constexpr core::NamedSlotPin slot_pins[] = {
    {"app.get_session_slot", {layout::vt_game_application, layout::slot_get_session, layout::get_session}},
    {"ingame_idler.session_slot", {core::rva::vt_ingame_idler, layout::slot_get_session, layout::idler_get_session}},
};

/// @brief Expected signatures.
inline constexpr std::size_t expected_signatures = 5;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 11;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 1;
/// @brief Expected slot pins.
inline constexpr std::size_t expected_slot_pins = 2;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins && std::size(slot_pins) == expected_slot_pins);

/**
 * @brief Checks every signature and pin of the probe.
 * @tparam M Memory policy (data reads: instruction bytes, RIP displacements, vtable slots).
 * @param text `.text` of the game image (signatures).
 * @param mem Memory policy.
 * @param image Image base the vtable slots are relocated to.
 * @return Null when all pass, else the id of the first failed check.
 */
template <class M>
const char* first_failed_pin(const core::TextSection& text, const M& mem, uptr image) noexcept {
    return core::first_failed(text, mem, image, signatures, instruction_pins, target_pins, slot_pins);
}

}  // namespace capie::host::pins
