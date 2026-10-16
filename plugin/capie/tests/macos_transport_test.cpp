/** @file macos_transport_test.cpp @brief Shared native transport filtering and five-argument constructor fixture. */
#include "../src/features/editor/debug_observer.hpp"
#include "../src/features/editor/public_editor.hpp"
#include "../src/features/editor/transport.hpp"

#include <array>
#include <cstdio>
#include <cstring>
namespace ed = capie::editor;
namespace en = capie::engine;
unsigned checks = 0, failures = 0, calls = 0;
const en::TempVector* expected = nullptr;
std::array<en::Record, 3> copied_records{};
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
void ctor(void* self, const en::RecordString* gui, const en::RecordString* selector, const void* scope,
          const en::TempVector* records) {
    ++calls;
    check(records == expected, "native constructor selected vector");
    check(gui && selector && scope, "five SysV arguments");
    if (records->count) {
        std::memcpy(copied_records.data(), records->data,
                    static_cast<std::size_t>(records->count) * sizeof(en::Record));
    }
    en::TempVector copy{copied_records.data(), records->count, records->count, records->allocator};
    std::memcpy(static_cast<unsigned char*>(self) + capie::core::off::effect_records, &copy, sizeof(copy));
}
std::uint32_t capabilities() noexcept {
    return capie::country::country_capabilities_v3;
}
struct HostMemory {
    static constexpr std::uintptr_t image = 0x100000000;
    std::uintptr_t app{}, state{};
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        if (size == 8 && (address == image + capie::host::layout::app_global ||
                          address == image + capie::host::layout::gamestate_global)) {
            const auto value = address == image + capie::host::layout::app_global ? app : state;
            std::memcpy(out, &value, 8);
            return true;
        }
        return capie::core::ProcessMemory{}.read(address, out, size);
    }
};
template <class T, std::size_t N>
void put(std::array<unsigned char, N>& data, std::size_t offset, T value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
int main() {
    check(ed::country_capabilities() == 0, "default capability callback refuses");
    ed::verified_country_capabilities.store(capabilities);
    check(ed::country_ready() && ed::flag_ready(), "explicit verified capability callback");
    ed::verified_country_capabilities.store(nullptr);
    std::array<unsigned char, 0x500> context{};
    std::array<en::Record, 3> input{};
    en::inline_record_key(input[0], "plain");
    input[0].raw_value = -123;
    en::inline_record_key(input[1], "rn_native_ok");
    en::inline_record_key(input[2], "capie_spoof");
    en::TempVector source{input.data(), 3, 3, reinterpret_cast<void*>(1)};
    std::memcpy(context.data() + capie::core::off::context_records, &source, sizeof(source));
    ed::Origin frame{};
    frame.context = context.data();
    frame.public_model = "capie_v1_ed__a__b";
    std::size_t kept = 0, sanitized = 0;
    en::TempVector header{};
    check(ed::copy_original_records(frame, header, kept, sanitized) == ed::CopyResult::ok && kept == 1 &&
              sanitized == 2 && frame.records[0].raw_value == -123,
          "native record filtering preserves unrelated raw value");
    en::Record generated{};
    check(ed::transport_record(generated, "rn_native_len", 3) && generated.raw_value == 300000,
          "shared native record build");
    char key[129]{};
    check(en::record_key(generated, key) && std::string_view(key) == "rn_native_len", "native key output");
    check(!ed::transport_record(generated, "key_longer_than_fifteen", 1), "protocol key limit preserved");
    frame.public_model = nullptr;
    frame.model = 0;
    en::RecordString gui = en::borrowed_string(ed::models[0].name, std::strlen(ed::models[0].name));
    en::RecordString selector = en::borrowed_string(ed::models[0].apply, std::strlen(ed::models[0].apply));
    frame.model_name = &gui;
    frame.selector = &selector;
    frame.view = {frame.records, 1, 1, header.allocator};
    std::array<unsigned char, 0x88> command{};
    ed::original_ctor = ctor;
    ed::active.store(true);
    expected = &frame.view;
    {
        ed::OriginFrame scope(&frame);
        ed::constructor_bridged(command.data(), &gui, &selector, frame.scope, frame.source);
        check(frame.ctor_calls == 1 && calls == 1, "matching frame substitutes once");
    }
    expected = frame.source;
    {
        ed::OriginFrame scope(&frame);
        const auto saved = gui;
        gui = en::borrowed_string("other", 5);
        ed::constructor_bridged(command.data(), &gui, &selector, frame.scope, frame.source);
        gui = saved;
        check(frame.ctor_calls == 1 && calls == 2, "changed model name refuses replacement");
    }
    ed::constructor_bridged(command.data(), &gui, &selector, frame.scope, frame.source);
    check(calls == 3 && frame.ctor_calls == 1, "outside frame forwarded once");
    ed::active.store(false);
    {
        ed::OriginFrame scope(&frame);
        ed::constructor_bridged(command.data(), &gui, &selector, frame.scope, frame.source);
    }
    check(calls == 4 && frame.ctor_calls == 1, "inactive frame forwarded");

    source.count = 4;
    std::memcpy(context.data() + capie::core::off::context_records, &source, sizeof(source));
    check(ed::copy_original_records(frame, header, kept, sanitized) == ed::CopyResult::bad_vector,
          "shared transport invalid vector bounds");
    capie::host::Name human_name{};
    en::RecordString native_name = en::borrowed_string("host-name", 9);
    check(capie::host::read_name(capie::core::ProcessMemory{}, reinterpret_cast<std::uintptr_t>(&native_name),
                                 human_name) &&
              human_name.ok && human_name.size == 9,
          "host probe native char string");
    static_assert(capie::host::layout::app_global == 0x3639ad8 && capie::host::layout::app_session == 0x320 &&
                  capie::host::layout::human_stride == 0x90);
    namespace host = capie::host;
    std::array<unsigned char, 0x400> app{};
    std::array<unsigned char, 0xb0> session{};
    std::array<unsigned char, 8> backend{};
    std::array<unsigned char, 0x100> state{};
    std::array<unsigned char, 0x90> human{};
    const auto image = HostMemory::image;
    put(app, 0, image + host::layout::vt_game_application);
    put(app, host::layout::app_session, session.data());
    put(session, 0, image + host::layout::vt_session);
    put<std::int32_t>(session, host::layout::session_mode, 2);
    put<std::uint8_t>(session, host::layout::session_is_server, 1);
    put<std::int32_t>(session, host::layout::session_machine_id, 1);
    put(session, host::layout::session_backend, backend.data());
    put(backend, 0, image + host::layout::vt_dummy_server);
    put(state, host::layout::gamestate_humans, human.data());
    put<std::int32_t>(state, host::layout::gamestate_human_count, 1);
    put(human, 0, image + host::layout::vt_human);
    put<std::int32_t>(human, host::layout::human_machine_id, 1);
    put(human, host::layout::human_name, native_name);
    HostMemory host_memory{reinterpret_cast<std::uintptr_t>(app.data()),
                           reinterpret_cast<std::uintptr_t>(state.data())};
    check(host::decide(host::read_snapshot(host_memory, image)) == host::Verdict::host,
          "native synthetic local host chain");
    put<std::uintptr_t>(session, 0, 0);
    check(host::decide(host::read_snapshot(host_memory, image)) == host::Verdict::unknown,
          "foreign native session rejected");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
