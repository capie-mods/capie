/**
 * @file console_layout.hpp
 * @brief Byte layouts of the game's console objects (HOI4 1.19.4.1, SHA CF781ED1...; ported in docs/research/port-1.19.4.1.md).
 *
 * Every offset is proven by one instruction pin in `console_wiring.hpp` ("re-evidence.md" is the
 * console RE report of session 2026-10-11):
 *
 *     console (singleton at image + `console_slot_rva`)
 *       +0x00 entry array data, +0x0C entry count, +0x10 allocator
 *     command entry, 0x1C8 bytes (add-or-replace assignment path, lookup)
 *       +0x00 flag (1 = usable in release), +0x08 name, +0x28 alias count,
 *       +0x30 3 alias pointers (copied raw), +0x48 description, +0x68 action,
 *       +0x70 unused, +0x78 action context, +0x80 argument-name count,
 *       +0x88 10 argument-name strings
 *     TempVector: +0x00 data, +0x08 capacity, +0x0C count, +0x10 allocator
 *     action result: +0x00 byte, +0x08 message (MSVC std::string)
 *
 * The structs mirror game memory for building CAPIE's own entries and argument vectors
 * and for reading the live array through a memory policy. They are never placed into
 * game memory by CAPIE; the game copies what it needs with its own code.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../engine/msvc_string.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace capie::console::layout {

using core::uptr;
using engine::MsvcString;

/// @brief RVA of the console singleton slot (stored by `console.singleton_create`, pins `console.singleton_store*`).
inline constexpr uptr console_slot_rva = 0x368F540;
/// @brief RVA of the add-or-replace function (`console.add_or_replace`).
inline constexpr uptr add_or_replace_rva = 0x2537250;
/// @brief RVA of MSVC `std::string::assign(const char*, size_t)` (called by `set_cosmetic_tag`).
inline constexpr uptr string_assign_rva = 0x12B3C0;
/// @brief RVA of the vanilla `set_cosmetic_tag` console action.
inline constexpr uptr set_cosmetic_tag_rva = 0x299AC0;
/// @brief RVA of the vanilla `drop_cosmetic_tag` console action.
inline constexpr uptr drop_cosmetic_tag_rva = 0x26A6E0;
/// @brief RVA of the vanilla `eval_effect` console action.
inline constexpr uptr eval_effect_rva = 0x27A8C0;
/// @brief RVA of the console command lookup (by name and alias).
inline constexpr uptr lookup_rva = 0x2537DF0;
/// @brief RVA of the `CCountry` cosmetic-tag setter the vanilla `set_cosmetic_tag` calls.
inline constexpr uptr country_set_cosmetic_rva = 0x759370;
/// @brief RVA of the dispatcher's gate message ("Console not available in multiplayer or ironman mode.").
inline constexpr uptr gate_message_rva = 0x2C03C70;

/// @brief Offset of the entry array data pointer in the console.
inline constexpr uptr console_entries = 0x00;
/// @brief Offset of the entry count in the console.
inline constexpr uptr console_entry_count = 0x0C;
/// @brief Size of one command entry.
inline constexpr std::size_t entry_size = 0x1C8;
/// @brief Number of alias slots in an entry.
inline constexpr std::size_t alias_slots = 3;
/// @brief Number of argument-name strings in an entry.
inline constexpr std::size_t arg_name_slots = 10;
/// @brief Sanity limit for the live entry count (vanilla has 343 plus module tables).
inline constexpr std::int32_t max_live_entries = 4096;
/// @brief Longest alias CAPIE reads when it checks for name collisions.
inline constexpr std::size_t max_alias_bytes = 64;

/// @brief Proven entry offsets (instruction pins in re-evidence.md section 7).
namespace entry_offset {
inline constexpr std::size_t name = 0x08;            ///< Pin `entry.name`.
inline constexpr std::size_t alias_count = 0x28;     ///< Pins `entry.alias_count`, `entry.alias_count_assign`.
inline constexpr std::size_t aliases = 0x30;         ///< Pin `entry.aliases`.
inline constexpr std::size_t description = 0x48;     ///< Pin `entry.description`.
inline constexpr std::size_t action = 0x68;          ///< Pins `entry.action`, `entry.action_assign`.
inline constexpr std::size_t context = 0x78;         ///< Pins `entry.context`, `entry.context_assign`.
inline constexpr std::size_t arg_name_count = 0x80;  ///< Pin `entry.arg_name_count`.
inline constexpr std::size_t arg_names = 0x88;       ///< Add-or-replace copies them (source + 8 + 0x80).
}  // namespace entry_offset
/// @brief Padding after a leading byte up to the next 8-byte member.
inline constexpr std::size_t byte_padding = 7;
/// @brief Size of a `TempVector` header.
inline constexpr std::size_t temp_vector_size = 0x18;
/// @brief Offset of the element count in a `TempVector` (pin `argv.count`).
inline constexpr std::size_t temp_vector_count = 0x0C;
/// @brief Size of an action result.
inline constexpr std::size_t action_result_size = 0x28;
/// @brief Offset of the message in an action result (pin `result.message`).
inline constexpr std::size_t action_result_message = 0x08;

/// @brief One command entry, byte for byte.
struct CommandEntry {
    std::uint8_t release_flag;                         ///< +0x00: 1 = usable in release builds.
    std::array<std::uint8_t, byte_padding> pad0;       ///< +0x01: padding.
    MsvcString name;                                   ///< +0x08: command name.
    std::int32_t alias_count;                          ///< +0x28: used alias slots.
    std::uint32_t pad1;                                ///< +0x2C: padding.
    std::array<uptr, alias_slots> aliases;             ///< +0x30: `const char*` aliases (copied raw).
    MsvcString description;                            ///< +0x48: help text.
    uptr action;                                       ///< +0x68: action function.
    std::uint64_t unused;                              ///< +0x70: copied, not used by the dispatcher.
    std::uint64_t context;                             ///< +0x78: third action argument.
    std::int32_t arg_name_count;                       ///< +0x80: used argument-name strings.
    std::uint32_t pad2;                                ///< +0x84: padding.
    std::array<MsvcString, arg_name_slots> arg_names;  ///< +0x88: argument names for `help`.
};
static_assert(sizeof(CommandEntry) == entry_size);
static_assert(offsetof(CommandEntry, name) == entry_offset::name &&
              offsetof(CommandEntry, alias_count) == entry_offset::alias_count &&
              offsetof(CommandEntry, aliases) == entry_offset::aliases &&
              offsetof(CommandEntry, description) == entry_offset::description &&
              offsetof(CommandEntry, action) == entry_offset::action &&
              offsetof(CommandEntry, context) == entry_offset::context &&
              offsetof(CommandEntry, arg_name_count) == entry_offset::arg_name_count &&
              offsetof(CommandEntry, arg_names) == entry_offset::arg_names);

/// @brief The engine's `TempVector<T>` header.
struct TempVector {
    uptr data;              ///< +0x00: elements.
    std::int32_t capacity;  ///< +0x08: allocated elements.
    std::int32_t count;     ///< +0x0C: used elements.
    uptr allocator;         ///< +0x10: allocator; not read for a source or argv vector.
};
static_assert(sizeof(TempVector) == temp_vector_size && offsetof(TempVector, count) == temp_vector_count);

/// @brief Result an action constructs in the dispatcher's storage.
struct ActionResult {
    std::uint8_t flag;                           ///< +0x00: stored as console+0xD8; meaning OPEN, vanilla writes 0.
    std::array<std::uint8_t, byte_padding> pad;  ///< +0x01: padding.
    MsvcString message;                          ///< +0x08: text the console prints.
};
static_assert(sizeof(ActionResult) == action_result_size && offsetof(ActionResult, message) == action_result_message);

/// @brief A console action (`Result* fn(Result* out, const TempVector<std::string>* argv, u64 ctx)`).
using ActionFn = ActionResult* (*)(ActionResult* out, const TempVector* argv, std::uint64_t context);
/// @brief `add_or_replace(console, source)`: appends new names, replaces existing ones.
using AddOrReplaceFn = void (*)(void* console, const TempVector* source);
/// @brief `assign(string, bytes, size)`: MSVC `std::string::assign` on the game's heap.
using AssignFn = MsvcString* (*)(MsvcString* self, const char* bytes, std::size_t size);
/// @brief `tag_parse(out_tag, name)`: country tag from a name string (copies the name; read only).
using TagParseFn = void* (*)(void* out_tag, const MsvcString* name);
/// @brief `country_of(tag)`: country object of a tag (array lookup in the game state; read only).
using CountryOfFn = void* (*)(const void* tag);
/// @brief `sprite_find(database, name)`: sprite of a name, null when it does not exist (read only).
using SpriteFindFn = void* (*)(void* database, const MsvcString* name);
/// @brief Id of the calling thread.
using ThreadIdFn = std::uint32_t (*)();
/// @brief Writes one NUL-terminated log line.
using LogFn = void (*)(const char* line);

/// @brief RVA of the sprite lookup (called by the portrait validity check).
inline constexpr uptr sprite_find_rva = core::rva::sprite_find;
/// @brief RVA of the global sprite database pointer (read by the portrait validity check).
inline constexpr uptr sprite_database_rva = core::rva::sprite_database_global;
/// @brief RVA of the tag parser called by `set_cosmetic_tag`.
inline constexpr uptr tag_parse_rva = 0x1168A70;
/// @brief RVA of the country lookup called by `set_cosmetic_tag`.
inline constexpr uptr country_of_rva = core::rva::country_of;
/// @brief Offset of the cosmetic tag string in a country (pin `country.cosmetic_tag`: `lea rsi,[rcx+1488]`).
inline constexpr uptr country_cosmetic_tag = core::off::country_cosmetic_tag;

/**
 * @brief Writes an empty MSVC string, as the vanilla `set_cosmetic_tag` does for its result.
 * @param target String storage (uninitialised or an empty SSO string).
 */
inline void construct_empty(MsvcString& target) noexcept {
    target = MsvcString{};
    target.capacity = engine::sso_capacity;
}

}  // namespace capie::console::layout
