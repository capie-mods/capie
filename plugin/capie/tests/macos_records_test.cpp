/** @file macos_records_test.cpp @brief Offline native record and constructor boundary tests. */
#include "../src/engine/macos_records.hpp"
#include "../src/engine/records.hpp"
#include "../src/features/editor/trampolines.hpp"

#include <array>
#include <cstdio>
#include <cstring>
namespace native = capie::engine::macos;
namespace records = native::records;
unsigned checks = 0, failures = 0, calls = 0;
void check(bool result, const char* name) {
    ++checks;
    if (!result) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
struct Memory {
    bool read(std::uintptr_t at, void* out, std::size_t size) const noexcept {
        return capie::core::ProcessMemory{}.read(at, out, size);
    }
};
records::TempVector* expected;
void ctor(void* self, const native::NativeString* gui, const native::NativeString* selector, const void* scope,
          const records::TempVector* source) {
    ++calls;
    check(source == expected, "constructor chosen vector");
    check(gui && selector && scope == reinterpret_cast<void*>(3), "SysV arguments preserved");
    check(self != nullptr, "fresh self forwarded");
}
int main() {
    static_assert(std::is_same_v<capie::editor::Constructor, records::Constructor>);
    static_assert(sizeof(records::Record) == 32 && sizeof(records::TempVector) == 24);
    Memory mem;
    records::Record record{};
    native::construct_name("rn_native_ok", record.key, nullptr);
    record.raw_value = -100000;
    std::array<char, 129> key{};
    std::size_t length = 99;
    check(records::read_key(mem, record, key, length) && std::string_view(key.data(), length) == "rn_native_ok",
          "native record key");
    records::TempVector source{&record, 1, 1, reinterpret_cast<void*>(42)}, copy{};
    check(records::read_vector(mem, reinterpret_cast<std::uintptr_t>(&source), copy) && copy.count == 1,
          "native vector bounds");
    source.count = 2;
    check(!records::read_vector(mem, reinterpret_cast<std::uintptr_t>(&source), copy), "count exceeds capacity");
    source.count = -1;
    check(!records::read_vector(mem, reinterpret_cast<std::uintptr_t>(&source), copy), "negative count");
    source.count = 1;
    native::NativeString gui{}, selector{};
    native::construct_name("capie_editor", gui, nullptr);
    native::construct_name("apply", selector, nullptr);
    records::TempVector replacement{&record, 1, 1, source.allocator};
    records::ConstructorFrame frame{&gui,           &selector, reinterpret_cast<void*>(3), &source, &replacement,
                                    "capie_editor", "apply"};
    expected = &source;
    int self = 0;
    records::forward_constructor(mem, ctor, nullptr, &self, &gui, &selector, reinterpret_cast<void*>(3), &source);
    check(calls == 1, "outside frame forwarded");
    expected = &replacement;
    records::forward_constructor(mem, ctor, &frame, &self, &gui, &selector, reinterpret_cast<void*>(3), &source);
    check(frame.calls == 1, "matching native frame replaced");
    native::NativeString impostor = gui;
    expected = &source;
    records::forward_constructor(mem, ctor, &frame, &self, &impostor, &selector, reinterpret_cast<void*>(3), &source);
    check(frame.calls == 1, "equal name distinct identity forwarded");
    native::construct_name("other", gui, nullptr);
    records::forward_constructor(mem, ctor, &frame, &self, &gui, &selector, reinterpret_cast<void*>(3), &source);
    check(frame.calls == 1, "same identity changed name forwarded");
    std::array<unsigned char, 0xb8> model{};
    check(records::parent_names_empty(mem, reinterpret_cast<std::uintptr_t>(model.data())), "empty native parents");
    model[0x40] = 2;
    model[0x41] = 'x';
    check(!records::parent_names_empty(mem, reinterpret_cast<std::uintptr_t>(model.data())),
          "nonempty parent rejected");
    std::array<unsigned char, 0xb8> edit{};
    edit[0xa0] = 4;
    char32_t content[3] = {U'ą', U'猫', 0};
    std::memcpy(edit.data() + 0xa4, content, sizeof(content));
    std::array<char32_t, 8> text{};
    length = 99;
    check(records::read_edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), text, length) && length == 2 &&
              text[0] == U'ą',
          "native short UTF32 snapshot");
    edit[0xa0] = 10;
    check(!records::read_edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), text, length),
          "short UTF32 length exceeds capacity4");

    std::array<char32_t, 8> heap_text = {U'a', U'b', U'c', U'd', U'e', 0};
    std::uint64_t allocation = 9, heap_length = 5;
    const auto heap_address = reinterpret_cast<std::uintptr_t>(heap_text.data());
    std::memcpy(edit.data() + 0xa0, &allocation, 8);
    std::memcpy(edit.data() + 0xa8, &heap_length, 8);
    std::memcpy(edit.data() + 0xb0, &heap_address, 8);
    check(records::read_edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), text, length) && length == 5 &&
              text[4] == U'e',
          "native heap UTF32 snapshot");
    heap_length = 8;
    std::memcpy(edit.data() + 0xa8, &heap_length, 8);
    check(!records::read_edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), text, length),
          "heap length exceeds tagged capacity");
    char heap_key[] = "a_key_longer_than_twenty_two_bytes";
    allocation = 49;
    heap_length = sizeof(heap_key) - 1;
    const auto heap_key_address = reinterpret_cast<std::uintptr_t>(heap_key);
    std::memcpy(record.key.bytes.data(), &allocation, 8);
    std::memcpy(record.key.bytes.data() + 8, &heap_length, 8);
    std::memcpy(record.key.bytes.data() + 16, &heap_key_address, 8);
    check(records::read_key(mem, record, key, length) && std::string_view(key.data(), length) == heap_key,
          "long record key read");
    heap_key[1] = 0;
    check(!records::read_key(mem, record, key, length), "embedded NUL key refused");
    capie::engine::Record shared{};
    check(capie::engine::inline_record_key(shared, "rn_ip_cmd_epoch") && shared.key.bytes[0] == 30,
          "shared inline record constructor");
    char output[129]{};
    check(capie::engine::record_key(shared, output) && std::string_view(output) == "rn_ip_cmd_epoch",
          "shared native key reader");
    check(!capie::engine::inline_record_key(shared, std::string_view(heap_key, sizeof(heap_key) - 1)),
          "overlong inline record rejected");
    source.capacity = -1;
    check(!records::read_vector(mem, reinterpret_cast<std::uintptr_t>(&source), copy), "negative capacity refused");
    source.capacity = 1;
    source.data = reinterpret_cast<records::Record*>(~std::uintptr_t{0} - 3);
    check(!records::read_vector(mem, reinterpret_cast<std::uintptr_t>(&source), copy), "record span overflow refused");
    check(calls == 4, "exactly one constructor per invocation");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
