# Shared caption and focus readers on macOS

`caption_refresh.hpp` and `focus_release.hpp` now use the platform layout and native
string comparisons. This ports their read-only templates; native game calls remain
behind the separate default-disabled game-call gate. No game was started, no focus
was changed, and no caption refresh callback was invoked for this verification.

Caption model resolution checks both native tagged parent strings through
`macos::records::parent_names_empty`. It does not treat model+0x48/+0x60 as
unconditional lengths: short strings encode their length in the tag byte at
+0x40/+0x58. Empty long strings are accepted only with valid storage and terminator.
Detailed instruction evidence remains in records-transport.md and editor-map.md.

Focus text snapshots now share `macos::records::read_edit_text` with widgets. That
helper decodes one native UTF-32 header snapshot, applies the caller's capacity
limit, and copies content plus terminator through the memory policy. Focus and
widgets enforce128 codepoints and65536 usable capacity. Negative/oversize native
headers, unreadable data, embedded NUL and unterminated content are refused. The
output length is published only after a complete read.

Caption's view resolver retains four native vtable/slot/target checks:

| Vtable | Slot | Target |
|---|---:|---|
| politics view |0x30| native view_is_visible |
| politics observer |0x40| native observer_frame |
| politics observer |0x50| native observer_refresh |
| in-game idler |0xd8| native idler_gui_getter |

The RVAs come from the independently evidenced macOS profile. The idler slot is
checked by the preceding resolver stage, as on Windows. These checks are not an
empty-table bypass. Windows byte fingerprints remain available only in Windows
builds; the native byte manifest/runtime preflight must authorize any future native
activation separately. This change does not reuse Windows signature bytes or
claim its vtable checks constitute complete runtime verification.

Test `macos_caption_focus_test.cpp` was RED on Windows-only profile/PE dependencies
before the platform branches. It now passes **11 checks** on native arm64 with
ASan/UBSan and x86_64 under Rosetta: empty/nonempty/malformed parent names, a long
empty parent and its terminator, short/long UTF-32 snapshots, length/capacity limits,
address overflow, and retention of all four native slot checks. Helper regressions
also pass: records34 and shared widgets23 checks with ASan/UBSan.

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_caption_focus_test.cpp -o /tmp/capie-caption-focus-asan
/tmp/capie-caption-focus-asan
clang++ -std=c++20 -Wall -Wextra -Werror -arch x86_64 \
  plugin/capie/tests/macos_caption_focus_test.cpp -o /tmp/capie-caption-focus-intel
/tmp/capie-caption-focus-intel
```

Not checked in game: the complete caption resolver chain, revision freshness,
focus-release timing, keyboard receiver transitions or native callback lifetime.
Those remain integration checks, not inferred consequences of offline tests.
