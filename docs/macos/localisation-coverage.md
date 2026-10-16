# macOS localisation: corrected coverage analysis

2026-10-16, static analysis only, SHA-256
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.
All addresses below are unslid VAs (image base `0x100000000`).

## Decision

Keep interception at the lookup boundary as the proposed design; do not inject
entries into the game's language arrays or string pool. The three known lookup
hooks are a justified starting point, **not yet a certified complete hook set**.
Do not enable CAPIE or announce API 7 until the remaining coverage and ABI checks
pass. A table-write implementation would add allocation, lifetime, reload and
thread-safety requirements that the current evidence does not establish.

The earlier assertion that three hooks miss most GUI text is **not proven**.
The old notes list 12 functions while saying 11, and mix lookup consumers with
load/update/export routines. The presence of an inlined binary search is not
evidence that a function renders text.

## Proven common text path

| Instruction | Observation |
|---|---|
| `0x1026a3bdf` | Wrapper `0x1026a3b40` calls `strlen` on its input, then forms the end pointer at `0x1026a3be4`. |
| `0x1026a3bfb` | Wrapper calls `0x1026a3640`, with start in `rdx`, end in `rcx`, and recursion depth zero on the stack. |
| `0x1026a377a`..`0x1026a3787` | Consumer computes `end-start`, moves the start into `rdi` and length into `rsi`, and calls `loc_lookup_n` at `0x10288da90`. |
| `0x1026a37b3`, `0x1026a37b8`; `0x1026a37d0`, `0x1026a37d5` | Two `strchr` calls search the returned text for byte `0x24` (`$`). |
| `0x1026a38d5` | An unresolved delimited key recurses through the same `0x1026a3640` consumer. The preceding path tries another expansion handler at `0x1026a3854`. |

The saved full disassembly contains 8,740 direct `call` instructions to
`0x1026a3b40`, two to `0x1026a3640` (wrapper and recursion), and one to
`loc_lookup_n`. These are **call-site counts**, not distinct caller-function
counts and not runtime observations. Tail jumps and indirect calls are excluded.
This proves a widely referenced text expansion path reaches the length-aware
hook. It does not prove every GUI text path does.

## Three entry points, with byte evidence

| Windows ID | macOS VA / RVA | Recovered SysV interface |
|---|---|---|
| `loc_lookup_z` | `0x10288d420` / `0x288d420` | `const char* (const char* key)`; key in `rdi`, result in `rax` |
| `loc_lookup_n` | `0x10288da90` / `0x288da90` | `const char* (const char* key, int length)`; key in `rdi`, length in `esi` |
| `loc_exists` | `0x10288e4c0` / `0x288e4c0` | `bool (const char* key)`; key in `rdi`, result in `al` |

The complete instruction sequences of these three entries are retained in
[`re/localisation-evidence.json`](re/localisation-evidence.json), along with
selected caller and writer sequences. They were freshly disassembled from the
current binary with Apple `llvm-objdump -d --x86-asm-syntax=intel`, using explicit
start/stop addresses **without `--macho`**. Byte ranges are independently compared
with the original file. Symbols printed for an earlier exported function are
not identities of these stripped functions.

For lookup_z, `0x10288d42e` reads global `0x1035dc4d8`, `0x10288d435` loads
the selected language, `0x10288d46f` reads count `+0x24`, `0x10288d478`
loads entries `+0x18`, and `0x10288d49e` establishes the 16-byte stride.
`0x10288d4c8` sign-extends entry `+8`; `0x10288d4cc` adds manager pool `+0x20`.
Missing keys return null. The length-aware variant has the same terminal lookup
at `0x10288db3c`..`0x10288dba5`. Exists additionally checks the pool is non-null
at `0x10288e568`..`0x10288e56d`.

Each entry begins with a 1-byte `push rbp`, a 3-byte `mov rbp,rsp`, and a
10-byte `movabs` constant. Those first 14 bytes contain no RIP-relative operand
or branch. This is a relocation observation, **not proof that installing a
detour is safe** (thread coordination and rollback are still required).

## Reclassification of the alleged missing paths

| Function | Observed role / evidence | Consequence |
|---|---|---|
| `0x1028894a0` | Processes arrays of keys/values, warns about duplicate keys; calls insert/update at `0x102889c3f` and `0x102889c84`; sorts at `0x102889d90`. | Not established as a GUI lookup. Its inlined search participates in loading/comparison. |
| `0x10288a160` | Writes an existing entry's pool offset at `0x10288a278`; updates secondary metadata at `0x10288a2bb` and `0x10288a2be`. | Writer, not just lookup. |
| `0x10288a820` | Searches the language table and tail-jumps to the update function at `0x10288a911`; the other path grows pool storage. | Insert/update path. |
| `0x10288cc30` | Builds quoted, escaped lines and invokes an output callback at `0x10288d328`. | Export-like consumer; exact public API identity remains inferred. |
| `0x10288dbb0` | Selects a language by string comparison, searches its main table, returns translated pointer or original key at `0x10288dd08`..`0x10288dd0f`. | Real alternate lookup. No direct call/jump to this entry found in saved dump; indirect reachability remains open. |
| `0x10288e580` | Compares entries across language tables and passes arrays to a callback at `0x10288ea20`. | Comparison/enumeration-like path, not established GUI rendering. |
| `0x10288fae0` | Compares language entries, constructs `.yml` output names (`0x1028903b8`) and passes output onward. | Export-like path; not evidence that most GUI text bypasses hooks. |
| `0x102890530` | Iterates the complete table (`0x102890637`) and produces hash pairs (`0x102890678`, `0x102890660`). | Table transformation, not a binary search for a requested display key. |
| `0x1028910a0` | Resolves language objects and compares secondary records with a main table (`0x10289125a` onward). | Comparison-like path; full caller semantics unresolved. |

The other three of the 12 old entries are precisely lookup_z, lookup_n and
exists. The saved dump has 25 occurrences of the full FNV basis in 17 functions;
the manifest records their addresses. **This literal search cannot establish
that no other hash construction or lookup exists.** For example, the deletion
helper `0x10288dff0` searches with a precomputed hash, calls `memmove` at
`0x10288e08b`, and decrements entry count at `0x10288e090`. It has no FNV basis
literal. Deletion callers include `0x10288dd20` and `0x10288e100`.

## Remaining acceptance checks

1. Trace concrete country, faction, status and editor GUI consumers into this
   path, including cache and language changes; resolve alternate named-language
   and indirect entry points. Do not equate a global literal scan with coverage.
2. Test the three detours against an x86_64 fixture before loading the game;
   verify argument ABI, null/missing keys, bounded length, nested expansion,
   original fallback, rollback and lifetime of returned strings.
3. In the game, check `CAPIE_STATUS`, `CAPIE_API`, `CAPIE_BUILD`, legacy `RN1`,
   country/adjective/faction names, `$KEY$`, save/load and language changes.
   None of these runtime checks was performed in this investigation.

## Reproduce the file check

```sh
python3 docs/macos/re/verify_evidence.py \
  "$HOME/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4"
python3 -m unittest discover -s docs/macos/re -p test_verify_evidence.py
```

Passing checks establish the recorded digest and bytes only. They never load
the game, install hooks, or assert that the macOS port is ready.

REA independently established artifact identity and Mach-O metadata in
[`re/macho-evidence.json`](re/macho-evidence.json), parent Evidence
`ev_ee8addc5e5756683f5bc6ea65e084f39c94be34b3834cc77bfaf001205b96c23`.
Its symbol inventory is explicitly non-exhaustive. REA/Hopper decompilation was
unavailable because another REA session owned the target; instruction claims
above come from `llvm-objdump`, not from REA decompilation.

## Faction template path: string branch reaches the hook

The faction-name fallback follows `0x100e527e0 -> 0x100f9b2a0 -> 0x102884970`.
The last function calls `0x102887ae0` at `0x102884a0a` and `0x102887be0` at
`0x102884a25`. Recursive evaluator `0x102887be0` calls value conversion
`0x102899490` at `0x102887db5` and `0x102887e1e`.

Value conversion reads the discriminator at `[rsi+0x20]` (`0x10289949c`).
For type zero it reads a libc++ string at `rsi+8`: bit 0 selects the heap pointer
at `+0x18` (`0x1028994c1`), otherwise inline bytes start at `+9`
(`0x1028994ed`). Both converge on the common text wrapper call at
`0x102899502 -> 0x1026a3b40 -> 0x1026a3640 -> loc_lookup_n`.
This closes the previously unknown **string-value branch** of the faction
expression path. Four additional fresh byte ranges are retained in the evidence manifest.

Type one instead invokes a callback at `0x1028994e9`; type two returns an integer
at `0x1028994ad..0x1028994b2`. The callback branch is not proven to traverse the
lookup hooks. Finding the string branch does not establish full expression coverage.
The recursive direct-call walk covered 20 functions in `0x102884000..0x1028a0000`;
out-of-range and indirect targets were not exhaustively explored.

## Country key generation and localisation

`UpdateNames` at `0x10117a790` invokes the same common text wrapper for the
computed country strings, including calls at `0x10117afe9`, `0x10117b043` and
`0x10117b094`. These paths therefore reach `loc_lookup_n` through the already
proven wrapper/consumer chain. The recorded range includes selection of inline
versus heap string bytes before the second and third calls.

Key-existence wrapper `0x1026af1f0` reads the key pointer from `[rdi]` and
tail-jumps at `0x1026af1f8` to `loc_exists`. `BuildNameKey` calls this wrapper
throughout its fallback selection. Thus both key-existence tests and subsequent
localisation have identified hook boundaries for the RN1/country-name path.
This is static control/data-flow evidence, not a successful campaign test.

The named-language lookup `0x10288dbb0` had no pointer references in the
`__DATA,__const` scan. Together with the absence of direct calls/jumps in the
saved disassembly this bounds its observed reachability; other sections,
computed addresses and runtime behavior remain outside that negative search.

## Faction return-string adapter

`engine/macos_owned_string.hpp` replaces the missing standalone Windows
`string_copy` call with an explicit copy-construction adapter. The exact native
algorithm is visible at `0x100e52865..0x100e52891`: round allocation to
`(length | 15) + 1`, store allocation+1 at +0, length at +8 and pointer at +16,
then copy length+1 bytes. The allocator at `0x102a7fbd0` tail-jumps to imported
malloc; its counterpart `0x102a7fc10` tail-jumps to free. These ranges are retained
in the evidence manifest. Production integration must pin these targets and
supply the verified allocator. The adapter only constructs fresh return storage;
it never overwrites an existing owning string or stored game state.

## Contract-scoped decision after callback inspection

The three lookup hooks remain the appropriate **localisation interception boundary**
for the traced plain-key/country paths. They are **not sufficient by themselves for the
whole API contract**: dynamic country/faction return values require their feature hooks,
and concrete status GUI entrypoints, all formatter callbacks and runtime invalidation
remain outside the closed proof. No fourth lookup hook is justified merely by the type-one
callback. Do not report universal GUI coverage or enable the full contract on this evidence.

Type-one conversion is a registered formatter path. Parser `0x102898f40` first tests a
`token:` form, then searches for byte0x7c (`|`) at `0x102898fa7`/`0x102898fae`. Absence
branches to `0x102899071`: it stores the input as type-zero string (assignment or variant
conversion; helper `0x10289a900` clears the discriminator at `0x10289a9ac`). Presence splits off the suffix, converts that suffix to an integer through
`0x102a6c0b0` at `0x1028990ca`, takes the prefix at `0x1028990e5`–`0x1028990f0`, and looks
up that prefix in the formatter registry `0x1034fdc40` at `0x1028990f5`–`0x1028990ff`.
On a successful lookup, entry+0x20 is saved at value+8 (`0x1028991c7`), parsed integer at
value+0x10 (`0x1028991ce`), and discriminator1 at value+0x20 (`0x1028991d2`). A missing
formatter produces "Unknown: Formatter not found", not a proven fallback lookup.

The converter `0x102899490` then invokes the first stored function pointer at
`0x1028994e9`, with output in rdi, integer token in esi and a key/value range in rdx/rcx.
It transfers the callback-produced string to the result at `0x102899507` without an
unconditional localisation lookup afterwards. Thus a callback **can** return text without
passing these hooks; whether a particular callback internally localises is a separate fact.

The registry insertion helper is `0x102898c70`; its assertion at `0x102898d71` identifies
"Multiple formatters with the same key found", backed by pdx_loc_formatter.cpp source
path. The saved dump has nine direct calls at `0x101265e90`, `0x10126750d`, `0x1012676c7`,
`0x10126791b`, `0x101267d4d`, `0x10126890e`, `0x10126a62d`, `0x10126a87d`, `0x10126aeb7`.
Nearby registration operands identify building-state modifier, character-name, advisor-desc,
country-leader-desc, country-culture, equipment-directory, idea-desc, idea-name and tech-effect
formatters. This bounded direct-call scan is not an exhaustive registry inventory: indirect
registration, another insertion routine or runtime changes were not excluded.

| Contract surface | Static conclusion | Remaining check |
|---|---|---|
| CAPIE_STATUS / CAPIE_API / CAPIE_BUILD | These plain keys contain neither `token:` nor `|`; through the inspected parser they remain type-zero strings, whose converter reaches lookup_n. Callback type1 is not a counterexample for this construction. | Trace each actual SDK GUI consumer; test displayed values and status refresh in game. |
| Legacy RN1 cosmetic country name | UpdateNames already reaches common wrapper→lookup_n; BuildNameKey existence tests reach exists. This path does not require the formatter parser. | Campaign/cache/language checks. Do not pass RN1 through arbitrary formatted-localisation APIs: RN1 contains `|` and would take formatter parsing. |
| Dynamic country/adjective names | Localisation/existence boundaries are identified, but private per-country overrides depend on UpdateNames/BuildNameKey hooks and state. | SysV integration and cache refresh; three loc hooks alone do not implement this feature. |
| Faction stored/private name | CFaction::GetName's stored text copies inline; private override requires the faction feature hook and owned-string adapter. | Verify output ownership/lifetime and private override in game. |
| Faction template expression | Type-zero string conversion reaches lookup_n; type-one formatter output has no mandatory post-callback lookup. | Callback semantics for actual templates and feature-hook coverage. No claim that all expression output traverses lookup_n. |

Fresh contiguous bytes and disassembly for parser, converter, constructor and registration
assertion are in `localisation-callback-evidence.json`, separate from the existing evidence
manifest. They were read directly from the pinned file with the Mach-O helper; the existing
REA identity evidence above is reused. No Hopper retry, game launch, registry write or hook
installation was performed. This closes the classification of the callback branch, not the
complete static/runtime proof of every contract consumer.

The checker-compatible companion export is
[`re/localisation-callback-evidence.json`](re/localisation-callback-evidence.json).
Verify its five ranges with:

```sh
python3 docs/macos/re/verify_evidence.py \
  "$HOME/Library/Application Support/Steam/steamapps/common/Hearts of Iron IV/hoi4.app/Contents/MacOS/hoi4" \
  --manifest docs/macos/re/localisation-callback-evidence.json
```

The richer sibling manifest above retains disassembly and remains unchanged.
