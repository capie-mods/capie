/**
 * @file name_layer_pins.hpp
 * @brief Code-byte pins of the country name layer (`capie_core.dll`), checked before its hooks go live.
 *
 * The layer hooks `CCountry_UpdateNames` (`rva::update_names`), `CCountry_BuildNameKey`
 * (`rva::build_name_key`) and `CSetVariableEffect::Execute` (`rva::set_variable_execute`),
 * and reads country variables and the country list with the layout in `core/game_layout.hpp`.
 * Every hooked function and every offset it reads is pinned here: 7 unique signatures,
 * 12 instruction pins (offsets in the game's own instructions) and 6 RIP/rel32 target pins
 * (API level 5 added the faction name accessor `rva::faction_get_name`, the string copy
 * `rva::string_copy` and the faction member layout). The "report" sections cited below are
 * those of the custom-name-layer report (session 2026-10-13).
 *
 * All-or-nothing for the layer only: one failed pin turns the layer off (its three hooks are
 * not installed) and is logged; the three localisation hooks and RN1 names keep working. This
 * is the display-only exception of docs/API-DECISIONS.md (roadmap decision 2).
 *
 * Reads code bytes only (the `.text` range checked readable by `pe_image.hpp`, and the pinned
 * instructions through `core::ProcessMemory`). No allocation.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/pe_image.hpp"
#include "../../core/pin_check.hpp"
#include "../../core/signatures.hpp"

#include <cstddef>
#include <iterator>

namespace capie::country::layer::pins {

using core::uptr;

/// @brief Hooked functions and the two variable functions whose logic CAPIE reproduces.
inline constexpr core::Signature signatures[] = {
    {"name.update_names", core::SignatureGroup::country_hook, core::rva::update_names,
     "48895c24185556574154415541564157488dac2410feffff", "CCountry_UpdateNames prologue (report 1.1)"},
    {"name.build_name_key", core::SignatureGroup::country_hook, core::rva::build_name_key,
     "488bc4488958084c8948204c8940185556574154415541564157488da8e8feffff", "CCountry_BuildNameKey prologue (1.2)"},
    {"name.set_variable_execute", core::SignatureGroup::country_hook, core::rva::set_variable_execute,
     "48895c24104889742418574883ec30488bd94533c04883c158488bfae8????????488d8b28010000488bf04885c97418"
     "80b9d000000001750f4c8bc7488d542440e8????????eb1e80b9d000000000752d",
     "CSetVariableEffect::Execute: scoped variable at effect+0x58 (1.5)"},
    {"name.variable_find", core::SignatureGroup::country_hook, core::rva::variable_find,
     "48895c241848897c24204154415641574883ec30418b40084c8bf24d8b084c8bf941bbc59d1c8185c07e1d448bd06690"
     "410fb6114d8d49014133d34469da930100014983ea0175e84c63611440b701498bcc4963c34823c848896c2450488974"
     "2458488d1c4948c1e30449035f0840387b047251",
     "variable find: FNV-1a, mask +14, entries +8, stride 0x30, probe byte +4 (1.4)"},
    {"name.variable_get", core::SignatureGroup::country_hook, core::rva::variable_get,
     "48895c24104c894c2420574883ec20488d5910488bfa488bcb488d542430e8????????0fb64b188b4314ffc103c14898"
     "488d0c40488b44243048c1e10448034b08483bc174064883c028eb05488d442448488b00488b5c2438488907488bc748"
     "83c4205fc3",
     "variable get: table at container+0x10, end = (mask+extra+1)*0x30, value at entry+0x28 (1.4)"},
    {"faction.get_name", core::SignatureGroup::country_hook, core::rva::faction_get_name,
     "40534883ec3033c0488bda483941287415488d5118488bcbe8????????488bc34883c4305bc3488b89680500004885c9"
     "74154881c1d0000000e8????????488bc34883c4305bc30f57c00f1142100f11024889421048c742180f000000880248"
     "8bc34883c4305bc3",
     "CFaction::GetName (whole function): +0x18 text when +0x28 size != 0, else template +0x568 (API 5)"},
    {"faction.string_copy", core::SignatureGroup::country_hook, core::rva::string_copy,
     "48895c2408488974241048897c241841564883ec2033c00f57c00f1101",
     "MSVC string copy-construct on the game heap (called by GetName, pin faction.get_name_copy)"},
};

/// @brief Offsets the layer reads, pinned in the game's own instructions.
inline constexpr core::InstructionPin instruction_pins[] = {
    {"country.variables", 0x589A8B, "488b9818020000"},                      // mov rbx,[rax+218] after country_of
    {"country.variables2", 0x589D52, "488b9818020000"},                     // second scope path, same offset
    {"scoped_variable.name_size", 0x58C463, "8b8780000000"},                // mov eax,[rdi+80]
    {"scoped_variable.name_data", 0x58C469, "488d5770"},                    // lea rdx,[rdi+70]
    {"scoped_variable.name_capacity", 0x58C46D, "4883bf880000000f"},        // cmp qword [rdi+88],0F
    {"set_variable.scoped_variable", 0x37D1D5, "4883c158"},                 // add rcx,58 before the scope call
    {"update_names.tag_argument", 0x75EA17, "488d9788140000"},              // lea rdx,[rdi+1488] = BuildNameKey tag
    {"gamestate.countries", 0x1AF4EB, "488b9810030000"},                    // mov rbx,[rax+310]
    {"gamestate.country_count", 0x1AF4F2, "4863801c030000"},                // movsxd rax,[rax+31C]
    {"country_of.countries", 0xC07737, "488b8710030000"},                   // mov rax,[rdi+310]
    {"faction.leader_first_member", 0x4C9568, "488b47588b13488b088b4108"},  // set_faction_leader: [[f+58]]+8 tag
    {"faction.member_count", 0xCBB05F, "4d637d64"},                         // movsxd r15,[r13+64]
};

/// @brief RIP-relative and rel32 targets (displacement RVA, next instruction RVA, target RVA).
inline constexpr core::RipPin target_pins[] = {
    {"variable_get.find", 0xC1D7DF, 0xC1D7E3, core::rva::variable_find},
    {"update_names.build_name_key", 0x75EA23, 0x75EA27, core::rva::build_name_key},
    {"scope.country_of", 0x589A87, 0x589A8B, core::rva::country_of},
    {"gamestate.global", 0x1AF4E7, 0x1AF4EB, core::rva::gamestate_global},
    {"country_of.gamestate", 0xC0771D, 0xC07721, core::rva::gamestate_global},
    {"faction.get_name_copy", 0xDF6389, 0xDF638D, core::rva::string_copy},
};

/// @brief Expected table sizes (report section 1).
inline constexpr std::size_t expected_signatures = 7;
/// @brief Expected instruction pins.
inline constexpr std::size_t expected_instruction_pins = 12;
/// @brief Expected target pins.
inline constexpr std::size_t expected_target_pins = 6;
static_assert(std::size(signatures) == expected_signatures &&
              std::size(instruction_pins) == expected_instruction_pins &&
              std::size(target_pins) == expected_target_pins);

/**
 * @brief Checks every signature, instruction pin and target pin of the layer.
 * @param text `.text` of the game image (the image base is `text.base`).
 * @return Null when all pass, else the id of the first failed check.
 */
inline const char* first_failed_pin(const core::TextSection& text) noexcept {
    return core::first_failed(text, core::ProcessMemory{}, reinterpret_cast<uptr>(text.base), signatures,
                              instruction_pins, target_pins);
}

}  // namespace capie::country::layer::pins
