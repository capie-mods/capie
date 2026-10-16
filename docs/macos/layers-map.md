# macOS country, portrait, flag and host evidence ledger

Static analysis, 2026-10-16. Target SHA-256:
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.
All addresses below are **unslid VA**, not RVA (subtract `0x100000000` for RVA).
No game execution, hooks, mutations, or multiplayer checks were performed.
This is a partial map, not a complete or enable-ready pin table. Unknowns must fail closed.

## Proven instruction observations

These establish individual offsets in the identified call chains. They do not establish hook
coverage, byte-signature uniqueness, or the complete ABI of each function.

| Field | macOS value | Instruction VA | Exact bytes | Disassembly |
|---|---|---|---|---|
| `country_variables` | `0x1d8` | `0x10095c142` | `488b98d8010000` | `mov rbx, qword ptr [rax + 0x1d8]` |
| `country_variables (second path)` | `0x1d8` | `0x10095c5bd` | `488b98d8010000` | `mov rbx, qword ptr [rax + 0x1d8]` |
| `gamestate_countries` | `0x2d8` | `0x1011d7250` | `498b8c24d8020000` | `mov rcx, qword ptr [r12 + 0x2d8]` |
| `gamestate tag-to-index array` | `0x308` | `0x1011d7240` | `488b8008030000` | `mov rax, qword ptr [rax + 0x308]` |
| `gamestate tag mapping read` | `int32` | `0x1011d7247` | `8b0488` | `mov eax, dword ptr [rax + 4*rcx]` |
| `country_politics` | `0xd38` | `0x10117c434` | `488b87380d0000` | `mov rax, qword ptr [rdi + 0xd38]` |
| `politics_ruling_party` | `0xd0` | `0x100fbef1d` | `488bb8d0000000` | `mov rdi, qword ptr [rax + 0xd0]` |
| `party_leaders` | `0x60` | `0x101366a7a` | `488b7f60` | `mov rdi, qword ptr [rdi + 0x60]` |
| `party_leader_count` | `0x6c` | `0x101366a74` | `837f6c00` | `cmp dword ptr [rdi + 0x6c], 0x0` |
| `leader_role_character` | `0x8` | `0x101300d14` | `488b4708` | `mov rax, qword ptr [rdi + 0x8]` |
| `character portraits` | `0x60` | `0x100d5dd21` | `498d5560` | `lea rdx, [r13 + 0x60]` |
| `character_variables` | `0xd0` | `0x100d5df73` | `4d8dbdd0000000` | `lea r15, [r13 + 0xd0]` |
| `set_variable scoped variable` | `0x50` | `0x101060800` | `4883c750` | `add rdi, 0x50` |
| `scoped variable string` | `0x70` | `0x10095d9bd` | `f6467001` | `test byte ptr [rsi + 0x70], 0x1` |
| `variables_entries` | `0x10` | `0x100b063c9` | `488b0f` | `mov rcx, qword ptr [rdi]` |
| `variables_mask` | `0x1c` | `0x100b063b6` | `448b670c` | `mov r12d, dword ptr [rdi + 0xc]` |
| `variable_entry_size` | `0x28` | `0x100b063f7` | `4883c328` | `add rbx, 0x28` |
| `variable_entry_probe` | `0x4` | `0x100b063d6` | `807cc10400` | `cmp byte ptr [rcx + 8*rax + 0x4], 0x0` |
| `variable_entry_key` | `0x8` | `0x100b06400` | `0fb64308` | `movzx eax, byte ptr [rbx + 0x8]` |
| `variable_entry_value` | `0x20` | `0x100b03424` | `4883c020` | `add rax, 0x20` |
| `app_session` | `0x320` | `0x10062e904` | `488b8720030000` | `mov rax, qword ptr [rdi + 0x320]` |

The first variable-table proof uses the insertion path; the read-only path independently
confirms it in the variable-reader section below: `0x100962aa0` builds the scoped name, calls `0x100b03370` at `0x100962bee`;
`0x100b03370` hashes the byte span with 32-bit FNV-1a, adds `0x10` to the container
at `0x100b03417`, and calls the table operation `0x100b06390` at `0x100b0341f`.
That operation uses mask `table+0xc`, array `table+0`, 40-byte entries, probe byte +4,
and libc++ string +8. The caller returns entry+0x20. `variables_extra=0x20` is established by the read-only lookup below.

`set_variable_name = 0xc0` follows from effect+0x50 and scoped-variable+0x70.
The name is a libc++ string: flag byte +0x70, long length +0x78 and long pointer +0x80
(`0x10095d9bd`, `0x10095d9f6`, `0x10095d9fb`). Windows size/capacity pins cannot be reused.

## Identified functions and RTTI

| Purpose | macOS unslid VA | Evidence and limitation |
|---|---|---|
| `CSetVariableEffect` vtable | `0x103366ab8` | RTTI exact `18CSetVariableEffect`; slot +0x70 contains `0x1010607f0` |
| Set-variable execution body | `0x1010607f0` | effect+0x50 passed to scoped-variable resolver at 0x101060806; result pointer receives int64 at 0x101060844. Runtime interception coverage unproven |
| Country lookup used by scoped variables | `0x1011d7110` | calls at 0x10095c13d and 0x10095c5b8; returned object+0x1d8 read immediately |
| Const country lookup used by portrait effect | `0x1011d6fb0` | call at 0x100fbef10; same remapping and country-array logic |
| Game-state global | `0x103519740` | 0x1011d7074 `lea r13,[rip+0x23426c5]`, followed by `mov r12,[r13]` |
| `CCharacter` vtable | `0x10332efe0` | RTTI `10CCharacter`, offset-to-top 0 |
| `CCharacter` serialization writer | `0x100d5dbd0` | slot +0x18; retains character in r13 at 0x100d5dbe4; token 0x4c29 at 0x100d5dd28 identifies portraits +0x60; token 0x2a4a at 0x100d5df89 identifies variables +0xd0 |
| Character serialization reader | `0x100d5cdd0` | slot +0x28; token 0x2a4a tested at 0x100d5cede; reader this/token/field chain confirmed in concrete manifest section |
| Country leader portrait effect body | `0x100fbeeb0` | RTTI template `42CSetCountryLeaderNamePortraitOrDescriptionIL30ENamePortraitOrDescriptionType1EE`, vtable `0x10334e6c8`, slot +0x70 |
| Politics getter | `0x10117c430` | called at 0x100fbef18, loads country+0xd38 |
| First leader role | `0x101366a70` | called 0x100fbef24; count +0x6c, data +0x60, jump 0x101366a7f to `0x100d82900` (dereference [rdi]) |
| Role character getter | `0x101300d10` | called at 0x100fbef31, returns [role+8] |
| Character portrait getter | `0x100d5edf0` | called 0x100fbef42, returns character+0x60; independently confirmed by writer token |
| Portrait write | `0x100d6f780` | call 0x100fbef71, `xor esi,esi` at 0x100fbef67 and `mov edx,1` at 0x100fbef69: civilian/large (0,1); writer creates pair(type,size), calls `0x100d6f850` |
| Faction vtable | `0x10333ccd0` | RTTI `N9NFactions8CFactionE`; GetName is established below at `0x100e527e0` |
| Application vtable | `0x1032ff870` | RTTI `16CGameApplication`; secondary `0x1032ff920`, top -8 |
| Application session getter | `0x10062e900` | primary vtable slot +0x90, loads [rdi+0x320]; constructor-to-CSession chain proven below |
| Session vtable | `0x1034a7710` | RTTI `8CSession` |
| Proxy backend vtable | `0x1034a71a0` | RTTI `12CProxyServer` |
| Network backend vtable | `0x1034a7078` | RTTI `14CNetworkServer` |
| Dummy backend vtable | `0x1034a75d8` | RTTI `12CDummyServer` |
| Human vtable | `0x103308f18` | RTTI `6CHuman` |

**Country tag is not proven to be an atlas index.** The two country lookup functions read
`tag = int32[*arg]`, then map it through `[gamestate+0x308][tag]`, then index the pointer
array at `gamestate+0x2d8`. This directly rules out blindly porting the Windows identity
assumption to this path. The flag-atlas indexing loop and tag remapping are established in the flag section below.

RTTI lookup caveat: the original helper requires a NUL before the mangled name. The
CGameApplication and CProxyServer names immediately follow non-string constants, so that
condition misses them. For these rows the exact mangled name followed by NUL was located
inside `__TEXT,__const`, then pointer references and offset-to-top were verified using the
helper. No class identity was inferred from nearby text alone.

## Current completion boundary

`layers-map.json` is the machine-readable map. Its evidence labels describe native
addresses/layouts and explicit adapter requirements; the consolidated counts are in
`address-map.md`. This is not runtime validation or an enable-ready complete pin table.

| Remaining item | Current status |
|---|---|
| `core::rva::string_copy` | No one-to-one callee established: faction name copy is inline. Use a separately validated libc++ game-heap adapter; do not reuse the fallback localisation helper. |
| `core::rva::variable_find` | Static evidence-only pin at RVA `0xb031c0`; Windows marks it pinned, never called. Native value/fallback-pointer return differs from Windows iterator; runtime integration remains disabled. |
| `host::layout::app_global` | Proven RVA `0x3639ad8`: CGameApplication vslot +0x40 writes this; see constructor and initializer evidence below. |
| `host::layout::app_session` | Proven offset `0x320`: constructor stores a newly constructed CSession; vslot +0x90 returns it. |
| Host-only command and idler pins | Host-ID command predicate, mode-enum interpretation, lobby server check and full ingame-idler session-slot proof remain incomplete. |
| Hook validation | Full signature/call-site coverage, ABI/ownership adapters, fail-closed executable pin tables and in-game behavior remain unverified. |

Country update/build, faction accessor, flag update/load/candidate builder, portrait resolver
and native validity functions are identified below. Faction first-member leadership, variable
read layout, country count and human-name fields are established. Individual named Windows
pins still require equivalent executable macOS validation, even when their underlying address
or field is known. The main inventory tracks that separate requirement.

## Reproduction and remaining checks

Read-only helper: `docs/macos/re/macho.py`; dump produced by llvm-objdump as described in
`re-notes.md`. Use `fdis(VA, endVA)` for the instruction and `raw(VA, length).hex()` for bytes.
The existing analysis dump was reused; exact bytes above were read from the binary.

Before enabling a layer: prove every pending pin, establish SysV return/argument ABI and
libc++ ownership, verify exact build hash and full prerequisite set before installing any hook,
and test game start/save/load/language changes plus feature-specific behavior. Host detection
requires the two-client check already required by the Windows design. No in-game result is
claimed by this document.

## Portrait resolver

Complete byte-pin validation, all callers and runtime coverage are still pending.

Resolver: **`0x100d6fbe0`**, direct GUI caller **`0x1023d3340`**.
SysV register ABI established from caller and callee:
`rdi=out entry`, `rsi=portrait map`, `edx=type`, `ecx=size`, `r8=character`,
`r9=fallback CString*`. Return `rax=out` at `0x100d6ff06`.
In particular, fallback is a libc++ string object passed to `0x100757c50` at
`0x100d6febe`; do not reuse a Windows `const char*` typedef.

The GUI selects `GFX_idea_unknown` for size==0 and `GFX_leader_unknown` otherwise
at `0x1023d338c`..`0x1023d33a5`. It obtains the map via `0x100d5ee00`, uses caller-local
entry storage at `[rbp-0x58]`, passes the character in r8 (`0x1023d33d1`), calls resolver
at `0x1023d33db`, and calls validity `0x1007581a0` at `0x1023d33e3`.
The resolver saves character r8 in r15 at `0x100d6fbf7`; `[r15+0xc]` at `0x100d6fd01`
establishes character ID. It checks the first map entry and then constructs fallback portraits.

Validity is more than a nonempty sprite-name check: it tests flag byte +0, kind +4,
for kind 1 requires nonzero handle +0x20, for kind 0 finds sprite by string +8 through
`0x1027b7b30` and global `0x1035dc028` (RIP calculation: next instruction
`0x1007581df` + displacement `0x2e83e49`). If gamestate exists and its +0x9b9 byte is
set, it additionally invokes sprite vslot +0x90 and requires nonzero result
(`0x100758269`..`0x100758281`). Preserve the native predicate, not a simplified clone.

| Observation | VA | Exact bytes | Instruction |
|---|---|---|---|
| resolver character | `0x100d6fbf7` | `4d89c7` | `mov r15, r8` |
| character ID | `0x100d6fd01` | `418b470c` | `mov eax, dword ptr [r15 + 0xc]` |
| entry flag | `0x1007581af` | `803f00` | `cmp byte ptr [rdi], 0x0` |
| entry kind | `0x1007581bb` | `8b4704` | `mov eax, dword ptr [rdi + 0x4]` |
| entry handle | `0x1007581cb` | `48837e2000` | `cmp qword ptr [rsi + 0x20], 0x0` |
| entry name | `0x1007581e2` | `4883c608` | `add rsi, 0x8` |
| sprite database | `0x1007581d8` | `488d05493ee802` | `lea rax, [rip + 0x2e83e49]` |
| sprite lookup call | `0x1007581e6` | `e845f90502` | `call 0x1027b7b30` |
| UI resolver call | `0x1023d33db` | `e800c899fe` | `call 0x100d6fbe0` |
| UI validity call | `0x1023d33e3` | `e8b84d38fe` | `call 0x1007581a0` |

## Flag atlas

Identified update function **`0x10194a920`**, incremental rebuilder **`0x101949d20`**,
slot loader **`0x10194a460`**, candidate builder **`0x100257750`**.
Loader has exactly two direct calls in the full dump: incremental `0x10194a1fd`
and threaded `0x10194bdc1`. Update compares the per-slot cosmetic qword at atlas+0xc0,
checks count +0xcc, loops country pointer array gamestate+0x2d8 with the same r15 index,
and calls incremental rebuild or full builder `0x101947f20`.

Candidate call at `0x10194a4e2`: out vector rdi, tag string rsi, cosmetic string rdx,
group rcx. Country cosmetic string is +0x1200. The builder initializes a custom
inline-buffer vector, not std::vector. Its output ABI and destructor must be ported too.
Update has a void-style epilogue; Windows `u64` return type is not validated for macOS.

The raw country tag at +8 is **not the atlas slot**: incremental writer calls
`0x1011d76d0` at `0x10194a12b`, which remaps through gamestate+0x308 at
`0x1011d7742`..`0x1011d7750`, then writes `[atlas.cosmetic_keys + index*8]`
at `0x10194a14f`. The update loop's r15 directly indexes both country pointers and keys.

| Observation | VA | Bytes | Instruction |
|---|---|---|---|
| cosmetic keys | `0x10194ad8a` | `498b86c0000000` | `mov rax, qword ptr [r14 + 0xc0]` |
| qword compare | `0x10194ad91` | `4a391cf8` | `cmp qword ptr [rax + 8*r15], rbx` |
| count | `0x10194aa80` | `413986cc000000` | `cmp dword ptr [r14 + 0xcc], eax` |
| country list | `0x10194ac3a` | `488b80d8020000` | `mov rax, qword ptr [rax + 0x2d8]` |
| slot loop | `0x10194ac41` | `4e8b24f8` | `mov r12, qword ptr [rax + 8*r15]` |
| cosmetic argument | `0x10194a4ac` | `4d8da600120000` | `lea r12, [r14 + 0x1200]` |
| candidate call | `0x10194a4e2` | `e869d290fe` | `call 0x100257750` |
| incremental load | `0x10194a1fd` | `e85e020000` | `call 0x10194a460` |
| threaded load | `0x10194bdc1` | `e89ae6ffff` | `call 0x10194a460` |

Faction member evidence: `0x100e50340` checks membership before adding a country; assertion text
`!_Members.Contains( &Country )` at `0x100e50416` corroborates the loop over pointers
at faction+0x48 (`0x100e503de`) and count +0x54 (`0x100e503d5`). The leader-is-first invariant and faction accessor are established in the faction section below.

## Host probe

`CSession::Init = 0x1026d3c20` is called by both RTTI-established CSession constructors
`0x1026d35c0` and `0x1026d3d40`. Init receives session rdi, backend rsi, mode edx.
It computes `(mode-1) < 2` unsigned into +0x54 and stores mode at +0x4c, backend +0x58.
It stores `0x100000002` at +0xa0: lower int32=2, upper int32 at +0xa4=1.
This proves initial machine ID=1, not yet that every host-only command uses ID1.
Machine-ID getter `0x1026d5690` reads +0xa4; setter `0x1026d56a0` writes it and logs
"Machine id ... assigned" with source `clausewitzlib/session.cpp`.

Local-human getter **`0x1006e7e60`** gets a session via vslot **+0x90**, calls machine-ID
getter, scans humans at gamestate+0xe0, count+0xec, stride0x90, matching human+0x88.
On no match it returns embedded fallback human at gamestate+0xf8. Never treat that fallback
as a proven host. The sibling `0x1006e7ed0` reports whether such a matching human exists.
CHuman writer `0x1007579f0` serializes string+0x20 under token0xe2; direct USER_NAME
formatting provenance in the additional evidence section below establishes name identity.

**Two different singletons:** local-human reads `0x1035196c0` at `0x1006e7e69`, whereas
`0x102674450` reads `0x103639ad8`. The latter is CApplication::AccessInstance storage, established by the initializer and
constructor chain below. They are distinct storage locations; this does not prove they
always hold the same instance during startup/shutdown.

| Observation | VA | Bytes | Instruction |
|---|---|---|---|
| mode | `0x1026d3c6f` | `89574c` | `mov dword ptr [rdi + 0x4c], edx` |
| server | `0x1026d3c5d` | `0f924754` | `setb byte ptr [rdi + 0x54]` |
| backend | `0x1026d3c8f` | `48897758` | `mov qword ptr [rdi + 0x58], rsi` |
| initial pair | `0x1026d3c75` | `48b80200000001000000` | `movabs rax, 0x100000002` |
| pair store | `0x1026d3c7f` | `488987a0000000` | `mov qword ptr [rdi + 0xa0], rax` |
| machine id | `0x1026d5694` | `8b87a4000000` | `mov eax, dword ptr [rdi + 0xa4]` |
| humans | `0x1006e7e93` | `488b8be0000000` | `mov rcx, qword ptr [rbx + 0xe0]` |
| count | `0x1006e7e89` | `8b93ec000000` | `mov edx, dword ptr [rbx + 0xec]` |
| human machine | `0x1006e7ea0` | `398188000000` | `cmp dword ptr [rcx + 0x88], eax` |
| stride | `0x1006e7ea8` | `4881c190000000` | `add rcx, 0x90` |

Independent fresh llvm-objdump (without --macho) rechecked flag key load/compare at
`0x10194ad8a`..`0x10194ad95`, matching the reused dump and exact raw bytes.

## Country name entry points

`update_names = 0x10117a790`, `build_name_key = 0x101177f30`.
Cosmetic setter `0x1011ca6a0` compares and assigns country+0x1200; its assignment at
`0x1011ca76a` is immediately followed by calling `0x10117a790` at `0x1011ca772`.
Update takes country in rdi (retained in rbx), copies cached strings at country+0x10/+0x28,
and handles `_ADJ`, `_DEF`, `NONIDEOLOGY*`, and `OVERLORD*` name variants.
It obtains the cosmetic tag at `0x10117abe1` and passes it in rsi to name builder at
`0x10117ac2d`. Builder receives output pointer in rdi, tag string rsi, other strings/pointers
in rdx/rcx/r8/r9 and a stack pointer plus byte flag. It contains `_subject` and `COUNTRY`
fallback paths. The entry-point correspondence is established; complete hook-call coverage,
return ABI and all ownership details are not.

| Observation | VA | Bytes | Instruction |
|---|---|---|---|
| setter calls update | `0x1011ca772` | `e81900fbff` | `call 0x10117a790` |
| update tag address | `0x10117abe1` | `4c8db300120000` | `lea r14, [rbx + 0x1200]` |
| update tag argument | `0x10117ac2a` | `4c89f6` | `mov rsi, r14` |
| update calls builder | `0x10117ac2d` | `e8fed2ffff` | `call 0x101177f30` |

## Faction and variable reader

Faction GetName is **`0x100e527e0`**, SysV **out=rdi, faction=rsi**. Nonempty faction+0x18
is copied inline as libc++ string; absent text falls back through faction+0x450 and
its +0xb8 member into `0x100f9b2a0`. That callee prepares a temporary record vector and
calls localisation `0x102884970` at `0x100f9b31a`. It is **not** a generic string-copy
constructor. Thus the Windows `string_copy` pin has no one-to-one call site on the primary
name path; implementing a game-heap libc++ copy adapter is required. The long-name copy
in GetName calls game allocator `0x102a7fbd0` at `0x100e52871`, writes libc++ fields and
calls memmove at `0x100e52891`. Do not label the fallback localisation helper as string_copy.
The set-faction-name effect `0x100db7a20` writes faction+0x18 at `0x100db7a9e`..`0x100db7aa9`.

Member leader-first invariant is now proven: RTTI CSetFactionLeaderEffect vtable
`0x103331200` slot+0x70 = `0x100db7430`. It loads members at `0x100db7460`, dereferences
first member at `0x100db7464`, reads country tag+8 at `0x100db7467`, and compares it with
the proposed leader before calling leader replacement at `0x100db749f`.
Together with the earlier serialization and membership loop, offsets +0x48/+0x54 are proven.

Variable getter **`0x100b03190`** takes `(container*, span*, int64 fallback)` and returns
int64 in rax. It adds +0x10 for the table and calls **`0x100b031c0`**, which contains the
whole FNV-1a/find loop and returns value pointer or caller-supplied fallback pointer.
This is not the Windows split between an iterator-returning find and output-pointer getter.
The `variable_find` JSON entry is static evidence for its pinned-never-called role; the
instructions and table semantics are proven. A faithful port should reproduce/read this layout
or adapt the native signature instead of calling it through the Windows typedef.

Read-only lookup independently confirms: array container+0x10, mask+0x1c, extra byte+0x20,
entry stride0x28, probe byte+4, key libc++ string+8, int64 value+0x20. Empty probe is zero;
failed lookup computes end=(mask+extra+1)*stride and selects fallback. Key comparison checks
length and then memcmp (call at `0x100b03307`), not hash alone.

| Observation | VA | Bytes | Instruction |
|---|---|---|---|
| faction name flag | `0x100e527ee` | `0fb64618` | `movzx eax, byte ptr [rsi + 0x18]` |
| faction template | `0x100e52832` | `488bb650040000` | `mov rsi, qword ptr [rsi + 0x450]` |
| leader first array | `0x100db7460` | `498b4648` | `mov rax, qword ptr [r14 + 0x48]` |
| leader first deref | `0x100db7464` | `488b38` | `mov rdi, qword ptr [rax]` |
| leader country tag | `0x100db7467` | `8b4708` | `mov eax, dword ptr [rdi + 0x8]` |
| read table offset | `0x100b0319c` | `4883c710` | `add rdi, 0x10` |
| get calls lookup | `0x100b031a4` | `e817000000` | `call 0x100b031c0` |
| get value | `0x100b031a9` | `488b00` | `mov rax, qword ptr [rax]` |
| read mask | `0x100b03287` | `8b470c` | `mov eax, dword ptr [rdi + 0xc]` |
| read entries | `0x100b03294` | `4c8b2f` | `mov r13, qword ptr [rdi]` |
| read extra | `0x100b03314` | `0fb64010` | `movzx eax, byte ptr [rax + 0x10]` |
| value pointer | `0x100b03354` | `498d4620` | `lea rax, [r14 + 0x20]` |
| fallback select | `0x100b0335b` | `480f4445c0` | `cmove rax, qword ptr [rbp - 0x40]` |

## Country count, human name and singleton evidence

`gamestate_country_count = 0x2e4`: RTTI `CGameState` primary vtable `0x103306308`,
slot+0x68 points to `0x1006ee530`, whose `0x1006ee534` instruction is
`mov eax,[rdi+0x2e4]`. Atlas update calls this vslot at `0x10194aa63`.

`human_name = 0x20` is now proven: at `0x100692988` the human array is read from +0xe0;
the loop compares machine ID at +0x88 (`0x1006929a0`), forms the selected pointer
(`0x1006929ba`), adds +0x20 (`0x1006929be`), and passes that as `USER` for the
`USER_NAME` formatting call (`0x1006929e4`/`0x1006929fd`). Independent UI path:
`0x101f12f91` count+0xec, `0x101f12fa1` data+0xe0, `0x101f12ffc` selected human+0x20,
then `0x101f13034` USER_NAME and `0x101f1304d` r8=name.

Singleton distinction strengthened: `0x1035196c0` is written by `0x100696660`,
`0x100696680`, `0x10069e4d0`; it is used for large ingame virtual interfaces (+0x390,
+0x2a0). The separate application singleton `0x103639ad8` is written by
`0x102675944` in CApplication initialization. Do not replace one by the other merely
because both expose a session slot.

## Bounded localisation trace from faction GetName

The observed direct chain is:
`0x100e527e0` -> call `0x100f9b2a0` at `0x100e52848` -> call `0x102884970`
at `0x100f9b31a`. `0x102884970` calls `0x102887ae0` at `0x102884a0a` and
`0x102887be0` at `0x102884a25`. Neither of those two inspected functions directly calls
`loc_lookup_z` (`0x10288d420`) or `loc_lookup_n` (`0x10288da90`).

The first reaches `0x102887e60` (calls `0x102887b4d`/`0x102887bb0`); the second
recurses and calls `0x102887f10`, `0x1028880b0`, `0x1028882a0`, and `0x102899490`,
with additional indirect dispatches. This bounded trace does **not** establish whether
all eventual paths reach n/z, nor prove that they bypass them. Those transitive callees
and indirect targets need the localisation coverage analysis. Three-hook coverage cannot
be claimed from this trace.

## Completed application chain and adapter decisions (2026-10-16)

CGameApplication constructor installs primary RTTI vtable `0x1032ff870` at
`0x10060df09`/`0x10060df10`. Its slot +0x40 is `0x102675940`, whose instruction
`0x102675944` stores the unadjusted this pointer into `0x103639ad8`. The accessor
`0x102674450` reads that location. The same primary vtable's +0x90 getter is
`0x10062e900`, reading this+0x320. Constructor code allocates a session, preserves it
in r15, calls `0x1026d4680` at `0x10060e109`, then stores r15 at application+0x320
at `0x10060e10e`. The thunk jumps to RTTI-established CSession constructor
`0x1026d3d40`. This closes static app_global/app_session identity; it does not prove
multiplayer host authorization or lifetime safety. The adapter must null-check the
application/session and verify expected vtables before reading session state.

| Observation | VA | Bytes | Instruction |
|---|---|---|---|
| application vtable | `0x10060df09` | `488d056019cf02` | `lea rax, [rip + 0x2cf1960]` |
| install application vtable | `0x10060df10` | `488903` | `mov qword ptr [rbx], rax` |
| construct session | `0x10060e109` | `e872650c02` | `call 0x1026d4680` |
| store session | `0x10060e10e` | `4c89bb20030000` | `mov qword ptr [rbx + 0x320], r15` |
| session constructor thunk | `0x1026d4685` | `e9b6f6ffff` | `jmp 0x1026d3d40` |
| publish application | `0x102675944` | `48893d8d41fc00` | `mov qword ptr [rip + 0xfc418d], rdi` |

**Variable adapter decision:** Windows variable_find/get are evidence pins, never called
by CAPIE (`game_layout.hpp`). Preserve that design: use the macOS read-only table layout
with a platform string-view decoder, and pin the integrated native lookup as corroborating
evidence. Do not invent a Windows iterator ABI. If a future caller needs native lookup,
its signature is `(table*, byte_span*, int64_t* fallback) -> int64_t*`; pass container+0x10
and detect absence by pointer equality with the fallback address, not value equality.
The native lookup is classified static under the Windows symbol ID only for its evidence-pin
role. No callable ABI compatibility or implementation integration is established;
`runtime_ready=false`. The layout evidence is proven separately.

**String adapter decision:** no generic game copy constructor address is required to
implement the behavior. Use an explicit macOS copy-construction adapter for the observed
24-byte libc++ representation. Decode source length/data, copy the inline form for short
strings, and allocate through the verified game allocator for long strings, then copy
length+1 bytes and write the long representation. Never copy the 24-byte long form
verbatim (that aliases ownership); never allocate with CAPIE's allocator. The exact
capacity rounding/overflow behavior must be taken from the inline GetName copy at
`0x100e52850`–`0x100e52891`, with allocation at `0x100e52871`; this decision alone does
not certify an implementation. Destination must be uninitialized copy-construction
storage; overwriting a live string requires destruction first. Keep string_copy absent
from address JSON rather than inventing an RVA. Required offline checks: empty/SSO-boundary/
long strings, embedded NUL, independent ownership, and allocation failure; game-heap
allocation/destruction compatibility still requires a runtime check.

## Concrete static pin manifest

`layers-pins.json` contains fresh binary bytes: 11 function signatures, each ending on
an instruction boundary and occurring exactly once in `__TEXT,__text`; 90 instruction
pins; 19 decoded rel32/RIP target pins; and 7 RTTI-vtable entries. Exact patterns are tied
to the SHA above, with no wildcard promises across builds. Vtable file bytes are unslid:
in process, compare rebased pointers, not the original eight-byte sequence.

The manifest enumerates all 65 named Windows pins in the three country name/portrait/flag
headers. Each has static counterpart evidence or an explicit changed-mechanism decision:
string copy uses the owned-string adapter, tag indexing uses remapping, and variable find
is an evidence-only integrated lookup. This accounts for names, not activation readiness.
It does not establish detour relocation safety, all callers, hook semantic equivalence,
complete ABI, runtime memory validity, or in-game behavior. Short offset instructions need
the surrounding call-chain and signature evidence; their individual uniqueness is not claimed.

Additional read-path provenance: CCharacter reader saves this in r13 at `0x100d5cde7`,
compares token 0x2a4a at `0x100d5cede`, and advances that object pointer by 0xd0 at
`0x100d5cf20`. Portrait UI saves incoming size in r12d at `0x1023d3354`, tests it at
`0x1023d338c`, chooses GFX_idea_unknown for zero at `0x1023d33a5`, and passes size in ecx
at `0x1023d33ce`. Out buffer is rbp-0x58 (`0x1023d33c1`), passed in rdi at
`0x1023d33c5`; character is passed in r8 at `0x1023d33d1`. These are macOS SysV
replacement instructions, not Windows stack argument offsets.
