# Read-only macOS variable-table adapter

`plugin/capie/src/engine/macos_variables.hpp` provides `Table`, `Lookup`, `read_table`
and `find_variable` in `capie::engine::macos::variables`. It makes no game calls,
allocations, writes or hooks. The memory policy supplies bounded `read(address,out,size)`.
Run its separate synthetic suite with `tools/test-macos-variables.sh` (ASan and UBSan).
The suite currently passes24 checks; it does not load the diagnostic dylib or run HOI4.

## Native evidence

Pinned SHA-256 and full instruction evidence are in layers-map.md/json. Rechecked against
the saved disassembly of the same exact binary:

| Property | macOS evidence |
|---|---|
| Container's table at+0x10 | `0x100b0319c` adds0x10 before lookup call `0x100b031a4` |
| Entries pointer and mask | `0x100b03294` reads table+0; `0x100b03287` reads table+0xc, so container+0x10/+0x1c |
| Entry stride/probe | `0x100b032c2` advances0x28; `0x100b032c6` compares next entry+4 against incremented distance; `0x100b032cd` stops when distance exceeds probe |
| Key representation | `0x100b032cf` reads entry+8 libc++ tag; long length+0x10 and pointer+0x18 at `0x100b032dc`/`0x100b032d8` |
| Extra slots and value | `0x100b03314` reads table+0x10 (container+0x20); end uses mask+extra+1; `0x100b03354` returns entry+0x20 |

FNV-1a is32-bit; with the accepted nonnegative mask the unsigned calculation produces the
same bucket as the Windows feature's signed conversion. Probing advances into extra slots
without wrapping, matching native code. The adapter deliberately caps at255 probes and
mask0xffffff, retaining existing feature sanity bounds. Table headers supplied directly to
find_variable are checked again for plausible mask/slot relationships.

## Contract with feature readers

Country and faction names share `name_layer.hpp::open_layer/append_array`: they require
three outcomes (found, absent, unreadable) and raw signed int64 fixed-point values. Flag
reader also uses that shared table/find API. Portrait reader uses it on the character's
embedded variable container. No conversion or fixed-point rounding occurs in this adapter;
feature code retains format/version/alphabet/length validation and absence handling.

Integration must read the country variable-container pointer at country+0x1d8 for country,
faction-leader and flag data. Portrait's container is embedded at character+0xd0; it is not
another pointer to dereference. This adapter accepts the resolved container address only.
It neither chooses a country nor validates host permissions. Windows feature implementation
and game-layout constants are not changed by these adapter files.

The output value changes only on found. Empty/null tables and early robin-hood stops yield
absent; unreadable/inconsistent memory, address wrap, malformed string headers, exhausted
extra slots and probe bounds yield unreadable. Names beyond32 bytes yield absent, matching
the shared reader's existing contract. Heap comparison does not require a readable terminator. Empty keys and embedded NUL are compared as exact
bytes like native memcmp; native termination is not required. CAPIE's higher-level generated
variable names remain ordinary ASCII.

Each entry is copied once (40 bytes), and its24-byte string header is fed to the existing
macos_string decoder through a snapshot adapter. Heap contents are read through the original
memory policy, never by direct dereference. Short-string content and value share the copied
entry. Header, entry and heap reads are not an atomic snapshot of concurrent game mutation;
thread/lifetime coordination remains an integration requirement.

## Checks performed

Synthetic cases cover SSO/heap32-byte key, signed negative values, collision with unequal
key, robin-hood stop, empty/null table, unreadable header/entry/heap, invalid mask and length,
heap capacity corruption, pointer overflow, no-wrap extra-slot exhaustion,255-probe bound,
oversized key, empty key and embedded NUL. The initial test compile failed because the
adapter did not exist; after implementation all24 checks pass under ASan/UBSan. No game
memory reads, hook activation or campaign behavior is claimed.

## Shared feature integration

`features/country/name_layer.hpp` now selects this adapter on Apple platforms.
The Windows lookup implementation remains in its original branch. Both platforms
execute the same version checks, fixed-point decoding, alphabet conversion,
array validation, RN1 encoding and faction-name encoding. Country container reads
on macOS use the proven +0x1d8 offset. The native offsets used by this path are
named in `core/macos_game_layout.hpp`; features and engine consume those constants. Flag and portrait hook integration is still
outstanding; their mention above describes the intended reader contract.

`core/memory.hpp` now routes guarded_copy/ProcessMemory through the tested macOS
kernel-mediated reader while retaining the Windows implementation. Windows-only
readable/SEH helpers are unavailable on Apple. `game_layout.hpp` deliberately
rejects Apple builds rather than silently supplying Windows offsets to unported
callers. This is a compile-time guard, not runtime activation.

The shared feature test `macos_name_layer_test.cpp` passes 32 checks: RN1, Polish
text, faction names, 32-character maximum, invalid version/count/code, missing
elements, fractions, unreadable entries, null pointers and address overflow.
The initial test failed 9 of 13 checks while it still selected Windows offsets;
all checks passed after selecting the native reader. The later flag/cosmetic integration adds 16 checks. ASan/UBSan passes on arm64.
`tools/test-macos-offline.sh` now runs both this test and the 24-check reader suite
on arm64 and x86_64/Rosetta. No native hook bodies or in-game name changes were tested.

## Flag-layer integration

`flag_layer.hpp` now selects the native country-variable pointer and libc++ cosmetic
string on Apple. Packing, version validation, snapshot comparison and rejection of
invalid layers remain shared with Windows. The cosmetic offset +0x1200 is named in
core, proven by `0x10194a4ac: lea r12,[r14+0x1200]` (see layers-map.md).
No flag hooks, atlas writes or game calls are enabled.

The additional 16 checks cover packed `aA_`, absent layers, matching/mismatching
snapshots, nonzero padding, excessive stem length, unreadable values, short/empty/
63-byte heap cosmetic strings, no required heap terminator, excessive tag length,
invalid capacity, unreadable header/payload, null country and address overflow.
Initial compilation rejected the unported Windows layout. The first native run
caught acceptance of a null country through offset arithmetic; the explicit null
check fixes it. All 32 combined checks pass on arm64 with ASan/UBSan and x86_64
under Rosetta. Windows cosmetic-reader implementation is retained in its branch;
no Windows build or live flag change is claimed.
