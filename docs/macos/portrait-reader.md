# Read-only macOS portrait layer

The shared `features/country/portrait_layer.hpp` now selects proven macOS offsets and RTTI
on Apple targets. Windows continues using its existing layout and protocol implementation.
The shared packed-string reader and macOS variable adapter decode the same API7 records;
no resolver hook, game call, write or cache mutation was added.

The separate `tests/macos_portrait_layer_test.cpp` initially failed at the deliberate guard
against including Windows game_layout.hpp on macOS. After the platform selection,24 checks
pass both natively under ASan/UBSan and in an x86_64 build. Commands from the repository root:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_portrait_layer_test.cpp -o /tmp/capie-portrait-reader
/tmp/capie-portrait-reader
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_portrait_layer_test.cpp -o /tmp/capie-portrait-reader-x86
/tmp/capie-portrait-reader-x86
```

## Proven chain

| Field | macOS value | Instruction/identity evidence |
|---|---|---|
| Country politics | +0xd38 | Getter0x10117c434 reads [rdi+0xd38] |
| Ruling party | +0xd0 | 0x100fbef1d in country-leader portrait effect |
| Leader-role array/count | +0x60/+0x6c | 0x101366a7a / 0x101366a74 |
| First role dereference | [roles] | Getter tail-jumps to0x100d82900 |
| Role character | +8 | 0x101300d14 |
| Character primary vtable | RVA0x332efe0 | RTTI10CCharacter, top adjustment0 |
| Embedded character variables | +0xd0 | Writer0x100d5df73 token0x2a4a; reader0x100d5cf20 |
| Character portraits | +0x60 | Writer0x100d5dd21 token0x4c29 |

These constants live in `core/macos_game_layout.hpp`; exact-byte evidence remains in
layers-map.md and layers-pins.json. The read-only layer uses the embedded container address,
not a pointer at character+0xd0. macOS vtable comparison rejects null/overflowing image bases.

Tests cover the complete leader chain, absent party/role, negative role count, null/unreadable
role pointers, foreign vtable, image/character arithmetic bounds, empty embedded variables,
large-only/both-sized portraits, absent/zero small fallback, missing large sprite, unknown or
fractional version, invalid length, malformed small sprite and unreadable entry. Invalid
packed data clears output names rather than exposing a partial replacement. These are
synthetic memory tests, not proof of in-game portrait refresh or sprite validity.

## Hook ABI and offline adapter

The Windows hook declaration must not be reused unchanged. macOS resolver0x100d6fbe0 receives
**out=rdi, map=rsi, type=edx, size=ecx, character=r8, fallback CString*=r9**. Its prologue saves
out in rbx at0x100d6fbfa, character in r15 at0x100d6fbf7 and fallback in r12 at0x100d6fbf4.
The GUI caller0x1023d3340 supplies those registers at0x1023d33c5..0x1023d33d4; the call is
0x1023d33db. Resolver returns the out address in rax at0x100d6ff06. The fallback is a native
libc++ string object, not the Windows `const char*` interpretation.

Native return entry uses set byte+0, kind int32+4,24-byte libc++ name+8 and qword handle+0x20:
copy instructions0x100d6fc18,0x100d6fc1e,0x100d6fc33..3e and0x100d6fcc2..c6 establish the
40-byte field extent. Validity0x1007581a0 reads those same fields and calls sprite_find for
kind0; it has additional game-state-dependent validation, so existence alone is insufficient.
The GUI releases a long return name at0x1023d3412/16 after consuming it. Ownership must match
this game allocator/free pair. Do not expose a shared mutable or borrowed long-name buffer.

The offline adapter now lives in `portrait_layer_hooks.hpp`. Its explicit Apple entry
`resolve_sysv(out,map,type,size,character,fallback)` preserves the native register order;
the shared semantic `resolve(map,out,...)` remains available. Apple originals use the native
resolver, validity function, allocator and deallocator. Windows still uses its original
copy constructor and 48-byte entry. No game function is bound or called by these tests.

A fresh candidate owns its native string. Successful validation transfers the string into
fresh caller return storage; invalid candidates free their heap buffer before invoking the
original. Failed allocation invokes the original without changing output. The game caller
owns and frees successful long results. Inline strings require no allocation. As with the
Windows hook, output must be fresh storage, and originals must be installed before dispatch.
Native validity callbacks are assumed nonthrowing game functions. Activation alone is not
pin verification: the future hook installer must verify all required locations first.

Test `plugin/capie/tests/macos_portrait_hooks_test.cpp` initially failed because the Windows
profile and 48-byte layout were selected and the SysV adapter was absent. After the adapter:
21 checks pass on native arm64 with ASan/UBSan and on x86_64 via Rosetta. They cover native
out/map ordering, unchanged fallback object, return pointer, entry validation fields,
long-string ownership/content, rejected-candidate cleanup, allocation failure, noncivilian
fallback, absent small portrait, changed character identity, inactive layer and inline names.

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_portrait_hooks_test.cpp -o /tmp/capie-portrait-hooks-asan
/tmp/capie-portrait-hooks-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_portrait_hooks_test.cpp -o /tmp/capie-portrait-hooks-intel
/tmp/capie-portrait-hooks-intel
```

Not checked in game: detour installation, actual sprite database validity calls, GUI refresh,
and destruction by the actual game caller. The remaining integration step is a verified
native hook/trampoline binding for resolver, validity, malloc and free, followed by the
separately authorized game test. This adapter does not enable a hook or claim runtime parity.
