# Port to HOI4 1.20.0.0

Port of CAPIE from HOI4 1.19.5.0 to 1.20.0.0 (CAPIE 1.7.1 → 1.7.2, branch `port-1.20.0`). Method and
tools as in [port-1.19.5.0.md](port-1.19.5.0.md), [port-1.19.4.2.md](port-1.19.4.2.md) and
`tools/port/README.md`; the old → new map by pin id is `tools/port/map_1.20.0.0.json`.

- Old build: 1.19.5.0, `hoi4.exe` SHA-256 `709F916AFBEBCFE64A26678702691AEB40945D206074C2A80DE8E9D601FCE148`,
  linked 5 November 2026 (private copy kept outside the repository, as in the last port).
- New build: 1.20.0.0 (launcher "Operation Postern v1.20.0.0.8873"), `hoi4.exe` SHA-256
  `1BEB2A38FE2D747E5B7ADB9B9648E350B0B851845DD228FFB87D2D2AC1B63A9F`, linked 17 November 2026, the
  file the Steam install holds. A private copy is kept for the next port.
- Addresses are RVAs from image base `0x140000000`.

## Result

1.20.0.0 is a minor/major update, but for CAPIE it behaves like a rebuild of the 1.19.5.0 code at other
addresses. Every function CAPIE pins, hooks or calls has the same instructions as before (relocation
fields aside), every pinned instruction is the same instruction with the same structure offset, every
vtable has the same slots, and every token value is the same. What moved is the address of everything:
all 231 byte pins, all 66 RIP/rel32 pins and 104 of 104 `rva::` constants, the host and console
constants. Nothing needed a design decision and no layer is turned off: the port re-pins the build (hash,
version, accepted table, map) and the CAPIE release (1.7.2, build number 10702, no feature change).

One byte-level change inside pinned code is worth naming (no consequence): in the two token-registration
pins `caption.F8915` and `caption.F7B47` the compiler's stack frame of the huge registration function
shifted by 8 (`mov rcx,[rsp+1AF60]` is now `mov rcx,[rsp+1AF58]`, `[rsp+1AD70]` is now `[rsp+1AD68]`). The
pin only identifies the registration of the token literal (`politics_tab`, `parent_window_token`), which is
unchanged, and its new bytes are generated from the new file.

## Byte-level picture of the two executables

| | 1.19.5.0 | 1.20.0.0 |
|---|---|---|
| Image size | `0x38A3000` | `0x38A9000` (+0x6000) |
| Entry point | `25A019C` | `25A396C` |
| `.text` (VA `0x1000`) | `0x2783D48` | `0x2787538` (+0x37F0) |
| `.rdata` | `2785000`, `0x977F92` | `2789000`, `0x979392` (+0x1400) |
| `.data` | `30FD000`, `0x598754` | `3103000`, `0x598BE4` (+0x490) |
| `.pdata` | `3696000`, `0x19CC80` | `369C000`, `0x19CE54` |
| PE timestamp | `6AEC984B` | `6AFC6461` |

The sections keep their order and grow by small amounts, so code moves by +0x90 … +0x3850 and data by
+0x4020 … +0x6544, in steps that change from region to region. The shifts are not uniform, so no
region delta was used: every address was derived as below, and the per-region deltas only served as a
plausibility check.

Function-level diff (`.text` split at the 16-byte aligned `int3` padding: 140,189 blocks in the old and
140,227 in the new file; each block hashed after masking the RIP displacement and the rel32 of every call
and jump; the two block lists aligned with `difflib.SequenceMatcher`):

- 136,747 blocks (97.5 %) are identical. 2,115 aligned regions differ: new and removed game code, and
  functions that hold a switch table or another image-relative constant, which change with the layout
  (a `[rdx+rax*4+table]` operand holds an RVA).
- Of the pinned items only four regions are not in the identical set, and each was read by hand:
  - `caption.163BBED`: the token parser's switch (`parser copies token to model+2C`). The region differs
    only in its two jump-table operands and the table's int32 case RVAs; the pinned `mov eax,[rsi+C0];
    mov [r13+2C],eax; jmp` is the same instruction run at the shifted address.
  - `rebuild.mode_bound` and the three `rebuild.case_*` jump-table entries (the colour layer's map-mode
    rebuild switch): the function is the same (`cmp edi,16`, `ja`, the table lookup) with the table at
    `FBB7CC` in the new file and the case code at the same offsets from the function start (`+0x37`, `+0x171`,
    `+0x195`).
  - `caption.F8915` and `caption.F7B47`: the huge token-registration function, see below.
- Every code constant of `game_layout.hpp` (70 of the 104; the other 34 are vtables, tokens and globals) is in the identical set and
  its address by function alignment equals the address `locate.py` found by its anchor. The vtable
  constants (19 of the header, plus CGameApplication, CSession, the three backends, CHuman and CNullEditBox)
  have the same slot count and every slot points to the same function (553 slots in the header's
  vtables; the one slot not mapped by function start, CProxyServer `+0x80`, is a thunk inside a larger
  block that moved by the same +0x37A0 as its neighbours).

## Method

1. `port.py locate --exe NEW`: all 136 anchors (104 header constants and 32 helpers) resolve, 0 problems.
2. `port.py triage`: 72 pins "moved", 154 "moved (short)", 1 "bytes changed" (`focus.2379F30`, 13/15 bytes), 4 "not
   found". The "short" candidates were not trusted. The one "bytes changed" pin is `mov rdx,rcx; mov
   rcx,[rip+d]; jmp rel32`: the RIP displacement and the rel32 are literal in the pin and both moved (a
   decode shows the same instructions; the RIP target is the focus manager global, see below). It is not
   a layout change. The four "not found" are the three `rebuild.case_*` jump-table entries and
   `host.get_session` (found by meaning: slot `0x88` of the new `CGameApplication` vtable).
3. Function index of both files and the alignment above (scratch tooling, now `tools/port/funcdiff.py`),
   then, for every pin:
   - the pin's function in the old file → the aligned function in the new file → the same offset, or the
     instruction aligned inside a changed region; the instructions covered by the pin are compared old
     against new with RIP/rel32 fields masked: they are equal for all 231 pins except the two stack
     displacements above (so every structure offset, immediate and flag of every pin is unchanged);
   - a unique long hit of the old pattern in the new file (triage) must equal the mapped address: it
     does for every pin that has one (0 disagreements);
   - short pins and pins in repeated code (27 copies of the `GetFiles` caller's body, templated registration
     blocks) are verified by their RIP/rel32 context, see the next point.
4. RIP/rel32 pins (66): the new displacement is read at the mapped instruction and the target is computed from
   the new bytes; it must equal the new value of the named constant (`locate.json`, RTTI, host or console
   constant) or, for a literal code target, the mapped address of the old target. All 66 do. The data
   literals were checked by content: the `Getting continent for %d` format string and the console's
   `Console not available in multiplayer or ironman` message are the same text at the moved addresses.
5. Constants: `game_layout.hpp` from `locate.json` (`port.py locate --check`: 136 resolved, 0 problems after the
   edit); host vtables by RTTI (`CGameApplication`, `CSession`, `CProxyServer`, `CNetworkServer`,
   `CDummyServer`, `CHuman`), `get_session` and the idler's session getter by their vtable slot `0x88`;
   console constants from the rip pins that reference them (two pins agree on the console singleton) and
   from the unique long hits of the console signatures.
6. `port.py --map map_1.20.0.0.json generate`: 231 patterns, all signatures unique in `.text`, no DIR64
   relocation inside any pin. `apply`, with the changes noted below.

### Two traps found on the way

- **Repeated code and the one-block shift.** `caption.F8915` and `caption.F7B47` sit in a registration
  function of 2,299 nearly identical blocks (`mov edx,[token]; call; lea r8,[name]; ...`). Alignment (and the
  unique hit of the masked pattern) placed both pins one block too early, on the neighbouring token
  (`diplomacy_tab` instead of `politics_tab`; the DWORD target `29CFCA4` holds `0x3AB3`, not `0x3AB2`). The
  RIP target of the first instruction was the tell. They were re-found by meaning: the block whose
  `lea r8` names the string `politics_tab` (new `F896C`, token DWORD `29CFCA0`) and `parent_window_token`
  (new `F7B9E`, token DWORD `2B35DDC`). Two registrations were removed from this function and two added
  (`blueprint_image`, `program` out; `dig_in_cap`, `naval_combat_result` in), which is what shifted the
  blocks. Lesson for the next port: a pin in a run of identical blocks is placed by the string or token
  value it is about, never by alignment.
- **Tool defect.** capstone reports `disp_size` 2 for the RIP-relative operand of some SSE instructions
  (`movdqa xmm1,[rip+d]`), so masking `disp_size` bytes leaves the high bytes of the displacement in the
  hash, and every function with such a load looked changed (with the 2-byte mask 82 % of the blocks looked identical, with all 4 bytes 97.5 %). The function index also must split only at 16-byte aligned
  starts after `int3` padding: an `0xCC` byte inside an operand is not a function boundary.

## Verified unchanged

- Structure offsets: every displacement of every instruction pin (all 148, plus the instructions of the
  83 signatures; the layout in `game_layout.hpp`, `host_probe.hpp`, `console_layout.hpp`), including the
  GUI/edit-box fields (window `+0xE0`/`+0xE8`/`+0x100`/`+0x108`, model `+0x28`/`+0x2C`/`+0x60`/`+0x80`, root
  `+0x13F0`/`+0x1380`, view `+0x30`/`+0x38`/`+0x5F0`/`+0x5F8`, edit `+0x281` flag, `+0x1A0`), country
  (`+0x218`, `+0x310`, `+0x31C`, `+0x350`, `+0x358`, `+0x1488`), faction, character, session (`+0x34`,
  `+0x3C`, `+0x40`, `+0x84`, `+0x380`), human (`+0x20`, `+0x98`, stride `0xA0`) and console entry
  (stride `0x1C8`, fields) offsets.
- Tokens: `politics_tab` `0x3AB2` at `29CFCA0`, `player_context` `0x3A70` at `2B35E34`,
  `parent_window_token` `0x3A6E` at `2B35DDC`.
- Vtable slots: slot `0x28`/`0x38`/`0x48` of the politics view/observer, `0xB8` idler GUI, `0xC0`/`0xC8` edit
  focus/unfocus, `0x2D8` idler in game, `0x88` session getters, and every other slot of the 26 vtables
  listed above.
- Globals: game state (`33E3B30`), idler (`33E3F88`), focus manager (`3507450`), sprite database (`3507360`),
  map (`33EDC98`), the six `NGraphics` colour defines (`33EB664` … `33EB968`), GUI structural global
  (`3103000`), console singleton slot (`36959D0`), `_tls_index` (`3699F28`). Each is the target of the
  RIP pin that reads it and of the anchor that finds it.
- RTTI: no CAPIE class name changed (`rtti_exe_test`), and no class of CAPIE lies in an anonymous namespace.

## What changed in the sources

- `map_1.20.0.0.json` (keys: 83 signatures, 148 instruction pins, 66 RIP pins, 104 `rva`, 9 `host`, 13
  `console`, `off` empty). Unlike the last two maps it covers every pin of the sources, including the
  catalog and portrait-path pins added in 1.7.0 that the identity map of 1.19.5.0 did not need.
- `apply` rewrote the pin tables and constants; its output was kept after three fixes by hand: it wraps
  patterns longer than 96 digits into several literals (the signature checker reads one literal per
  entry, so the originally single-line patterns were joined again) and it cut `colour.map_colors`,
  `colour.refresh` and `sprite.release_nolock` at the last whole instruction (their patterns end in a partial
  instruction on purpose; the old byte count was kept). `rebuild_mode_table`, the session getter of the idler
  and the three `rebuild.case_*` patterns were set by hand.
- `retest` rewrote 227 literals of the tests. It missed literals that are not pin addresses: two flip
  addresses inside `variable_find` / `variable_get` (`name_layer_pins_exe_test`), the rebuild table in the
  colour pin test, the fake-image page bases of `colour_layer_test` (`33DD000` → `33E3000`, `33E7000` → `33ED000`:
  they were page-aligned copies of the old globals' addresses), the fake image size of five editor tests
  (`3502000` → `3508000`: the focus manager global moved above it), the null edit box vtable of
  `public_editor_test` (`2BC8F30` → `2BCDC50`), and the `0x2379F3A` of `focus_release_test` (written with an `ll` suffix).
- Pin ids keep the addresses of 1.19.5.0 in their names (`caption.BC1700`, `focus.2379F30`, ...): they are
  labels. Comments name the function instead of an address; `tools/lint/check_comment_addresses.py` passes.
- `accepted-1.20.0.0.json` (the 47 signatures of `signature_table.hpp` and the constants of the checker),
  pinned in `check_signature_table.py` by the SHA-256 of the CRLF working copy (`7C79D74C…`).
- `game_build.hpp`: hash, `game_version` 1.20.0.0, `capie_release` 1.7.2, `capie_build_number` 10702; the
  launcher's `GameSha256`; the pinned-build strings of `capie_core` and the injector; the `CAPIE_BUILD` /
  `capie_ver` expectations of the tests; the changelog and the supported-build lines of the README, the
  site and the diagrams.

## Multiple builds

As before, the DLL accepts one `hoi4.exe`. CAPIE 1.7.2 supports 1.20.0.0 only and stays off on 1.19.5.0,
although every pin would also match the old file at its old addresses.

## Open

- In-game test on 1.20.0.0 (not done here). The offline and `*_exe_test` checks run against the real new file.
- Release pipeline: reviewed binary hashes (`deploy/bundle-inputs.json`), installer pins (`tools/bootstrap`,
  including its supported game hash), the Workshop texts and the GitHub release are written at release
  time and were not changed here.
