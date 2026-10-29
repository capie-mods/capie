/**
 * @file rtti.hpp
 * @brief Runtime check that every vtable constant belongs to the class CAPIE expects (MSVC RTTI).
 *
 * The vtable RVAs in `game_layout.hpp` are used as equality guards ("this object is a CEditBox").
 * Some of them were found by class name and position during a port, not by a constructor that
 * stores them. Every MSVC vtable is preceded by a pointer to its complete object locator (COL):
 *
 *   vtable[-1] -> COL { u32 signature = 1; u32 offset; u32 cd_offset; i32 type_descriptor_rva;
 *                       i32 class_descriptor_rva; i32 self_rva }
 *   type descriptor: { void* vftable; void* spare; char name[] }   (name at +0x10, ".?AVCEditBox@@")
 *
 * `vtable_is_class` follows that chain through a memory policy (guarded reads in the game, the
 * mapped EXE file in tests) and compares the decorated class name and the subobject offset. A
 * mismatch means the constant points at another class: the dependent feature must stay off.
 *
 * Data and pure reads only.
 */
#pragma once
#include "game_layout.hpp"
#include "memory.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::core {

/// @brief One expected vtable: RVA, decorated class name and subobject offset (0 = primary).
struct VtableClass {
    const char* id;           ///< Identifier for the log.
    uptr vtable_rva;          ///< Vtable RVA (`rva::vt_*`).
    const char* mangled;      ///< Decorated name, e.g. `.?AVCEditBox@@`.
    std::uint32_t offset;     ///< Offset of the subobject this vtable belongs to.
};

/// @brief Longest decorated name compared (the longest table entry has 35 characters).
inline constexpr std::size_t max_rtti_name = 64;

/// @brief Offset of the name inside an MSVC x64 type descriptor.
inline constexpr uptr type_descriptor_name = 0x10;

/// @brief MSVC x64 complete object locator (signature 1: image-relative fields).
struct CompleteObjectLocator {
    std::uint32_t signature;             ///< 1 on x64.
    std::uint32_t offset;                ///< Offset of this vtable's subobject in the complete object.
    std::uint32_t cd_offset;             ///< Constructor displacement offset.
    std::int32_t type_descriptor_rva;    ///< RVA of the type descriptor.
    std::int32_t class_descriptor_rva;   ///< RVA of the class hierarchy descriptor.
    std::int32_t self_rva;               ///< RVA of this locator.
};

/**
 * @brief Checks that the vtable at `image + entry.vtable_rva` belongs to `entry.mangled`.
 * @tparam M Memory policy with `bool read(uptr, void*, size_t) const`.
 * @param mem Memory policy.
 * @param image Image base the RVAs are relative to (the preferred base in file tests).
 * @param entry Expected class.
 * @return `true` when the locator chain is consistent and the name and offset match.
 */
template <class M>
bool vtable_is_class(const M& mem, uptr image, const VtableClass& entry) noexcept {
    if (entry.vtable_rva < sizeof(uptr) || image > uptr_max - entry.vtable_rva) {
        return false;
    }
    uptr locator = 0;
    if (!mem.read(image + entry.vtable_rva - sizeof(uptr), &locator, sizeof(locator)) || locator <= image) {
        return false;
    }
    CompleteObjectLocator col{};
    if (!mem.read(locator, &col, sizeof(col)) || col.signature != 1 ||
        static_cast<uptr>(static_cast<std::uint32_t>(col.self_rva)) != locator - image || col.offset != entry.offset ||
        col.type_descriptor_rva <= 0) {
        return false;
    }
    const std::size_t length = std::strlen(entry.mangled);
    if (length == 0 || length >= max_rtti_name) {
        return false;
    }
    char name[max_rtti_name]{};
    const uptr name_address = image + static_cast<uptr>(col.type_descriptor_rva) + type_descriptor_name;
    return mem.read(name_address, name, length + 1) && std::memcmp(name, entry.mangled, length + 1) == 0;
}

/**
 * @brief Returns the first entry of `table` that fails `vtable_is_class`.
 * @tparam M Memory policy.
 * @tparam N Table size.
 * @param mem Memory policy.
 * @param image Image base.
 * @param table Expected classes.
 * @return The failing entry's id, or `nullptr` when all match.
 */
template <class M, std::size_t N>
const char* first_wrong_class(const M& mem, uptr image, const VtableClass (&table)[N]) noexcept {
    for (const auto& entry : table) {
        if (!vtable_is_class(mem, image, entry)) {
            return entry.id;
        }
    }
    return nullptr;
}

/// @brief Vtables the editor DLL compares objects against (editor, caption and focus chains).
inline constexpr VtableClass editor_vtable_classes[] = {
    {"vt_edit", rva::vt_edit, ".?AVCEditBox@@", 0},
    {"vt_text_buffer", rva::vt_text_buffer, ".?AVCEditBox@@", 0x80},
    {"vt_instant_text", rva::vt_instant_text, ".?AVCInstantTextBox@@", 0},
    {"vt_edit_definition", rva::vt_edit_definition, ".?AVCEditBoxType@@", 0},
    {"vt_execute_effect", rva::vt_execute_effect, ".?AVCExecuteScriptedWindowEffect@@", 0},
    {"vt_sg_model", rva::vt_sg_model, ".?AVCScriptedWindowTemplate@@", 0},
    {"vt_sg_manager", rva::vt_sg_manager, ".?AVCScriptedWindowManager@@", 0},
    {"vt_sg_context", rva::vt_sg_context, ".?AVCScriptedWindowGUIUpdater@@", 0},
    {"vt_sg_root", rva::vt_sg_root, ".?AVCContainerWindow@@", 0},
    {"vt_politics_view", rva::vt_politics_view, ".?AVCCountryPoliticsView@@", 0},
    {"vt_politics_observer", rva::vt_politics_observer, ".?AVCCountryPoliticsView@@", 0x30},
    {"vt_ingame_idler", rva::vt_ingame_idler, ".?AVCInGameIdler@@", 0},
};

/// @brief Vtables the core DLL's portrait layer compares objects against.
inline constexpr VtableClass core_vtable_classes[] = {
    {"vt_character", rva::vt_character, ".?AVCCharacter@@", 0},
};

/// @brief Vtables the colour layer writes into its block or compares objects against.
inline constexpr VtableClass colour_vtable_classes[] = {
    {"vt_country_colors", rva::vt_country_colors, ".?AVCCountryColors@@", 0},
    {"vt_color", rva::vt_color, ".?AVCColor@@", 0},
    {"vt_country", rva::vt_country, ".?AVCCountry@@", 0},
    {"vt_ingame_idler", rva::vt_ingame_idler, ".?AVCInGameIdler@@", 0},
};

/// @brief Vtables the ruling-party ideology name layer compares objects against.
inline constexpr VtableClass subideology_vtable_classes[] = {
    {"vt_ideology_group", rva::vt_ideology_group, ".?AVCIdeologyGroup@@", 0},
    {"vt_political_party", rva::vt_political_party, ".?AVCPoliticalParty@@", 0},
    {"vt_ingame_idler", rva::vt_ingame_idler, ".?AVCInGameIdler@@", 0},
    {"vt_character", rva::vt_character, ".?AVCCharacter@@", 0},
};

}  // namespace capie::core
