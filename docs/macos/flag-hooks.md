# Native flag hook bodies (offline)

`features/country/flag_layer_hooks.hpp` now selects native macOS string/layout and
atlas update return conventions. It does not install or activate any game detour.
Windows bodies retain their original layout, string copying and return convention.
The pinned binary is SHA-256
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.

## ABI and ownership evidence

| Boundary | Native contract | Evidence |
|---|---|---|
| Update atlas `0x10194a920` | atlas RDI, gfx RSI; void result | saves at `0x10194a934/937`; no result contract in callers |
| Load `0x10194a460` | atlas RDI, country RSI, dst RDX, three ints ECX/R8D/R9D, int and pointer on stack | entry saves `0x10194a474..484`; incremental caller `0x10194a1d4..1fd`; threaded caller `0x10194bdbc..dc1` |
| Load result | int EAX; success 0 or 5 | incremental `0x10194a234..23b`, threaded `0x10194bdd2..ddd` |
| Candidates `0x100257750` | out RDI, tag RSI, cosmetic RDX, group RCX | saves `0x100257761..76a`; call `0x10194a4e2` |
| Candidate input lifetime | synchronous copy; no ownership transfer of input | inline copy `0x1002579b3..9c9`; long input length/data `0x1002579cb/9cf`, own allocation `0x1002579f9`, memmove `0x100257a1a`, append helper `0x100257a2e` |

Fresh raw bytes read from this binary (not inferred from Windows):

- `0x10194a474`: `4589cd4489c3894dd0488955884989f64889bd68ffffff`
- `0x10194bdbc`: `ff73704152e89ae6ffff`
- `0x10194a234`: `83fb05740885db0f85`
- `0x100257761`: `4989cf4989d44989f64889fb`

The candidate output is the game's custom inline vector. CAPIE never constructs,
interprets or destroys that vector: the original builder receives the original out
pointer and returns its own result. CAPIE supplies a temporary native string via
`macos_owned_string.hpp`, with an empty native cosmetic string and null group, then
frees its temporary heap buffer after the original returns. Allocation failure
forwards original inputs unchanged. Future binding must supply verified matching
allocator/free functions; tests use malloc/free.

Atlas key offsets +0xc0/+0xcc and country array +0x2d8/+0x2e4 are proven in
[layers-map.md](layers-map.md). Raw country tag +8 is only a checked optimization:
the pointer must match the country-list slot, otherwise the list is scanned. Atlas
key writes use this verified list index, never the raw tag unconditionally.

The macOS write policy accepts only one qword, checks the current VM region is
writable and contains the whole target, then uses `mach_vm_write` on this task.
It does not change page permissions. Arithmetic bounds reject overflow before the
write. A concurrent remap remains a semantic lifetime risk; kernel copying avoids
an unchecked userspace destination dereference. Atlas ownership/thread assumptions
still require the runtime integration check.

## Verification

Test-first compilation failed on the Windows-only profile, Windows VM types and
missing native callback slots. After implementation, `macos_flag_hooks_test.cpp`
passes **30 checks** on native arm64 with ASan/UBSan and on x86_64 under Rosetta:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_flag_hooks_test.cpp -o /tmp/capie-flag-hooks-asan
/tmp/capie-flag-hooks-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_flag_hooks_test.cpp -o /tmp/capie-flag-hooks-intel
/tmp/capie-flag-hooks-intel
```

Checks cover eight load arguments, native temporary contents/lifetime, fallback
retry, both success statuses, allocation failure, wrong cosmetic pointer,
no active frame, disabled forwarding, native atlas key invalidation and unchanged
generation. Real private test pages verify qword writes and rejection of null,
wrong size, read-only and unmapped destinations. Synthetic game data remain offline.

Not tested in game: actual texture load, missing-file fallback, all three atlases,
worker-thread invocation and lifecycle after save/load. No detour is installed by
this change; verified trampoline binding and an authorized runtime test remain.
