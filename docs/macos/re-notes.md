# CAPIE on macOS: reverse-engineering notes

Starting notes for a macOS port of CAPIE. Everything here was read from the macOS build of
HOI4 1.19.3.0 by static analysis only (nothing was run). Items marked **proven** were checked
instruction by instruction; everything else is a lead to verify.

## The macOS game binary

| | |
|---|---|
| Path | `~/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4` |
| Version | `Operation Postern v1.19.3.0.c01a (5632)` (`launcher-settings.json`) |
| SHA-256 | `09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb` |
| Format | Mach-O x86_64 only (runs under Rosetta 2 on Apple Silicon), built with clang/libc++ |
| Image base | `0x100000000`; `__TEXT,__text` `0x10000b330`..`0x102b25350` |
| Code signature | none, so no hardened runtime |
| Fixups | classic `LC_DYLD_INFO_ONLY` (pointers in `__DATA` are plain unslid VAs) |
| Functions | 130,315 entries in `LC_FUNCTION_STARTS`; names stripped except ~970 weak/template functions |
| RTTI | every typeinfo-name string (`_ZTS`) is present; typeinfo objects are exported only for weak classes |

The Paradox Launcher (`dowser`) is a separate binary signed by Paradox with hardened runtime
(`flags=0x10000(runtime)`). `run_hoi4` in the game folder starts `hoi4` directly.

## Why the Windows numbers cannot be reused

- Different compiler and ABI: MSVC/Microsoft x64 on Windows, clang/Itanium/SysV on macOS. No
  Windows RVA, byte pattern or vtable slot carries over.
- Strings: the game's strings on macOS use the libc++ layout (24 bytes; short mode when byte 0
  bit 0 is clear, size = byte0 >> 1, text at +1; long mode: size at +8, data pointer at +0x10).
  MSVC strings are 32 bytes, so every struct offset after a string member moves. The mangled
  name `CString` exists in symbols; check that it is a plain libc++ string wrapper before relying
  on the layout.
- Itanium vtables start with two destructor slots (complete and deleting) where MSVC has one, so
  later slots are usually shifted by one. Verify every slot.
- clang inlined code that MSVC kept out of line (see localisation below).

## Finding classes and vtables

Vtables are found through RTTI even when the typeinfo symbol is stripped: find the string
`<len><name>\0` (e.g. `8CEditBox`) in `__TEXT,__const`, find the typeinfo object in
`__DATA,__const` whose second word points at that string (typeinfo = pointer address - 8), then
every 8-byte word in `__DATA,__const` equal to the typeinfo address is a vtable's RTTI slot; the
first virtual function slot is 8 bytes after it, and the word 8 bytes before it is the
offset-to-top.

**CEditBox** (proven via RTTI):
- primary vtable, first slot at `0x1034add98`, 86 slots;
- secondary vtable at `0x1034ae058` with offset-to-top `-112` (0x70), i.e. a base subobject at
  `edit + 0x70`. On Windows the text-buffer subobject is at `edit + 0x80`; check whether this is
  the same subobject.

Useful anchors: `CCommandHelper<CExecuteScriptedWindowEffect>::CommandRegistrator::Create()` is
exported at `0x10021a680`; typeinfo-name strings exist for `CSetVariableEffect`,
`CEffectEntry<CSetVariableEffect>`, `CNullEditBox`, `CNullInstantTextBox`, `TEditBox`,
`CScrollbarObserverGlue<CInstantTextBox>`, `CLegacyButtonObserverGlue<CCountryPoliticsView>`,
`CLegacyButtonObserverGlue<CScriptedWindowGUIUpdater>`, `TGameItemDatabase<CScriptedWindowDatabase>`,
`TGameItemDatabase<CScriptableLocalizationDatabase>` (instance pointer `0x1034d0f70`).

## Localisation

**2026-10-16 correction:** see [the coverage analysis](localisation-coverage.md).
Several searches below belong to writers/exporters, and the common `$KEY$` path
does call `loc_lookup_n`. The old claim that three hooks miss most GUI text was
not established. The entry addresses remain independently byte-verified.

### The module

All localisation lookups live in one module at about `0x102888000`..`0x102896000`. It is the only
code in the binary that uses the FNV-1a offset basis `0xcbf29ce484222325` (25 occurrences in 17
functions; prime `0x100000001b3`).

### Manager (proven)

Global `0x1035dc4d8` holds a pointer `P` to a 0x58-byte manager. All 83 references to this global
are inside the module. `0x102889080` allocates it (`new(0x58)`) and calls the constructor
`0x102888830`; `0x102889110` returns `P != nullptr`.

| Offset | Meaning |
|---|---|
| `P+0x00` | current language object `L` |
| `P+0x08` | array of language objects: data `+0x08`, capacity `+0x10`, count `+0x14`, allocator `+0x18` |
| `P+0x20` | string pool (char array): data `+0x20`, capacity `+0x28`, count `+0x2c`, allocator `+0x30` |
| `P+0x38` | bool (constructor argument) |
| `P+0x40` | another array: data `+0x40`, capacity `+0x48`, count `+0x4c`, allocator `+0x50` |

### Language object `L` (partly proven)

| Offset | Meaning |
|---|---|
| `L+0x00` | language name, libc++ string |
| `L+0x18` | sorted entry array, 16-byte entries: `u64` FNV-1a hash of the key at `+0`, `int32` offset into the string pool at `+8` |
| `L+0x24` | `int32` entry count |
| `L+0x30` | second array of 24-byte entries (hash at `+0`, pool offset at `+8`, int at `+0x10` / `+0x14`), count `L+0x3c` |
| `L+0x48` | array used by `0x10288ca10` (entries of 24 bytes, libc++ strings) |

A lookup hashes the key, binary-searches `L+0x18` for the first entry with hash >= key hash,
compares the hash only, and returns `pool_base (P+0x20) + entry.offset`, or null when missing.

### Entry points (proven)

| Windows | macOS | Signature (SysV) | Callers |
|---|---|---|---|
| `loc_lookup_z` (239ED40) | `0x10288d420` | `const char* (const char* key)` | 9 |
| `loc_lookup_n` (239EE20) | `0x10288da90` | `const char* (const char* key, int length)` | 1 |
| `loc_exists` (239D0F0) | `0x10288e4c0` | `bool (const char* key)` | 17 |

Also: `0x10288e470` = `u64 fnv1a(const char*)`; `0x10288d4e0(const char* language, const char* key)`
= find a key's int value in a named language's 24-byte table (returns -1 when missing);
`0x10288dbb0(const char* fallback, const char* language)` = same main lookup in a named language,
returning `fallback` when missing; `0x10288ca10` = lookup returning a string from `L+0x48`.

### The coverage problem

On Windows, hooking the three entry points was the reference design. The initial macOS
investigation listed these 12 functions as possible inlined lookup paths:
`0x1028894a0`, `0x10288a160`, `0x10288a820`, `0x10288cc30`,
`0x10288d420`, `0x10288da90`, `0x10288dbb0`, `0x10288e4c0`, `0x10288e580`, `0x10288fae0`,
`0x102890530`, `0x1028910a0`. Subsequent instruction analysis distinguishes load/update,
export and comparison paths from display lookups. In particular, the common text expansion
wrapper `0x1026a3b40` reaches `loc_lookup_n` through `0x1026a3640`, including recursive
`$KEY$` substitution. Full GUI coverage is still open; it is not proven absent.

Original alternatives, now superseded by the provisional interception decision in
[localisation-coverage.md](localisation-coverage.md):
1. Find the smallest set of module functions that together cover every lookup.
2. Make CAPIE's keys real entries in the table. The entry array is per language object and the
   strings are int32 offsets from the pool base, so any added string must be reachable within
   int32 range of `P+0x20`. Before choosing this, find every writer of `P+0`, `L+0x18`, `L+0x24`
   and the pool after loading, and whether any code caches `L` or the entry pointer.

## Mapping progress

The starting list below is retained as scope, not current progress. New evidence is in
[editor-map.md](editor-map.md), [layers-map.md](layers-map.md), and
[console-map.md](console-map.md). An unresolved row must never become a runtime pin.

Everything else in `plugin/capie/src/core/game_layout.hpp` and the `*_pins.hpp` files: the
scripted window chain (Tick, Bind, Dispatch, effect constructor and Execute), FindEdit,
FindInstantText, SetText, edit refresh, the country/faction name layer, the flag and portrait
layers, the caption refresh and focus release chains, and every struct offset. Each needs a
macOS address and offset found by behaviour (strings, constants, vtables, call graph), with the
instruction that proves it.

## Tools used

`docs/macos/re/macho.py`: a small read-only Mach-O reader (sections, RTTI vtable finder,
pointer scan, function starts) plus fast lookups into a full `llvm-objdump` dump. Make the dump
once (about 3 minutes, 375 MB):

```bash
/Library/Developer/CommandLineTools/usr/bin/llvm-objdump --macho -d --no-show-raw-insn --demangle --x86-asm-syntax=intel hoi4 > dis.txt
```

Put a symlink named `hoi4` to the game binary next to `macho.py`, then for example
`python3 -I macho.py vt CEditBox` or `python3 -I macho.py str 'politics_tab'`.
