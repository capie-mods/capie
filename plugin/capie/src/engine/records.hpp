/**
 * @file records.hpp
 * @brief Layout of the temp-variable records a scripted-GUI click passes to its effect.
 *
 * A click on a scripted-GUI button builds a `CExecuteScriptedWindowEffect` from the
 * window context's temp-variable vector: a list of (name, fixed-point value) records.
 * The editor bridge appends its own records to a copy of that list (see
 * `features/editor/transport.hpp`); the game's constructor deep-copies the list, so the
 * copy only has to live for the constructor call.
 *
 * This file defines the two POD layouts and a bounded key reader. It does not decide
 * which keys are reserved or what values mean; that is editor protocol.
 */
#pragma once
#include "../core/memory.hpp"
#include "msvc_string.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace capie::engine {

/// @brief One temp variable: name and fixed-point raw value.
struct Record {
    MsvcString key;          ///< Variable name.
    std::int64_t raw_value;  ///< Fixed-point value (scaled by `fixed_point` in the editor protocol).
};

/// @brief Game temp vector: data, int32 capacity, int32 count, allocator.
struct TempVector {
    Record* data;           ///< Record array.
    std::int32_t capacity;  ///< Allocated records.
    std::int32_t count;     ///< Valid records.
    void* allocator;        ///< Game allocator (copied through unchanged).
};
/// @brief Size of one game record (MSVC string + int64).
inline constexpr std::size_t record_bytes = 0x28;
/// @brief Offset of `raw_value` in a record.
inline constexpr std::size_t record_value_offset = 0x20;
/// @brief Size of the game temp vector header.
inline constexpr std::size_t vector_bytes = 0x18;
/// @brief Offset of `count` in the vector header.
inline constexpr std::size_t vector_count_offset = 0xC;
/// @brief Offset of `allocator` in the vector header.
inline constexpr std::size_t vector_allocator_offset = 0x10;
static_assert(sizeof(Record) == record_bytes && offsetof(Record, raw_value) == record_value_offset);
static_assert(sizeof(TempVector) == vector_bytes && offsetof(TempVector, count) == vector_count_offset &&
              offsetof(TempVector, allocator) == vector_allocator_offset);
static_assert(std::is_trivially_copyable_v<Record> && std::is_trivially_copyable_v<TempVector>);

/// @brief Sanity limit on a record vector capacity read from game memory.
inline constexpr std::int32_t max_vector_capacity = 65536;

/**
 * @brief Copies a record's key (terminator required, no embedded NUL).
 * @param record Copied record.
 * @param out Destination (129 bytes).
 * @return `true` on success.
 */
inline bool record_key(const Record& record, char (&out)[max_name_bytes + 1]) noexcept {
    const auto& key = record.key;
    if (key.size > max_name_bytes || key.size > key.capacity || key.capacity > max_string_capacity) {
        return false;
    }
    const void* data = key.capacity <= sso_capacity ? static_cast<const void*>(key.data.small)
                                                    : static_cast<const void*>(key.data.pointer);
    return guarded_copy(out, data, static_cast<std::size_t>(key.size) + 1) && out[key.size] == '\0' &&
           std::memchr(out, '\0', static_cast<std::size_t>(key.size)) == nullptr;
}

}  // namespace capie::engine
