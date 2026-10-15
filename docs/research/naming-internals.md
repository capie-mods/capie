# HOI4 naming internals

Reverse-engineering reference for `hoi4.exe` 1.19.3.0 (Windows x64): how the game stores, localises, saves and refreshes country, character and state names, and how the edit box and the command queue work. Investigation date: 2026-10-11. Original notes (Polish): [monorepo archive: docs/evidence/research/naming-internals.md](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/research/naming-internals.md).

## 0. Build, method and conventions

| | |
|---|---|
| File | `hoi4.exe` 1.19.3.0, 55 778 424 bytes, x64 MSVC, no PDB |
| SHA-256 (pinned) | `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5` |
| ImageBase | `0x140000000`; every address in this document is an RVA |
| Sections | `.text` 0x1000, `.rdata` 0x2707000, `.data` 0x3085000, `.pdata` 0x35E8000 (138 286 entries) |

Method:

- The analysis ran on a copy of the executable in a scratch directory. The game directory was not modified and the game was not running during static analysis.
- A Python toolkit (pefile, capstone, numpy) provided function boundaries from `.pdata`, MSVC RTTI (18 956 types, class names and vtables resolved through the Complete Object Locator), RIP-relative xrefs, E8/E9 calls, source file paths from assertions, and the lexer token table (10 761 id-to-string pairs).
- Ghidra 12.1.4 headless: import without auto-analysis, then decompilation of selected functions.
- Signatures: bytes from the start of the function, with wildcards on RIP-relative disp32 and on call/jmp/jcc rel32. A script checked each one as the only match in `.text` at the expected RVA. Full list: [naming-signatures.json](naming-signatures.json).
- Observation script for Frida (only `Interceptor.attach` and memory reads): [plugin/tools/verify.js](https://github.com/capie-mods/capie/blob/main/plugin/tools/verify.js).

Evidence tags:

- **CONFIRMED-STATIC**: seen in disassembly or decompilation (RVA or function given).
- **CONFIRMED-LIVE**: verified in the running game. In the 2026-10-11 run, `LookupKeyN`, `LookupKeyZ` and `KeyExists` passed the unique-signature check at the expected RVAs and the hooks installed in PID43732. This confirms the entry points. It does not confirm full UI, save/load or MP semantics; those test results are recorded separately in [in-game-test-record.md](../contributing/in-game-test-record.md).
- **HYPOTHESIS**: an inference that has not been confirmed directly.

Conventions (CONFIRMED-STATIC): Microsoft x64 calling convention (`rcx, rdx, r8, r9`, `this` in `rcx`). MSVC `std::string`: `+0` SSO buffer[16] or pointer, `+0x10` size, `+0x18` capacity (heap allocated when > 15).

Globals (CONFIRMED-STATIC, RIP-relative reads in the listed functions):

| RVA | Meaning |
|---|---|
| `0x35BA038` | `CPdxLocalize*` localisation manager (`PdxLoc_LookupKeyN`) |
| `0x332F260` | `CGameState*` (assertion in `gamestate.h`: "gamestate unitilialized") |
| `0x332F698` | ingame interface handler (vcall `+0xB8`, `+0x88`, `+0x2D0`) |
| `0x333CFB8` | object holding the map-name refresh flags (bytes `+0x81/+0x82`) |

## 1. Localisation core (key to text)

### 1.1 Flow

```
 GUI / effects / tooltips  (9 459 Localize call sites)
        |
        v
 Localize(std::string* out, const char* key, SReplace* args, int nArgs)      0x2245E60
        |  scratch buffer 0x1800 B, no cache
        v
 Loc_ExpandRecursive(char** cur, char* end, key, keyEnd, rootKey,            0x2245880
                     args, nArgs, depth<32)
        |-- txt = PdxLoc_LookupKeyN(key, len)                                0x239EE20
        |      FNV-1a 64 -> tail-jmp PdxLoc_FindByHash                    0x239ED90
        |-- txt == NULL and key == rootKey  -> copies the key itself (fallback)
        |-- $VAR$  -> substitution from args (0x2248C20) or recursion on the key
        v
 out = UTF-8 text (with control codes 0x11..0x1A)
        |
        v  (separately, for GUI texts)
 ContextLoc_Evaluate  [Scope.Property]                                       0x12CED60
```

### 1.2 Functions

| RVA | Working name | Prototype | Tag |
|---|---|---|---|
| `0x239ED90` | `PdxLoc_FindByHash` | `const char* (LocTable* t, uint64 hash)` | CONFIRMED-STATIC |
| `0x239EE20` | `PdxLoc_LookupKeyN` | `const char* (const char* key, int len)` | CONFIRMED-STATIC |
| `0x239ED40` | `PdxLoc_LookupKeyZ` | `const char* (const char* key)` (NUL-terminated) | CONFIRMED-STATIC |
| `0x239D150` | `PdxLoc_LookupHash` | `const char* (uint64 hash)` (precomputed hash; used by GUI 0x22DE010/0x22DE040) | CONFIRMED-STATIC |
| `0x239D0F0` | `PdxLoc_KeyExists` | `bool (const char* key)` | CONFIRMED-STATIC |
| `0x2245E60` | `Localize` | `std::string* (std::string* out, const char* key, void* args, int nArgs)` | CONFIRMED-STATIC (assertion in `localize.cpp` "pStr") |
| `0x2245880` | `Loc_ExpandRecursive` | 8 arguments, recursive, depth limit 0x20 | CONFIRMED-STATIC |
| `0x23A1100` | `PdxLoc_EncodeValue` | writes a value into the blob using the replacement table | CONFIRMED-STATIC |
| `0x12CED60` | `ContextLoc_Evaluate` | evaluates `[...]` (textbase.cpp), 19 callers | CONFIRMED-STATIC (name: HYPOTHESIS) |

The only readers of `0x35BA038` are functions in the range 0x239A1C0..0x23A24E6 (module `clausewitz\pdx_localize`, 65 xrefs).

Correction to the first Ghidra reading (2026-10-11): `LookupKeyN` and `LookupKeyZ` compute the hash and tail-jump to `PdxLoc_FindByHash` (N: `0x239EE6A`, Z: `0x239ED88`). Ghidra placed the body of the tail-jump target into the pseudocode of both wrappers. Their own instructions do not contain an inlined search. `KeyExists` makes an ordinary call to `FindByHash`. The wrapper `0x2244840` passes the first pointer of a string_view pair to `KeyExists`. Deciding evidence: [raw Capstone for N/Z/Exists/FindByHash and the consumer](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/localization-abi-capstone.txt).

### 1.3 Structures (CONFIRMED-STATIC, from decompiling 0x239ED90, 0x239C290, 0x239E120, 0x239BA30)

```
CPdxLocalize (M = *(void**)0x35BA038)
  +0x00  LocTable* current          // active language
  +0x08  LocTable** languages; +0x14 int count   (lookup by language name: 0x239C090)
  +0x20  char* blob; +0x28 int capacity; +0x2C int size
  +0x40  ReplaceEntry* repl; +0x4C int replCount
LocTable
  +0x20  {uint64 hash; int32 offset; int32 pad}[]  sorted by hash
  +0x2C  int count
  +0x38  {uint64 hash; int32 keyOff; int32 valOff; int32 ...}[] (0x18 B); +0x44 int count
```

Pseudocode of `PdxLoc_FindByHash`:

```c
const char* FindByHash(LocTable* t, uint64_t h) {
    if (t->count == 0) return NULL;
    Entry* e = lower_bound(t->entries, t->entries + t->count, h);
    if (e != end && e->hash == h) return M->blob + e->offset;
    return NULL;
}
// hash = FNV-1a 64: h=0xCBF29CE484222325; for each byte: h=(h^b)*0x100000001B3
```

### 1.4 Text encoding

- The manager constructor (0x239BA30) registers character replacements: `0xA7`(§)->0x11, `@`->0x12, `0xA3`(£)->0x13, `0xA4`(¤)->0x14, `{`->0x1A [CONFIRMED-STATIC]. All other bytes from `.yml` files pass through unchanged, so the stored text is UTF-8 with control codes [HYPOTHESIS as to full UTF-8 support, since it depends on the fonts].
- The GUI text field counts characters as UTF-8 (0x24FDFD0 skips continuation bytes 0x80..0xBF) [CONFIRMED-STATIC].

### 1.5 Caching and threads

- `Localize` has no cache and performs a lookup on every call [CONFIRMED-STATIC].
- Country names are cached in the `CCountry` object (section 2). State names are stored as finished text (section 3).
- The lookup is a plain read with no locks [CONFIRMED-STATIC]. Calls from several threads (tbb) are likely [HYPOTHESIS], so a hook should work without locks (an immutable map swapped atomically).

### 1.6 Consequences for the hook

A hook on `PdxLoc_FindByHash` can return its own `const char*` for reserved hashes and covers all the wrappers shown above. It has no access to the original key or payload, however.

The implemented component hooks `LookupKeyN`, `LookupKeyZ` and `KeyExists` so that it can parse `RN1|<name>|<adjective>|` without a hash registry. It accepts only an empty suffix, `_ADJ` or `_DEF`. The statically confirmed fallback in `BuildNameKey` checks the base `cosmetic_tag + suffix` after the ideology-specific keys fail, so the plugin does not need to claim the ideology variants. Every other suffix falls through to the original localisation, in particular the cosmetic keys for decision titles and descriptions.

`Loc_ExpandRecursive` dereferences the lookup result as an ordinary NUL-terminated C string. There is no wrapper and no metadata before the text. Details, evidence and current limits: [country-re-findings.md](country-re-findings.md).

Note: `PdxLoc_FindByHash` is a leaf with no prologue (`movsxd rax,[rcx+0x2C]; mov r10,rdx; test; jne +3`), so a trampoline must relocate the short `jne`.

## 2. Country name and adjective

### 2.1 Flow

```
 set_cosmetic_tag = X   (CSetCosmeticTagEffect::Execute 0x361760)
 drop_cosmetic_tag      (CDropCosmeticTagEffect::Execute 0x355D50, passes "")
        |
        v
 CCountry_SetCosmeticTag(CCountry* c, const std::string* tag)          0x711260
        |  c+0x1488 = tag   (if it changed)
        v
 CCountry_UpdateNames(CCountry* c)                                     0x716580
        |  base = cosmetic (c+0x1488) or tag
        |  key  = CCountry_BuildNameKey(base, ideology, suffix)        0x6ECFF0
        |         variants checked through KeyExists (0x2244840 -> 0x239D0F0)
        |  c+0x10 = Localize(key "")      name
        |  c+0x30 = Localize(key "_ADJ")  adjective
        |  c+0x50 = Localize(key "_DEF")  definite name
        |  (subject/puppet: formatting with arguments, 0x6C7920)
        |  on change -> copies of the previous names to c+0x90/+0xB0/+0xD0,
        |            MapNames_SetDirty(*0x333CFB8)                      0xE139C0
        v
 gamestate 0x1DB180 (flag/colours)  ->  IngameIface_OnCountryNameChanged  0xA66C50
        v
 [GER.GetName] -> CountryText_GetProperty (0x1A8E6B0) returns c+0x10 etc.
```

Loading uses a separate path, `CCountry::Read` (`0x705CE0`). The `cosmetic_tag` token goes into `c+0x1488` without a direct call to the setter or to `UpdateNames` in that case. [Fresh decompilation](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/country-save-decompile.txt).

The full load cycle with the DLL active works live. After a full restart (PID42732) the map, diplomacy and tooltip kept `qaąęłż` without a new Apply, and the panel kept `testowy`. [Map](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/qa-20261011/16-pl-load-country-map.jpg), [fields](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/qa-20261011/17-pl-load-country-fields.jpg), [diplomacy and tooltip](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/qa-20261011/18-pl-load-diplomacy-tooltip.jpg). The exact order of the functions that refresh the cache after reading has not been traced.

### 2.2 `CCountry` fields (RTTI `.?AVCCountry@@`, vtable 0x27E7360)

| Offset | Field | Evidence | Tag |
|---|---|---|---|
| `+0x08` | tag (int, index) | `Tag_ToString(c+8)` in 0x716580, 0x711260 | CONFIRMED-STATIC |
| `+0x10` | name (`std::string`, finished text) | getter 0x222840 (`lea rax,[rcx+0x10]`), case 0 `GetName` | CONFIRMED-STATIC |
| `+0x30` | adjective | getter 0x6C00E0, case 3 `GetAdjective` | CONFIRMED-STATIC |
| `+0x50` | definite name (`_DEF`) | getter 0x6F2F20, case 1 `GetNameDef` | CONFIRMED-STATIC |
| `+0x90/+0xB0/+0xD0` | previous name/adjective/definite name | 0x716580, getters 0x6F3910/0x6F3900/0x6F3920 | CONFIRMED-STATIC (meaning: HYPOTHESIS) |
| `+0x1488` | cosmetic tag (`std::string`) | 0x711260, 0x361760, written with token `cosmetic_tag` | CONFIRMED-STATIC |
| `+0x130C` | alternative tag used for names | 0x716580 | CONFIRMED-STATIC (meaning: HYPOTHESIS) |

Scripted localisation property IDs for Country (descriptor tables in `.data` from 0x30B4B20): `GetName`=0, `GetNameDef`=1, `GetNameDefCap`=2, `GetAdjective`=3, `GetAdjectiveCap`=4 [CONFIRMED-STATIC]. The properties return the cached fields above and do not perform a new lookup [CONFIRMED-STATIC].

### 2.3 Pseudocode

```c
void CCountry_SetCosmeticTag(CCountry* c, const std::string* tag) {   // 0x711260
    if (c->cosmetic == *tag) return;
    c->cosmetic = *tag;                        // StdString_Assign 0x129CA0
    CCountry_UpdateNames(c);                   // 0x716580
    if (GameState_UpdateCountryFlag(g_gs, &c->cosmetic, c+0x350, c+0x3B0)
        && g_iface && g_iface->vt[0x2D0/8]())
        IngameIface_OnCountryNameChanged(iface_sub(), &c->tag);   // 0xA66C50
}
```

### 2.4 Conclusions

- The country name is cached as text, and the tag setter calls `UpdateNames` [CONFIRMED-STATIC]. Loading with the DLL active restores the custom name without a new Apply [CONFIRMED-LIVE, PID42732]. The exact caller of the post-load refresh is not established.
- The cosmetic tag can be any string and is written to the save (section 5). The path "cosmetic tag `RN_x` plus a hook answer for the hashes of `RN_x`, `RN_x_ADJ`, `RN_x_DEF`" gives persistence without a separate save file [CONFIRMED-STATIC, LIVE for country save/load]. `BuildNameKey` tries the variants and then the base forms. The strict RN1 parser handles only the base, `_DEF` and `_ADJ`. [Analysis and decision regression](country-re-findings.md).
- Alternative: a post-hook on `UpdateNames` that overwrites `+0x10/+0x30/+0x50` (for example through `StdString_Assign`) [HYPOTHESIS, untested].

## 3. State name

### 3.1 Flow

```
 set_state_name = KEY / reset_state_name   (shared Execute, vtable slot 13)
        |
        v
 CSetStateNameEffect_Execute(effect, scope)                            0x3111C0
        |  state = gs->states[scope+0xA8]   (gs+0x2C8 array, gs+0x2D4 count)
        |  tmp   = Localize(effect->key (+0x58))   // for reset: ""
        |  state+0x38 = tmp                          // FINISHED TEXT, not a key
        |  MapNames_SetDirty(*0x333CFB8)            0xE139C0 (+0x81 = 0x0101)
        |  iface = g_iface->vt[0xB8]() ; push type-1 entry (0xD8 B) to iface+0x4A8
        |  if (!byte 0x332F6AA) IngameIface_FlushMapUpdates(iface)     0xB6B050
        v
 CState_GetName(state)                                                 0x9D91D0
        if (state+0x48 /*size*/) return &state+0x38;        // custom name
        if (state+0xF8)          return &state+0xE8;        // other override
        return  *(state+0x30) + 0x20;                       // default name from definition
```

### 3.2 Details

| Element | RVA / offset | Tag |
|---|---|---|
| Execute for set/reset (shared slot 13 of both vtables 0x27501D8 / 0x27502D8) | `0x3111C0` | CONFIRMED-STATIC |
| Effect Read: set copies the token to `effect+0x58`, reset sets `""` | `0x30AD70` / `0x30AC40` | CONFIRMED-STATIC |
| Effect tooltip (`EFFECT_SET_STATE_NAME` / `EFFECT_RESET_STATE_NAME`) | `0x318F50` | CONFIRMED-STATIC |
| `CState+0x38` custom name (`std::string`, text after localisation) | 0x3111C0, 0x9D91D0 | CONFIRMED-STATIC |
| `CState+0xE8` second name override | 0x9D91D0 | CONFIRMED-STATIC (meaning: HYPOTHESIS) |
| `CState+0x30 -> +0x20` default name | 0x9D91D0 | CONFIRMED-STATIC |
| Getter variant without `+0xE8` | `0xE23410` (99 calls) | CONFIRMED-STATIC |
| `CState_GetName`: 432 call sites in 270 functions | `0x9D91D0` | CONFIRMED-STATIC |
| Refresh: flags `+0x81/+0x82` of the `*0x333CFB8` object, iface queue `+0x4A8`, flush `0xB6B050` | | CONFIRMED-STATIC (that this rebuilds the map labels: HYPOTHESIS) |

`MapNames_SetDirty` (0xE139C0) has 6 callers, including 0x3111C0 (state) and 0x716580 (country). Country and state names share the same mechanism [CONFIRMED-STATIC].

### 3.3 Conclusions

- The state name is held and saved as arbitrary text (saving in section 5) [CONFIRMED-STATIC].
- `Localize` returns the key itself for a key that is not in the database (fallback in 0x2245880), so `set_state_name = "Any text"` sets the literal text [CONFIRMED-STATIC; LIVE: none].
- The plugin could set `state+0x38` (through `StdString_Assign` 0x129CA0) and call the same refresh sequence as 0x3111C0 [HYPOTHESIS, untested; in MP this would need synchronising].

## 4. Character name

### 4.1 Flow

```
 set_character_name  (class CSetNameEffect, vtable 0x277B030; token id 0x4E1A)
        |
        v
 CSetNameEffect_Execute(effect, scope)                                 0x362D50
        |  ch = explicit reference (effect+0x58, 0x543430) or character from scope
        |  ch == NULL         -> log ": Character does not exist"
        |  ch retired         -> log ": Character <id> has been retired. Can't Update."
        |  otherwise CCharacter_SetName(ch, copy(effect+0x128))
        v
 CCharacter_SetName(CCharacter* ch, std::string* key)                  0xFA61E0
        |  ch+0x48 = move(key)                // KEY (or literal text)
        |  ch+0x28 = Localize(ch+0x48)        // cached display name
        |  unitLeader = ch+0xA8 (or lookup) -> 0xC26510(unitLeader, key)
        v
 after load: CCharacter_PostLoad (vtable slot 8)                       0xFA43C0
        ch+0x28 = Localize(ch+0x48)
```

### 4.2 `CCharacter` fields (RTTI `.?AVCCharacter@@`, vtable 0x297EA60)

| Offset | Field | Evidence | Tag |
|---|---|---|---|
| `+0x18` | character token id | written with token `token` (0x4A47) in 0xFA6520 | CONFIRMED-STATIC |
| `+0x28` | localised name (cache) | 0xFA61E0, 0xFA43C0 | CONFIRMED-STATIC |
| `+0x48` | name key (`std::string`) | 0xFA61E0; written/read with token `name` (0x1B) | CONFIRMED-STATIC |
| `+0x68` | country (tag) | written with token `country` (0x289A) | CONFIRMED-STATIC |
| `+0xA8` | linked `CUnitLeader*` | 0xFA61E0 | CONFIRMED-STATIC |

`CCharacter_SetName` has 9 callers (including the effect, character_manager and country_characters) [CONFIRMED-STATIC].

### 4.3 Conclusions

- The save receives `name="<ch+0x48>"` as a quoted string (section 5). After loading, PostLoad calls `Localize`; for a key that does not exist, the result is the text itself. A literal name therefore survives save/load without a hook [CONFIRMED-STATIC; LIVE: none].
- 0xC26510 updates the copy of the name held in `CUnitLeader` [CONFIRMED-STATIC]. Whether portraits and all windows refresh immediately: HYPOTHESIS.

## 5. Serialisation (save)

### 5.1 Mechanism

- Keys in the save are lexer tokens: `Tok_Register(obj, id, "string")` (0x24C4B70) at startup, `Token_ToString(id)` (0x24BC260) when writing. The id-to-string map was extracted statically from 10 768 registration call sites [CONFIRMED-STATIC].
- Writing a string: `CWriter_WriteString(writer, tokenId, std::string* value, bool raw)` (0x24C2EA0). The value is written in quotes, with `"` and `\` escaped (two calls to the replace function 0x24CCEA0) [CONFIRMED-STATIC].
- Reading: `ReadToken(obj, reader, tokenId)` methods compare ids; strings are read through 0x24C0AB0 [CONFIRMED-STATIC].

### 5.2 Name fields in the save

| Object | Writer / Reader | Token | Source | Content | Tag |
|---|---|---|---|---|---|
| CState | `0x9E0E90` / `0x9DBE00` | `name` (0x1B) | `+0x38` | literal text; written only when `+0x48 != 0` | CONFIRMED-STATIC |
| CCharacter | `0xFA6520` / `0xFA50B0` | `name` (0x1B) | `+0x48` | key or literal text | CONFIRMED-STATIC |
| CCountry | `0x7191B0` / `0x705CE0` (case 0x372F) | `cosmetic_tag` (0x372F) | `+0x1488` | any string | CONFIRMED-STATIC |

A second Ghidra decompilation (2026-10-11) confirms that `0x7191B0` writes through `Writer_WriteString(writer,0x372F,country+0x1488,0)` and that case `0x372F` of `0x705CE0` reads into the same field. The setter `0x711260` passes the full std::string size to the assignment, and these functions show no length limit on the cosmetic tag. Limits of the generic reader/writer were not established in this pass. Evidence: [country-save-decompile.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/country-save-decompile.txt).

After reading `cosmetic_tag`, the game refreshes the flag and calls `IngameIface_OnCountryNameChanged` [CONFIRMED-STATIC]. **The reader case does not call `UpdateNames` directly.** An earlier analysis found 16 callers of `UpdateNames` (including country initialisation). A later bounded scan of direct E8 calls found 12 call sites in 11 `.pdata` function ranges; it does not cover tail jumps or indirect calls. That `UpdateNames` runs for every country after load: HYPOTHESIS.

### 5.3 Existing fields with literal text

Network commands and objects with an arbitrary string (RTTI `CCommandHelper<...>`): `CSetDivisionNameCommand` (field `+0x30`, written with the `name` token in 0x136DEA0), `CSetUnitNameCommand`, `CSetFleetNameCommand`, `CSetShipNameCommand`, `CRenameEquipmentVariantCommand`, `CSetAirWingNameCommand`, `CRenameAirGroupCommand`, `CRenameAirTheatreCommand`, `CSetIntelligenceAgencyNameCommand`, `CSetOperativeCodenameCommand`, `CSetTheaterGroupNameCommand`, `CSetConveyorNameCommand`, `CRailwayGunSetNameCommand` [CONFIRMED-STATIC for the existence of the classes; the string field was checked only for `CSetDivisionNameCommand`].

Conclusion: literal names survive save/load natively for states (`name`), characters (`name`) and units, fleets and variants. For a country the carrier is `cosmetic_tag` (plus a localisation hook or a cache overwrite). Script variables hold numbers, not strings [HYPOTHESIS, not investigated in the exe].

## 6. Edit box, text input and commands (MP)

### 6.1 Input flow

```
 SDL2 (statically linked; SDLK_* key codes 0x4000004A..0x40000052; IMM32 import)
        |  UTF-8 text
        v
 CTextInputReceiver::OnTextInput(std::string)  -- CEditBox implementation  0x22F40D0
        |  filter (0x237E0F0), optionally digits only (flag 0xD1 & 8, 0x24CCB50)
        v
 CTextBuffer_InsertText(tb, std::string utf8)                             0x22F30A0
        |  UTF-8 -> UTF-32 (0x24FE550), insertion (0x22F2E30)
        v
 keys: CTextBuffer_HandleKey(tb, event)                                   0x22F1030
        Enter (0x0D) -> observers vslot 0x28; CTextBuffer_NotifyEnter     0x22F2050 (vslot 0x30)
```

### 6.2 Object layout

| Element | Description | Tag |
|---|---|---|
| `CEditBox` (vtable 0x2B4A940; subobjects: `CTextBuffer` at `+0x80`, vtable 0x2B4ABD0; `CTextInputReceiver` at `+0x1A0`, vtable 0x2B4AC30) | ctor `0x22F3140` | CONFIRMED-STATIC |
| `CTextBuffer`: observer list `+0x08` (add 0x22EE180, remove 0x22EEBC0) | | CONFIRMED-STATIC |
| `CTextBuffer`: text as `std::basic_string<char32_t>` at `+0x30` (size `+0x40`, cap `+0x48`, SSO 3) | 0x22F27C0, 0x22F30A0 | CONFIRMED-STATIC |
| `CTextBuffer`: length limit `+0xF8` (counted in UTF-8 code points) | 0x22F2190 | CONFIRMED-STATIC |
| `CTextBuffer_SetText`: `0x22F27C0` is a thunk from `std::string&` to `const char*` (actual body 0x22F27D0) | | CONFIRMED-STATIC |
| `CNullEditBox` (null object, ctor 0x2378440); widget lookup does not use `dynamic_cast` | | CONFIRMED-STATIC |
| Lookup of a real CEditBox: `0x22BAD40(container, const MsvcString*, bool warn, bool outerWarn)`; recursion does not depend on the flags | list +0x1240, map +0x1258, a miss returns CNullEditBox | CONFIRMED-STATIC 2026-10-11; runtime pending |

The earlier candidate `0x22BC600`, used for the vanilla `state_name`, looks up an instantTextboxType, not a CEditBox. The list at +0x11E0 and the null object at +0x1E0 confirm this; the earlier analyst alias was not evidence of the type. The correct lookup 0x22BAD40 returns the null object at +0x1D8, consistent with the CNullEditBox constructor 0x2378440.

Reading UTF-32 from `CEditBox+0x80+0x30` and the correct lookup now have a static contract. A separate log-only observer checks the model name, the vtables and memory readability; it does not cache the widget. Actual typing and live reading are still pending. See the [trial contract](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-native-editor-contract.txt).

### 6.3 Network commands

```
 GUI (e.g. 0x16AC9D0, division rename)
   session = g_iface(0x332F698)->vt[0x88]()
   cmd = new CSetDivisionNameCommand(unit, name)        // 0x58 B, ctor 0x13615D0
   CSession_PostCommand(session, cmd, false)            // 0x2250B00, session.cpp
        -> log "Dropped command" or session+0x58 ->vt[0x28](cmd) (queue)
```

| Element | Description | Tag |
|---|---|---|
| `CCommand` (vtable 0x27214C8): `+0x0C` player, `+0x10` tick, `+0x14` | | CONFIRMED-STATIC |
| vslot 9 validation, 10 Execute, 11 type (returns a token id, e.g. 0x3909), 13 Clone, 22 (`+0xB0`) WriteBody, 23 (`+0xB8`) ReadBody | from `CSetDivisionNameCommand` | CONFIRMED-STATIC (slot names: HYPOTHESIS) |
| `CSession_PostCommand`, 667 call sites | `0x2250B00` | CONFIRMED-STATIC |
| Commands are serialised with tokens (like the save), so strings in commands are supported | 0x136DEA0 | CONFIRMED-STATIC |
| `CExecuteButtonCommand` (scripted GUI) and `CChatCommand` as synchronisation carriers | RTTI | HYPOTHESIS (not analysed) |

### 6.4 Boundary between country names and literal unit/party names

The native country cache exposes the name, the definite form and the adjective. Changing these fields does not create new names for divisions, templates or parties. Files of the exact 1.19.3 installation confirm this: `history/units/POL_1936.txt:2` has `name = "Dywizja Piechoty"`, `common/units/names_divisions/POL_names_divisions.txt:47` has `fallback_name = "%d Dywizja Piechoty"`, and `localisation/english/parties_l_english.yml:77` has `POL_neutrality_party: "Sanacja"`. None of these reference `GetAdjective`. A passing adjective test in dynamic event text does not count as evidence that these names change automatically. The workaround for unit and template names is the game's own standard editor; parties need separate localisation.

## 7. Recommended hook points

The order follows the robustness-to-effort ranking from an earlier pass. All 42 signatures were rechecked as unique in `.text` of the pinned build ([naming-signatures.json](naming-signatures.json), [scan result](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/signature-verification.json)). The implemented component uses the three localisation wrappers, not recommendation #1 in the table below. The three entry points and the hook installation are confirmed live; the other addresses remain static only.

| # | Function (RVA) | Use | Signature | Confidence |
|---|---|---|---|---|
| 1 | `PdxLoc_FindByHash` (0x239ED90) | return custom text for reserved hashes | `48 63 41 2C 4C 8B D2 85 C0 75 03 33 C0` | high (STATIC); leaf, short `jne` in the first 11 bytes |
| 2 | `CCountry_SetCosmeticTag` (0x711260), called, not hooked | set `RN_x`, recompute names, refresh the map | `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30 48 83 7A 18 0F` | high |
| 3 | `CCountry_UpdateNames` (0x716580) | post-hook overwriting `+0x10/+0x30/+0x50` (alternative to #1) | `48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 FE FF FF` | high (STATIC) |
| 4 | `CSetStateNameEffect_Execute` (0x3111C0) / `CState_GetName` (0x9D91D0) | pattern for setting a state name and refreshing; getter for observation | `4C 8B DC 49 89 5B 08 49 89 6B 18 49 89 73 20 57 41 56 41 57 48 83 EC 50` / `48 83 79 48 00 74 05 48 8D 41 38 C3` | high |
| 5 | `CCharacter_SetName` (0xFA61E0), called | literal character name | `48 89 5C 24 18 48 89 54 24 10 56 57 41 56 48 83 EC 40 48 8B DA` | high |
| 6 | `CEditBox_OnTextInput` (0x22F40D0), `CTextBuffer_NotifyEnter` (0x22F2050) | capture typed text | `48 89 5C 24 08 57 48 83 EC 40 48 8B DA 48 8B F9 48 8D 91 B0 00 00 00` / `40 53 48 83 EC 20 48 8B 59 08 48 85 DB 74 13 90 48 8B 0B 48 8B 01 FF 50 30` | medium |
| 7 | `CSession_PostCommand` (0x2250B00) | MP synchronisation entry point | `48 89 5C 24 08 57 48 81 EC 30 01 00 00 48 8B FA 48 8B D9 45 84 C0` | medium (MP carrier design still open) |
| 8 | `Localize` (0x2245E60) | observation, or an alternative to #1 with access to the key string | `48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 50 45 33 FF` | high, but does not catch hash lookups (0x239D150) |

Helper functions the plugin can call (signatures in the JSON): `StdString_Assign` 0x129CA0, `Scope_GetCountry` 0xBB4390, `Tag_ToString` 0xBB4E70, `MapNames_SetDirty` 0xE139C0, `IngameIface_FlushMapUpdates` 0xB6B050, `IngameIface_OnCountryNameChanged` 0xA66C50.

Live verification: `python plugin/tools/observe.py --pid PID --seconds 120 --out LOG`. The host checks the SHA-256 of the process executable. The script searches for the signatures in `.text`, rejects non-unique or shifted matches and logs selected calls. Frida instruments instructions temporarily; it writes no gameplay state and no game files. The first attach did not complete, so it is not evidence of live function calls.

## 8. Open questions

| Question | What was checked | Status |
|---|---|---|
| Variant fallback in `BuildNameKey` | a fresh decompilation of 0x6ECFF0 confirms the ideology attempts, then base tag + suffix; the hook accepts only base/_DEF/_ADJ | CONFIRMED-STATIC; full ideology matrix not tested live |
| Meaning of `CState+0xE8` | getter 0x9D91D0 | unknown |
| How to obtain the player's `CCountry*` | not analysed | open |
| Does `UpdateNames` run for every country after load | 16 callers | HYPOTHESIS |
| Threads that call the localisation lookup | the lookup takes no locks | HYPOTHESIS; check `Process.getCurrentThreadId()` in Frida |
| MP synchronisation carrier for text (`CExecuteButtonCommand`, `CChatCommand`, reuse of an existing command) | RTTI only | open |
| Full font coverage (Polish characters etc.) | not an exe question | to test in game |
| Do map labels rebuild after `MapNames_SetDirty` alone | callers 0x3111C0, 0x716580 | HYPOTHESIS |

## 9. Addendum 2026-10-11: two script scope bugs

### 9.1 Typed State and the plain state number

`rn_sel_state = PREV` stores a typed State reference, not a plain number. Evaluator `0x543EE0` encodes it as `state_id | 0xFFFFFFFFC0000000`, while the number parser `0x546530` encodes the literal 85 as `8500000`. `CCheckVariable` (`0x3D0BB0`) compares the raw signed values, so the reference to Gdańsk (state 85) satisfies neither `> 0` nor `= 85`. `THIS.id` is not a confirmed converter to a plain number. [Analysis and raw evidence](typed-state-variable-findings.md).

Commit `cc9791d` keeps the typed reference for `var:` and arrays. Inside its scope, it matches `state = ID` and writes a separate literal to `ROOT.rn_sel_state_id`. The same dispatch loads VPs for all 1081 states, including the 126 without VPs. [Mappings and negative checker fixtures](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/city-state-literal-mapping-checks.json) confirm, among others, 85->362, 10->3544/402 and state 72 without VPs; they are static evidence.

Runtime behaviour of the city and its save is a separate matter. In `4f4c5ad` "Nowy Gdańsk" was observed for VP 362, and `bd96572` records a PASS for a full restart and load of the custom save without renaming again. [Map after restart](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/qa-20261011-day2/17-city-map-after-full-restart-load.jpg), [log comparison after load](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/qa-20261011-day2/run33252-after-load-log-comparison.json). This does not cover all states, nor the zero-VP UI live.

### 9.2 Character: correct decoder, limited scripted GUI bridge

The blank header was not caused by an empty target: the save contained identical `rn_display_target^0` and `rn_tchar^0` and the custom character `0x20000E8B`. [Diagnostic save inspection](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-blank-header-save-inspection.json).

Decoder `0x5443F0` correctly recognises the Character tag `0x20000000`, and `0x53A400` writes the reference to `scope+0x50`. The problem is a separate bridge in the scripted GUI updater `0xB8AD20`, used with `change_scope = yes`. It rebuilds the child scope through `0x535E00` -> `CCharacter+0xA8` (`CUnitLeader*`) -> `0x53B8E0`. It does not copy the Character reference directly, so a civilian character without a UnitLeader loses it at this step. [Independent review and result limits](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-character-scope-review.txt), [decoder and bridge](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-character-bridge-decompile.txt), [list updater](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-character-row-updater-decompile.txt).

Vanilla supports reading a name through a typed variable:

```text
ROOT = { set_variable = { SOV_purged_character_for_loc = PREV } }
[?ROOT.SOV_purged_character_for_loc.GetName]
```

Sources in the 1.19.3 installation: `common/scripted_effects/SOV_scripted_effects.txt:1687` and `localisation/english/nsb_events_l_english.yml:711`. `Character.GetName` is documented in `documentation/loc_objects_documentation.md:145-147`. `common/scripted_guis/_documentation.md:83-85` documents `value`, `index` and `change_scope`, without promising support for every scope type.

Commit `224be98` keeps the country as the row context (`change_scope = no`), renders `[?rn_row_character.GetName]` / `[?rn_header_target.GetName]` and, on click, goes explicitly through `var:rn_row_character` to the target helper. The native updater also preserves the raw row variables for the click command. The generator/checker and an independent review approved this path statically. Runtime PID22876 confirmed the names on both sides of the list, the Haller and Mościcki headers, a custom Apply of the leader "Jan Żółć" in GetLeader and in native politics, and the civilian Kwiatkowski renamed to "Adam QA" with a list refresh. Save and load of these two changes are still pending. [Current matrix](../contributing/in-game-test-record.md).
