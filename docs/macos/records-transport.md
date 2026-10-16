# Native record and editor string boundary

`engine/macos_records.hpp` implements the macOS wire representations and bounded
constructor forwarding adapter. `engine/records.hpp` dispatches to those native
representations on Apple; Windows keeps its MSVC representations. Shared transport's
two inline record-key construction sites now call `engine::inline_record_key` rather
than writing MSVC fields. Protocol keys remain bounded by the existing 15-byte limit.
`editor/trampolines.hpp` selects explicit SysV function types without a fake
`__fastcall` macro. This is an offline boundary port, not activation of the editor.

## Four adapter-only fields

These are not missing numeric offsets. Assigning Windows-like size/capacity fields
would be wrong. Existing `macos_string.hpp` decodes native tagged char/UTF-32 strings;
the new wrappers apply that decoder to the proven object positions.

| Windows-style field | Native evidence | Adapter |
|---|---|---|
| model_parent_window_len | string +0x40; `0x100b4622c` byte tag, short size `>>1`; `0x100b462d7` long size +0x48 | `parent_names_empty` validates both strings, including termination |
| model_parent_sg_len | string +0x58; `0x100b4602b..40` short tag or long size +0x60 | same adapter |
| edit_size | UTF-32 object +0xa0; `0x102793c4f` tag, `0x102793c73` long length +0xa8 | `read_edit_text` returns decoded element count |
| edit_capacity | `0x102793ca7..cd7`: `(length|3)+1` element allocation, byte allocation multiplied by4, tagged count OR1 | decoder checks long length against `(tag & ~1)-1`; short capacity4 |

`read_edit_text` copies UTF-32 and requires termination/no embedded NUL; Unicode
scalar validity and protocol alphabet validation remain higher-level checks.
No adapter writes strings or assumes a fixed unconditional length field.

## Native records and constructor

The constructor boundary is the thunk `0x10020b820 -> 0x10020b2b0`.
Dispatch call `0x100b4bae5` supplies self RDI, model-name RSI, selector RDX,
scope RCX and records R8. `0x100b4bacf` forms context+0x470 for the records vector;
`0x100b4bad6` forms model+8 for its name. This is five ordinary SysV arguments,
without a hidden return object.

**Native constructor has a void result contract.** The final path at
`0x10020b759..778` calls allocator vslot0x18 and returns without restoring self
in RAX. Dispatch uses its saved command pointer R13 at `0x100b4baed`, not RAX.
The Windows pointer-return signature must not be reused for post-call inspection;
the macOS adapter returns void, and diagnostics must inspect the original self.

Native record is **32 bytes**: libc++ char string+0, signed int64 raw value+0x18.
Native vector is **24 bytes**: data+0, int32 capacity+8, int32 count+0xc,
allocator+0x10. Constructor reads source data/count at `0x10020b30f/312`, creates
its own vector at self+0x28, and calls helper `0x10021a6f0` at `0x10020b31c`.
The helper allocates count*32 (`0x10021a755`), copies inline keys and raw value
(`0x10021a7a0..7c0`), and allocates/copies each long key
(`0x10021a7d4..83f`). Thus replacement records need only survive the synchronous
constructor call; source key storage must remain valid until that call returns.
No ownership of source records or strings transfers to the command.

Dispatch first applies its original records at `0x100b4b863..8ce`, before reaching
the constructor. Constructor-only replacement therefore does not inject CAPIE
records into that earlier dispatch-side variable application. The full shared
transport must retain its existing reserved-key filtering and dispatch gates.

Fresh bytes from SHA256
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`:

- `0x10020b30f`: `498b10418b480c48`
- `0x10021a7b3`: `498b441c18498947184983c7204883c320`
- `0x100b4bac7`: `4c8b45d0498b70204981c0700400004883c6084889c7488b55c0488b4dc8e836fd6bff`
- `0x102793ccf`: `4983cc014c8965c04c897dc8`

## Verification and integration boundary

`macos_records_test.cpp` started RED with the missing native header. It now checks
native and shared layout, bounded vectors, inline/heap record keys, malformed keys,
inline construction, negative bounds/overflow, exact identity plus fresh names for
replacement, exactly one native constructor invocation, parent names and short/long
UTF-32 including tagged capacity rejection. Synthetic callbacks exercise the SysV
signature; no game function is called.

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_records_test.cpp -o /tmp/capie-records-asan
/tmp/capie-records-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_records_test.cpp -o /tmp/capie-records-intel
/tmp/capie-records-intel
```

The `ConstructorFrame` is an explicitly supplied, stack-owned, previously authorized
frame. This helper enforces argument identity and current names, but does not itself
prove window visibility, epoch, payload authorization or reserved-key filtering.
The shared transport, public editor and debug observer now compile on macOS with
native RecordString arguments and a void constructor that inspects self afterward.
The integrated fixture and capability/host boundaries are documented in
[shared-transport.md](shared-transport.md). Nothing here installs a hook or validates
an in-game Apply click; lifecycle, UI-thread lifetime and native callbacks still need
an independently authorized runtime integration check.
