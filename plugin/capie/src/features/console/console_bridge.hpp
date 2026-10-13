/**
 * @file console_bridge.hpp
 * @brief Registers CAPIE's console commands with the game and routes them to vanilla commands.
 *
 * This is the only console code that touches the game. It does three things:
 *
 * 1. **Register (once).** On the game thread (the editor DLL's Tick, after the original
 *    returns), it reads the console singleton and its command array through a memory
 *    policy, refuses when any CAPIE name already exists (the game's add function would
 *    *replace* that entry), checks that the vanilla delegates are present with their
 *    expected action addresses, and calls the game's add-or-replace function once with
 *    three CAPIE entries. It then reads the array again and requires every CAPIE name to
 *    point at CAPIE's action. Entries have no aliases (the game copies alias pointers raw).
 * 2. **Run.** The game's dispatcher calls `console_action` only after its own
 *    multiplayer/ironman gate and lookup (`24B4D60`). CAPIE reads the tokens, plans the
 *    command (`console_commands.hpp`), and either writes a message with the game's string
 *    assign or calls the planned vanilla action, found again by name, with the
 *    dispatcher's own result storage.
 * 3. **Nothing else.** No hooks, no patches, no command posting, no writes to game memory
 *    other than the result storage the dispatcher hands to every action.
 *
 * Inactive by default: `CAPIE_CONSOLE_CANDIDATE` must be defined to 1, and the caller must
 * pass a `NativeApi` whose `verified` flag says the signature checks of
 * `re-evidence.md` section 7 passed. Not wired into any DLL in this candidate.
 *
 * Threading: registration and actions run on the game thread that runs Tick and the
 * console (open item O1). The bridge keeps no lock; it must not be shared across threads.
 */
#pragma once
#include "../../core/memory.hpp"
#include "../../engine/msvc_string.hpp"
#include "console_commands.hpp"
#include "console_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#ifndef CAPIE_CONSOLE_CANDIDATE
#define CAPIE_CONSOLE_CANDIDATE 0
#endif

namespace capie::console {

using core::uptr;

/// @brief `true` only in builds that opt into the console candidate.
inline constexpr bool candidate_enabled = CAPIE_CONSOLE_CANDIDATE != 0;
/// @brief Marker in the high half of an entry context: "this is a CAPIE entry".
inline constexpr std::uint64_t context_magic = 0xCA91E00000000000ULL;
/// @brief Mask of the command id in an entry context.
inline constexpr std::uint64_t context_id_mask = 0xFFU;
/// @brief Most tokens CAPIE reads from one console line.
inline constexpr std::int32_t max_tokens = 64;

/// @brief Game addresses the bridge needs; built by the caller from the verified image.
struct NativeApi {
    uptr console_slot{};                      ///< Address of the console singleton pointer.
    layout::AddOrReplaceFn add_or_replace{};  ///< `24B4920`.
    layout::AssignFn assign{};                ///< `129CA0`.
    uptr set_cosmetic_tag{};                  ///< Expected action of `set_cosmetic_tag`.
    uptr drop_cosmetic_tag{};                 ///< Expected action of `drop_cosmetic_tag`.
    uptr eval_effect{};                       ///< Expected action of `eval_effect`.
    layout::TagParseFn tag_parse{};           ///< `10E10C0`: country tag from a name (read only).
    layout::CountryOfFn country_of{};         ///< `BB4390`: country object of a tag (read only).
    layout::ThreadIdFn current_thread{};      ///< `GetCurrentThreadId` in the game; a fake in tests.
    layout::LogFn log{};                      ///< One log line (may be null).
    bool verified{};                          ///< Signature, hash and layout checks passed.
};

/// @brief Registration state.
enum class Registration {
    disabled,          ///< Candidate not compiled in, or the API was not verified.
    waiting,           ///< Console not created yet; try again later.
    registered,        ///< Entries added and read back.
    name_collision,    ///< A CAPIE name already exists; nothing was added.
    missing_delegate,  ///< A vanilla delegate is absent or points elsewhere; nothing was added.
    unreadable,        ///< The console or its array could not be read; nothing was added.
    readback_failed,   ///< Added, but the read-back did not find CAPIE's actions (log and stop).
};

/**
 * @brief Log name of a registration state.
 * @param state State.
 * @return Short name.
 */
constexpr const char* registration_name(Registration state) noexcept {
    switch (state) {
        case Registration::disabled:
            return "disabled";
        case Registration::waiting:
            return "waiting";
        case Registration::registered:
            return "registered";
        case Registration::name_collision:
            return "name_collision";
        case Registration::missing_delegate:
            return "missing_delegate";
        case Registration::unreadable:
            return "unreadable";
        case Registration::readback_failed:
            return "readback_failed";
    }
    return "unknown";
}

namespace detail {

/**
 * @brief ASCII case-insensitive comparison (the game's lookup `25529C8` ignores case).
 * @param a First.
 * @param b Second.
 * @return `true` when equal ignoring ASCII case.
 */
constexpr bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    constexpr char case_bit = 0x20;
    for (std::size_t index = 0; index < a.size(); ++index) {
        char x = a[index];
        char y = b[index];
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x | case_bit);
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y | case_bit);
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

/// @brief Bytes of one string read from game memory.
template <std::size_t Capacity>
struct ReadText {
    std::array<char, Capacity + 1> bytes{};  ///< Bytes.
    std::size_t size{};                      ///< Length.
    /// @brief The bytes. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/**
 * @brief Reads an MSVC string through a memory policy (bounded, no terminator needed).
 * @tparam Capacity Largest accepted length.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param address Address of the `MsvcString`.
 * @param out Receives the bytes.
 * @return `false` when unreadable or longer than `Capacity`.
 */
template <std::size_t Capacity, class M>
bool read_string(const M& mem, uptr address, ReadText<Capacity>& out) noexcept {
    engine::MsvcString raw{};
    if (address == 0 || !mem.read(address, &raw, sizeof(raw)) || raw.size > Capacity || raw.size > raw.capacity ||
        raw.capacity > engine::max_string_capacity) {
        return false;
    }
    out.size = static_cast<std::size_t>(raw.size);
    if (raw.capacity <= engine::sso_capacity) {
        for (std::size_t index = 0; index < out.size; ++index) {
            out.bytes[index] = static_cast<char>(raw.data.small[index]);
        }
    } else if (out.size != 0 && !mem.read(reinterpret_cast<uptr>(raw.data.pointer), out.bytes.data(), out.size)) {
        return false;
    }
    out.bytes[out.size] = '\0';
    return true;
}

/**
 * @brief Reads a NUL-terminated alias of at most `layout::max_alias_bytes` bytes.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param address Alias address.
 * @param out Receives the bytes.
 * @return `false` when unreadable or unterminated within the limit.
 */
template <class M>
bool read_alias(const M& mem, uptr address, ReadText<layout::max_alias_bytes>& out) noexcept {
    out = {};
    for (std::size_t index = 0; index < layout::max_alias_bytes; ++index) {
        char c = 0;
        if (!mem.read(address + index, &c, 1)) {
            return false;
        }
        if (c == '\0') {
            out.size = index;
            return true;
        }
        out.bytes[index] = c;
    }
    return false;
}

}  // namespace detail

/// @brief One live entry as CAPIE needs it.
struct LiveEntry {
    uptr action{};            ///< Action address.
    std::uint64_t context{};  ///< Action context.
};

/**
 * @brief The bridge. One instance per process, owned by the DLL that runs Tick.
 * @tparam M Memory policy (`core::ProcessMemory` in the game, a fake in tests).
 */
template <class M>
class ConsoleBridge {
public:
    /**
     * @brief Creates an idle bridge.
     * @param mem Memory policy.
     * @param api Game addresses; ignored unless `verified`.
     * @param action Address of the action CAPIE registers (the DLL's `console_action`).
     */
    ConsoleBridge(const M& mem, const NativeApi& api, uptr action) noexcept : mem_(mem), api_(api), action_(action) {}

    /**
     * @brief Sets the game addresses and the action before the first registration attempt.
     * @param api Game addresses.
     * @param action Address of the registered action.
     * @note Ignored once registration has started or ended, so a live entry never changes.
     */
    void configure(const NativeApi& api, uptr action) noexcept {
        if (state_ == Registration::waiting && registration_thread_ == 0) {
            api_ = api;
            action_ = action;
        }
    }

    /// @brief Current state. @return State.
    [[nodiscard]] Registration state() const noexcept { return state_; }

    /**
     * @brief Registers the three commands once. Call after the original Tick on the game thread.
     * @return New state; `waiting` means "call again on a later Tick".
     */
    Registration try_register() noexcept {
        if (state_ != Registration::waiting) {
            return state_;
        }
        if constexpr (!candidate_enabled) {
            return state_ = Registration::disabled;
        }
        if (!api_.verified || api_.add_or_replace == nullptr || api_.assign == nullptr || api_.tag_parse == nullptr ||
            api_.country_of == nullptr || api_.current_thread == nullptr || action_ == 0) {
            return state_ = Registration::disabled;
        }
        registration_thread_ = api_.current_thread();
        uptr console = 0;
        if (!mem_.read(api_.console_slot, &console, sizeof(console))) {
            return state_ = Registration::unreadable;
        }
        if (console == 0) {
            return state_;  // waiting
        }
        bool collision = false;
        if (!scan_names(console, collision)) {
            return state_ = Registration::unreadable;
        }
        if (collision) {
            return state_ = Registration::name_collision;
        }
        for (const auto delegate : {Delegate::set_cosmetic_tag, Delegate::drop_cosmetic_tag, Delegate::eval_effect}) {
            LiveEntry entry{};
            if (!find_delegate(console, delegate, entry)) {
                return state_ = Registration::missing_delegate;
            }
        }
        build_entries();
        const layout::TempVector source{reinterpret_cast<uptr>(entries_.data()),
                                        static_cast<std::int32_t>(command_count),
                                        static_cast<std::int32_t>(command_count), 0};
        api_.add_or_replace(reinterpret_cast<void*>(console), &source);
        for (const auto& command : command_specs) {
            LiveEntry entry{};
            if (!find_entry(console, command.name, entry) || entry.action != action_ ||
                entry.context != context_of(command.id)) {
                return state_ = Registration::readback_failed;
            }
        }
        return state_ = Registration::registered;
    }

    /**
     * @brief Runs one CAPIE command; the body of the registered action.
     * @param out Dispatcher's result storage (constructed here).
     * @param argv Tokens after the command name.
     * @param context Entry context (`context_magic | id`).
     * @return `out`.
     */
    layout::ActionResult* run(layout::ActionResult* out, const layout::TempVector* argv,
                              std::uint64_t context) noexcept {
        out->flag = 0;
        layout::construct_empty(out->message);
        const auto id = static_cast<std::size_t>(context & context_id_mask);
        if ((context & ~context_id_mask) != context_magic || id >= command_count ||
            state_ != Registration::registered) {
            say(out, "CAPIE: console commands inactive");
            return out;
        }
        if (const auto thread = api_.current_thread(); thread != registration_thread_) {
            Message line{};
            line.add("CAPIE_CONSOLE refused: action thread ")
                .add_number(thread)
                .add(" differs from Tick thread ")
                .add_number(registration_thread_);
            note(line.view());
            say(out, "CAPIE: console commands refused (wrong thread, see capie_editor.log)");
            return out;
        }
        if (!thread_logged_) {
            thread_logged_ = true;
            Message line{};
            line.add("CAPIE_CONSOLE first action on thread ").add_number(registration_thread_).add(" (same as Tick)");
            note(line.view());
        }
        const auto command = static_cast<CommandId>(id);
        Tokens tokens{};
        if (!read_tokens(argv, tokens)) {
            say(out, "CAPIE: could not read the command line");
            return out;
        }
        const Plan plan = plan_command(command, tokens.views.data(), tokens.count);
        if (plan.delegate == Delegate::none) {
            say(out, plan.message.view());
            return out;
        }
        if (plan.requires_capie_tag) {
            CosmeticText cosmetic{};
            if (!read_cosmetic(plan.tokens[0].view(), cosmetic)) {
                say(out, "Invalid tag for the command");  // the vanilla text for an unknown tag
                return out;
            }
            if (!is_capie_cosmetic(cosmetic.view())) {
                const auto refusal = reset_refusal(plan.tokens[0].view(), cosmetic.view());
                say(out, refusal.view());
                return out;
            }
        }
        uptr console = 0;
        LiveEntry delegate{};
        if (!mem_.read(api_.console_slot, &console, sizeof(console)) || console == 0 ||
            !find_delegate(console, plan.delegate, delegate)) {
            say(out, "CAPIE: console delegate unavailable");
            return out;
        }
        std::array<engine::MsvcString, max_delegate_tokens> strings{};
        for (std::size_t index = 0; index < plan.token_count; ++index) {
            strings[index] = engine::borrowed_string(plan.tokens[index].bytes.data(), plan.tokens[index].size);
        }
        const layout::TempVector delegate_argv{reinterpret_cast<uptr>(strings.data()),
                                               static_cast<std::int32_t>(plan.token_count),
                                               static_cast<std::int32_t>(plan.token_count), 0};
        const auto call = reinterpret_cast<layout::ActionFn>(delegate.action);
        return call(out, &delegate_argv, delegate.context);
    }

    /**
     * @brief Entry context of a command.
     * @param id Command.
     * @return `context_magic | id`.
     */
    static constexpr std::uint64_t context_of(CommandId id) noexcept {
        return context_magic | static_cast<std::uint64_t>(id);
    }

private:
    /// @brief Longest cosmetic tag CAPIE reads (an RN1 tag is at most 4 + 128 + 1 + 128 + 1 bytes).
    static constexpr std::size_t max_cosmetic_bytes = 512;
    /// @brief Storage of a country tag object written by `10E10C0` (the game uses at most 8 bytes).
    static constexpr std::size_t tag_object_bytes = 16;
    /// @brief A cosmetic tag copied out of the country.
    using CosmeticText = detail::ReadText<max_cosmetic_bytes>;

    /**
     * @brief Writes one log line when a sink is configured.
     * @param text Line without line break.
     */
    void note(std::string_view text) const noexcept {
        if (api_.log != nullptr) {
            Message line{};
            line.add(text);
            api_.log(line.bytes.data());
        }
    }

    /**
     * @brief Reads the current cosmetic tag of a country with the game's own lookups (read only).
     * @param tag Validated three-character tag.
     * @param out Receives the tag bytes (empty when none).
     * @return `false` for an unknown tag or unreadable memory.
     */
    [[nodiscard]] bool read_cosmetic(std::string_view tag, CosmeticText& out) const noexcept {
        alignas(alignof(std::uint64_t)) std::array<unsigned char, tag_object_bytes> tag_object{};
        const auto name = engine::borrowed_string(tag.data(), tag.size());
        api_.tag_parse(tag_object.data(), &name);
        std::int32_t index = 0;
        if (!mem_.read(reinterpret_cast<uptr>(tag_object.data()), &index, sizeof(index)) || index <= 0) {
            return false;  // 282593: the vanilla command treats tag <= 0 as invalid
        }
        const auto country = reinterpret_cast<uptr>(api_.country_of(tag_object.data()));
        return country != 0 && detail::read_string(mem_, country + layout::country_cosmetic_tag, out);
    }

    /// @brief Tokens of one line, copied out of game memory.
    struct Tokens {
        std::array<char, line::max_line_bytes + max_tokens> storage{};  ///< Token bytes.
        std::array<std::string_view, max_tokens> views{};               ///< Views into `storage`.
        std::size_t count{};                                            ///< Number of tokens.
    };

    /**
     * @brief Copies the tokens of `argv` into `tokens`; a line over the limit fails.
     * @param argv Game argv.
     * @param tokens Receives copies.
     * @return `false` when unreadable or too long.
     */
    [[nodiscard]] bool read_tokens(const layout::TempVector* argv, Tokens& tokens) const noexcept {
        layout::TempVector header{};
        if (argv == nullptr || !mem_.read(reinterpret_cast<uptr>(argv), &header, sizeof(header)) || header.count < 0 ||
            header.count > max_tokens) {
            return false;
        }
        std::size_t used = 0;
        for (std::int32_t index = 0; index < header.count; ++index) {
            detail::ReadText<line::max_line_bytes> token{};
            if (!detail::read_string(mem_, header.data + (static_cast<uptr>(index) * engine::msvc_string_bytes),
                                     token) ||
                token.size > tokens.storage.size() - used) {
                return false;
            }
            for (std::size_t c = 0; c < token.size; ++c) {
                tokens.storage[used + c] = token.bytes[c];
            }
            tokens.views[tokens.count++] = std::string_view(tokens.storage.data() + used, token.size);
            used += token.size;
        }
        return true;
    }

    /**
     * @brief Writes a message into the result with the game's string assign.
     * @param out Result (already constructed).
     * @param text Message.
     */
    void say(layout::ActionResult* out, std::string_view text) const noexcept {
        if (!text.empty() && api_.assign != nullptr) {
            api_.assign(&out->message, text.data(), text.size());
        }
    }

    /**
     * @brief Visits every live entry; stops when the visitor returns `true`.
     * @tparam F Visitor `bool(uptr entry_address)`.
     * @param console Console address.
     * @param visit Visitor.
     * @return `false` when the array header is unreadable.
     */
    template <class F>
    [[nodiscard]] bool for_each_entry(uptr console, const F& visit) const noexcept {
        uptr data = 0;
        std::int32_t count = 0;
        if (!core::rd(mem_, console, layout::console_entries, data) ||
            !core::rd(mem_, console, layout::console_entry_count, count) || count < 0 ||
            count > layout::max_live_entries || (count != 0 && data == 0)) {
            return false;
        }
        for (std::int32_t index = 0; index < count; ++index) {
            if (visit(data + (static_cast<uptr>(index) * layout::entry_size))) {
                break;
            }
        }
        return true;
    }

    /**
     * @brief Checks every live name and alias against CAPIE's names.
     * @param console Console address.
     * @param collision Set when a CAPIE name exists.
     * @return `false` when anything was unreadable.
     */
    [[nodiscard]] bool scan_names(uptr console, bool& collision) const noexcept {
        bool readable = true;
        const bool listed = for_each_entry(console, [&](uptr entry) noexcept {
            detail::ReadText<engine::max_name_bytes> name{};
            std::int32_t alias_count = 0;
            if (!detail::read_string(mem_, entry + offsetof(layout::CommandEntry, name), name) ||
                !core::rd(mem_, entry, offsetof(layout::CommandEntry, alias_count), alias_count) || alias_count < 0 ||
                std::cmp_greater(alias_count, layout::alias_slots)) {
                readable = false;
                return true;
            }
            for (const auto& command : command_specs) {
                collision = collision || detail::same_name(name.view(), command.name);
            }
            for (std::int32_t slot = 0; slot < alias_count; ++slot) {
                uptr alias = 0;
                detail::ReadText<layout::max_alias_bytes> text{};
                if (!core::rd(mem_, entry,
                              offsetof(layout::CommandEntry, aliases) + (static_cast<uptr>(slot) * sizeof(uptr)),
                              alias) ||
                    !detail::read_alias(mem_, alias, text)) {
                    readable = false;
                    return true;
                }
                for (const auto& command : command_specs) {
                    collision = collision || detail::same_name(text.view(), command.name);
                }
            }
            return false;
        });
        return listed && readable;
    }

    /**
     * @brief Finds a live entry by name (case-insensitive, like the game's lookup).
     * @param console Console address.
     * @param wanted Name.
     * @param found Receives action and context.
     * @return `true` when found and readable.
     */
    [[nodiscard]] bool find_entry(uptr console, std::string_view wanted, LiveEntry& found) const noexcept {
        bool hit = false;
        const bool listed = for_each_entry(console, [&](uptr entry) noexcept {
            detail::ReadText<engine::max_name_bytes> name{};
            if (!detail::read_string(mem_, entry + offsetof(layout::CommandEntry, name), name) ||
                !detail::same_name(name.view(), wanted)) {
                return false;
            }
            hit = core::rd(mem_, entry, offsetof(layout::CommandEntry, action), found.action) &&
                  core::rd(mem_, entry, offsetof(layout::CommandEntry, context), found.context);
            return true;
        });
        return listed && hit;
    }

    /**
     * @brief Finds a delegate and checks its action address.
     * @param console Console address.
     * @param delegate Delegate.
     * @param found Receives action and context.
     * @return `true` when present at the expected address.
     */
    [[nodiscard]] bool find_delegate(uptr console, Delegate delegate, LiveEntry& found) const noexcept {
        uptr expected = 0;
        switch (delegate) {
            case Delegate::set_cosmetic_tag:
                expected = api_.set_cosmetic_tag;
                break;
            case Delegate::drop_cosmetic_tag:
                expected = api_.drop_cosmetic_tag;
                break;
            case Delegate::eval_effect:
                expected = api_.eval_effect;
                break;
            case Delegate::none:
                return false;
        }
        return expected != 0 && find_entry(console, delegate_name(delegate), found) && found.action == expected;
    }

    /// @brief Fills `entries_` with borrowed strings over the constexpr spec table.
    void build_entries() noexcept {
        for (std::size_t index = 0; index < command_count; ++index) {
            const auto& command = command_specs[index];
            auto& entry = entries_[index];
            entry = layout::CommandEntry{};
            entry.release_flag = 1;
            entry.name = engine::borrowed_string(command.name.data(), command.name.size());
            entry.alias_count = 0;
            entry.description = engine::borrowed_string(command.description.data(), command.description.size());
            entry.action = action_;
            entry.context = context_of(command.id);
            entry.arg_name_count = static_cast<std::int32_t>(command.arg_count);
            for (std::size_t arg = 0; arg < layout::arg_name_slots; ++arg) {
                const auto text = arg < command.arg_count ? command.args[arg] : std::string_view{};
                entry.arg_names[arg] = engine::borrowed_string(text.data(), text.size());
            }
        }
    }

    const M& mem_;                                               ///< Memory policy.
    NativeApi api_;                                              ///< Game addresses.
    uptr action_;                                                ///< CAPIE's registered action.
    Registration state_{Registration::waiting};                  ///< Registration state.
    std::uint32_t registration_thread_{};                        ///< Tick thread that registered.
    bool thread_logged_{};                                       ///< First-action thread line written.
    std::array<layout::CommandEntry, command_count> entries_{};  ///< Source entries for the add call.
};

}  // namespace capie::console
