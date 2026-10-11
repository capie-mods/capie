/**
 * @file signatures.hpp
 * @brief Types and matching rules for the signature table.
 *
 * A signature is a short run of code bytes that must occur exactly once in `.text`, and
 * at exactly the expected RVA. CAPIE checks every signature before it installs a hook or
 * calls a game function; one miss refuses initialization. The data itself lives in
 * `signature_table.hpp`; this file only defines what an entry is and how it is matched.
 *
 * Pattern syntax: two hex digits per byte, `??` for a wildcard byte (used only by the
 * three localisation hooks, whose RIP displacements are relocated per build). Patterns
 * are decoded at initialization into a stack buffer; nothing here allocates.
 *
 * The same entries are meant to drive the future runtime scanner (decision 10(a), RE
 * findings section 6.4): the scanner would find each entry by an anchor, then require the
 * bytes here (plus whole-function hashes) before enabling anything. That scanner is not
 * implemented in this stage; see `scanner.hpp`.
 *
 * Contracts: matching reads only the `.text` range located by `pe_image.hpp`, which was
 * checked readable as a whole. Cost is one linear pass over `.text` per signature, at
 * initialization only.
 */
#pragma once
#include "memory.hpp"
#include "pe_image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::core {

/// @brief Longest pattern the decoder accepts (bytes); the longest table entry has 78.
inline constexpr std::size_t max_pattern_bytes = 128;

/// @brief Which part of CAPIE relies on a signature; used for grouping and for error codes.
enum class SignatureGroup : unsigned {
    country_hook,   ///< Localisation lookup hooked by `capie_core.dll` (code 20 on mismatch).
    editor_hook,    ///< Function hooked by `capie_editor.dll` (code 20).
    editor_callee,  ///< Game function called by `capie_editor.dll` (code 20).
    caption,        ///< Caption refresh chain evidence (code 20).
    focus,          ///< Focus release chain evidence (code 22).
};

/// @brief One byte signature: `hex` must match exactly once in `.text`, at `rva`.
struct Signature {
    const char* id;        ///< Stable identifier, e.g. "editor.tick"; used by tests and logs.
    SignatureGroup group;  ///< Owner and error-code class.
    uptr rva;              ///< Expected RVA of the first byte.
    const char* hex;       ///< Pattern: hex byte pairs, `??` = wildcard.
    const char* proves;    ///< What the bytes establish (RE evidence, one line).
};

/// @brief A vtable slot that must point to a given function (both as RVAs).
struct SlotPin {
    uptr vtable;  ///< Vtable RVA in `.rdata`.
    uptr slot;    ///< Byte offset of the slot inside the vtable.
    uptr target;  ///< Expected function RVA stored in the slot.
};

/// @brief A 32-bit data word in the image that must hold a known value.
struct TokenPin {
    const char* id;        ///< Identifier for tests and messages.
    uptr rva;              ///< RVA of the DWORD.
    std::uint32_t value;   ///< Expected value.
};

/// @brief A RIP-relative operand whose target must be a known RVA.
struct RipPin {
    const char* id;          ///< Identifier for tests and messages.
    uptr displacement_rva;   ///< RVA of the signed 32-bit displacement.
    uptr next_instruction;   ///< RVA of the next instruction (RIP base of the operand).
    uptr target;             ///< Expected target RVA.
};

/// @brief Decoded pattern: bytes plus a mask (`true` = byte must match).
struct Pattern {
    std::array<unsigned char, max_pattern_bytes> bytes{};  ///< Expected bytes.
    std::array<bool, max_pattern_bytes> exact{};           ///< `false` for `??` wildcards.
    std::size_t size{};                                    ///< Decoded length; zero = invalid.
};

/**
 * @brief Value of one hex digit.
 * @param c Character.
 * @return 0..15, or -1 for a non-hex character.
 */
constexpr int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Decodes a plain hex string (no wildcards) into bytes.
 * @param hex NUL-terminated hex string.
 * @param out Destination buffer.
 * @param cap Capacity of `out`.
 * @return Byte count, or 0 on odd length, bad digit, empty input or overflow.
 */
inline std::size_t decode_hex(const char* hex, unsigned char* out, std::size_t cap) noexcept {
    const auto length = std::strlen(hex);
    if (length == 0 || length % 2 != 0 || length / 2 > cap) {
        return 0;
    }
    for (std::size_t i = 0; i < length / 2; ++i) {
        const int hi = hex_nibble(hex[2 * i]);
        const int lo = hex_nibble(hex[(2 * i) + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out[i] = static_cast<unsigned char>((hi * 16) + lo);
    }
    return length / 2;
}

/**
 * @brief Decodes a pattern that may contain `??` wildcards.
 * @param hex NUL-terminated pattern.
 * @param out Receives bytes and mask; `out.size == 0` on error.
 * @return `true` when the pattern is valid and non-empty.
 */
inline bool decode_pattern(const char* hex, Pattern& out) noexcept {
    out = Pattern{};
    const auto length = std::strlen(hex);
    if (length == 0 || length % 2 != 0 || length / 2 > max_pattern_bytes) {
        return false;
    }
    for (std::size_t i = 0; i < length / 2; ++i) {
        const char a = hex[2 * i];
        const char b = hex[(2 * i) + 1];
        if (a == '?' && b == '?') {
            continue;  // wildcard: exact[i] stays false
        }
        const int hi = hex_nibble(a);
        const int lo = hex_nibble(b);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.bytes[i] = static_cast<unsigned char>((hi * 16) + lo);
        out.exact[i] = true;
    }
    out.size = length / 2;
    return true;
}

/**
 * @brief Tests whether `pattern` matches `data` (respecting wildcards).
 * @param data First candidate byte.
 * @param pattern Decoded pattern.
 * @return `true` on a full match.
 */
inline bool matches_at(const unsigned char* data, const Pattern& pattern) noexcept {
    for (std::size_t i = 0; i < pattern.size; ++i) {
        if (pattern.exact[i] && data[i] != pattern.bytes[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Requires exactly one match of `pattern` in `.text`, located at `rva`.
 * @param text `.text` location (readable as a whole).
 * @param rva Expected RVA.
 * @param pattern Decoded pattern (size > 0).
 * @return `true` when the only match is at `rva`. Any match elsewhere, a second match or
 *         no match returns `false`.
 */
inline bool unique_at(const TextSection& text, uptr rva, const Pattern& pattern) noexcept {
    if (pattern.size == 0 || rva < text.rva || rva - text.rva + pattern.size > text.size) {
        return false;
    }
    const unsigned char* data = text.base + text.rva;
    std::size_t count = 0;
    for (std::size_t i = 0; i + pattern.size <= text.size; ++i) {
        if (pattern.exact[0] && data[i] != pattern.bytes[0]) {
            continue;  // cheap first-byte filter; identical result
        }
        if (matches_at(data + i, pattern)) {
            if (i + text.rva != rva || ++count > 1) {
                return false;
            }
        }
    }
    return count == 1;
}

/**
 * @brief Decodes and checks one signature entry.
 * @param text `.text` location.
 * @param signature Table entry.
 * @return `true` when unique at its RVA.
 */
inline bool verify_signature(const TextSection& text, const Signature& signature) noexcept {
    Pattern pattern{};
    return decode_pattern(signature.hex, pattern) && unique_at(text, signature.rva, pattern);
}

/**
 * @brief Checks every signature of a table.
 * @tparam N Table size.
 * @param text `.text` location.
 * @param table Signature table (one consumer group).
 * @return `true` when every entry is unique at its RVA.
 */
template <std::size_t N> bool verify_all(const TextSection& text, const Signature (&table)[N]) noexcept {
    for (const auto& signature : table) {
        if (!verify_signature(text, signature)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Checks that a vtable slot points to the expected function.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param pin Slot pin.
 * @return `true` when `[image + vtable + slot] == image + target`.
 */
template <class M> bool verify_slot_pin(const M& mem, uptr image, const SlotPin& pin) noexcept {
    uptr slot = 0;
    return rd(mem, image + pin.vtable, pin.slot, slot) && slot == image + pin.target;
}

/**
 * @brief Checks a 32-bit token in the image.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param pin Token pin.
 * @return `true` when the DWORD equals the expected value.
 */
template <class M> bool verify_token_pin(const M& mem, uptr image, const TokenPin& pin) noexcept {
    std::uint32_t value = 0;
    return rd(mem, image, pin.rva, value) && value == pin.value;
}

/**
 * @brief Checks the target of a RIP-relative operand.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param pin RIP pin.
 * @return `true` when `next_instruction + displacement == target`.
 */
template <class M> bool verify_rip_pin(const M& mem, uptr image, const RipPin& pin) noexcept {
    std::int32_t displacement = 0;
    return rd(mem, image, pin.displacement_rva, displacement) &&
           static_cast<long long>(pin.next_instruction) + displacement == static_cast<long long>(pin.target);
}

}  // namespace capie::core
