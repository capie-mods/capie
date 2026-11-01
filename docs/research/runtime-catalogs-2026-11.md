# Runtime catalogs (API 9): findings

Research for CAPIE API level 9, "runtime catalogs". The goal is that Rename Anything (RA) works with
any total conversion without a patch for each mod. The background and plan (c) are in
`rename-anything/docs/research/mod-compatibility-kaiserredux.md`. The design that follows from these
findings is `../contributing/api9-runtime-catalogs-spec.md`.

- Game: HOI4 1.19.4.1, `hoi4.exe` SHA-256 `CF781ED1…476F`. Addresses are RVAs from image base
  `0x140000000`. Function names are analyst labels.
- Tools: Ghidra 12.1.4 through REA on a fresh, fully analysed session of the 1.19.4.1 file;
  pefile/capstone (relenv interpreter) for RTTI and vtables; Python over the mod files on disk.
- Nothing was run in the game. Every claim is static, and section 9 lists what needs a runtime check.
- Confidence: **high** means read in the decompiled code or the bytes; **medium** means inferred from
  one call site or from structure; **low** means plausible but unchecked.
- Corrections to earlier notes: the character manager arrays (B4 of the Ghidra review), see 3.2.

## 1. Summary

| Catalog | Where the game holds it | Enumerable cheaply? | Changes during a game? | Verdict |
|---|---|---|---|---|
| Countries and tags, including dynamic tags | `[gs+0x310]` country array, count `+0x31C`; tag text `[gs+0x358]` | yes, a plain array | dynamic tags appear and disappear | **Not needed from CAPIE**: script already iterates `every_country` and `every_possible_country`. CAPIE only needs the tag text of a country (for filtering). |
| Cosmetic tags (defined) | `std::unordered_map<string, CCountryColors*>` at `gs+0x8E8` | yes | no (loaded once) | Not needed in v1. The current cosmetic tag of a country (`+0x1488`) is what RA needs, for the snapshot. |
| Flag textures (`gfx/flags`) | **not held**: flags are probed by name (`BuildFlagCandidates`), never listed | yes, through the game's VFS listing `GetFiles` | no | **Feasible**: list `gfx/flags/*.tga` once with the game's own listing, which honours `replace_path`. |
| Characters per country | character manager `CCountry+0xFF0`: recruited `+0x10`, pool `+0x28` | yes | yes (recruit, create, kill) | **Not needed from CAPIE for the list**: script iterates `every_character`, and the portrait can be drawn natively with `image = "portrait"`. |
| Portraits: sprites | sprite database `[0x3500ED0]`, hash map at `+0x100` | yes, about 32k to 106k sprites, filtered by name | no (only on a GUI reload) | **Feasible** (walk once, filter by name). |
| Portraits: texture paths (KRX's about 14 000 runtime leaders) | the file system (`gfx/leaders/**`) and each character's portrait entry (kind 1) | yes, `GetFiles` recursive | no | **Feasible**. |
| States, names, VPs | `gs+0x2C8` state array by id; state provinces `+0x18`; VP value at province `+0x38` | yes | VPs can change by effect | **Feasible**, per selected state, read at the click. |

The exposure mechanism, which section 6 explains, is **local display plus names in the command**:

- CAPIE keeps a per-player, local "catalog view" (catalog, filter, page). It draws it through slot
  sprites that it re-points to other textures, and through localisation keys it answers.
- A click on a catalog cell is a scripted GUI command. CAPIE adds records to it holding the chosen
  **name**, or the province id. Every client runs the effect with the same records, so the saved
  state is identical everywhere, even when the local catalogs differ.

## 2. Countries, tags, cosmetic tags

**2.1 Country array.** `[gs+0x310]`, an array of `CCountry*` with an int32 count at `+0x31C`, indexed
by the tag index. This is already pinned and used by every CAPIE layer (`core/game_layout.hpp`).
Confidence: high. Dynamic tags (`D01`…) are most likely in the same array, because the tag table below
and the array are both gamestate members. Confidence: medium, not traced in code.

**2.2 Tag text.** `0xC08100(const int* tag)` returns `&(*(gs+0x358))[tag]`, an array of MSVC
strings with a 0x20 stride. Without a gamestate it falls back to the static database `[0x33DF288]`
through `0x763EC0`. Confidence: high. The VP name builder `0xEEED80` uses the same call chain:
`0xC07B80` → `0x733F40` → `0xC08100`.

**2.3 Cosmetic tags.** `0x1E34F0`, called from `InitGameState` through `0x1E2DE0`, loads
`common/countries/cosmetic.txt` into a `std::unordered_map<string, CCountryColors*>` at `gs+0x8E8`:
list head `+0x8F0`, size `+0x8F8`, buckets `+0x900`/`+0x908`. Each node is
`{next, prev, key string +0x10, CCountryColors* +0x30}`. Entries without colours are dropped with a
log line. Confidence: high. A country's **current** cosmetic tag is the MSVC string at `+0x1488`
(already pinned, `load.cosmetic_argument`).

**What script can already do.** `every_country`, `every_possible_country`, `has_cosmetic_tag` and
`original_tag` are scope-based and work for any mod. RA's real problem is literals: `original_tag =
SOV`, and static lists of cosmetic tags. CAPIE removes both needs: it filters flags by the scope
country's tag text (2.2), and it hands the current cosmetic tag to the effect as packed records
(spec §5.3).

## 3. Characters and portraits

**3.1 Portrait entries.** A portrait entry is 0x30 bytes: `set` byte `+0`, `kind` int `+4` (0 =
sprite name, 1 = file path), MSVC string `+8`, texture handle `+0x28` (already in
`portrait_layer_hooks.hpp`).

- `PortraitEntryValid` (`0x1439200`): kind 0 calls `FindSprite(db, entry+8)` and, in a game with
  `gs+0xA39` set, needs the sprite's slot `+0x88` (size) to be non-zero. **Kind 1 is valid only when
  the handle at `+0x28` is non-zero.** It does no lookup. Confidence: high.
- `MakeIconEntry` (`0x1438AC0`), `entry* (entry* out, int kind, string&& name, bool flag)`: for kind 1
  (and a name other than `.`) it **loads the texture at once**:
  ```text
  texmgr = *(spriteDB + 0x170)
  id     = AddTexture(texmgr, &name, &opts{0, flag}, 1)   // 0x2488BD0, loads synchronously
  out+0x28 = GetTexture(texmgr, id)                       // 0x2489670, texture*
  ```
  Confidence: high.
- `AddTexture` (`0x2488BD0`) hashes the path (FNV-1a) into a table at `texmgr+0x28`. Its records are
  0x48 bytes at `texmgr+8`: `texture*` `+0`, path `+8`, refcount `+0x30`. Both `AddTexture` and
  `GetTexture` take the global mutex at `0x31383F0`, **so the texture manager is safe to call from
  any thread.** Confidence: high. No release function was identified (open item O1).
- Builders: `0x14393D0 (out, const string* path, bool warn, bool flag)` always makes kind 1.
  `0x1439480 (out, const string* s, bool warn, bool flag)` makes kind 1 when the string contains `/`
  or `.`, otherwise kind 0. It has 21 callers, among them `create_country_leader` and
  `ResolvePortrait`. Confidence: high.

**Consequence for the portrait layer (question 4).** An entry `{set=1, kind=1, name=path, handle=0}`
fails the validity check, so the game would show its own portrait. CAPIE must build path entries with
the game's builder `0x14393D0`, which loads the texture. It should do that on the logic thread, the
thread where `create_country_leader` does it, and cache the result. Confidence: high.

**3.2 Character manager (correction to B4).** `AddCharacter` (`0x1171000`, `(mgr, char, force)`)
inserts the character into the array at `mgr+0x10` (count `+0x1C`) and removes it from `mgr+0x28`
(count `+0x34`). Entries in both are 16 bytes `{CCharacter*, int}`, kept sorted by pointer (lower
bound `0x116EA40`). So `+0x10` is not a hash map of tokens. Most likely reading: `+0x10` holds
recruited or active characters and `+0x28` holds the unrecruited pool; a country's full set is the
union. Confidence: high for the operations, medium for the meaning.

**3.3 Character fields.** id int `+0xC` (pinned); name MSVC string `+0x28`, getter `0x666420`, which
generates a name from the int at `+0x18` when the string is empty; owner tag int `+0x68` (set by
`0x102A690`); portraits map `+0x78`, a sorted flat array (data `+8`, count `+0x14`) of 0x38-byte
elements `{int type; int size; entry (0x30)}`. `0x143A8C0(map, type, size)` looks up the exact pair,
then the first with the same size, then a default. The generated-portrait fallback is seeded from the
id (`id`, `0x5EA6BA9F - id`). Confidence: high. A gamestate-level character database is at
`[gs+0x6A8]` (layout not read).

**3.4 `create_country_leader`** (`CLegacyCreateCountryLeaderEffect`, vtable `0x27E39C8`, Execute
`0x371D60`). It asserts the gamestate and `ThreadForbidCount == 0`, so it runs on a gamestate thread.
The picture string at effect `+0x98` goes three ways: a string containing `/` becomes a kind-1 entry
through `0x14393D0`; one ending in `.dds` becomes `gfx/leaders/<TAG>/<picture>`, kind 1; anything else
goes through `0x1439480`. It then creates the character (`0x6FFD70` on `[gs+0x6A8]`), adds it
(`AddCharacter`) and sets the leader (`0x1174080`). **So every KRX runtime leader is a real character
object, in its country's manager, with a kind-1 entry whose texture is already loaded.** Script sees
it with `every_character`. Confidence: high.

**3.5 Drawing a character's portrait natively.** The scripted GUI updater `0xBDD5B0` draws the
portrait of the scope character when an `image` property evaluates to the literal `portrait` (review
R2, confirmed on 1.19.4.1 by the agent). It caches a hash of the character so it redraws only on
change, and the draw helper `0xB9B1F0` uses the character's own entry, path entries included. So RA's
Characters list needs no portrait catalog, provided the list entries have character scope (open item
R-T1). Confidence: high for the code, low for list-entry scope.

**3.6 Sprite database.** `[0x3500ED0]`. `FindSprite` (`0x22A0ED0`) is a thunk to `0xA88D30`.
- Map at `db+0x100`: int count `+0x100`, uint bucket count `+0x104`, `node**` buckets `+0x108`.
  A node is `{obj* +0, next* +8}`; an empty bucket is null. Hash `0x254EA40` over the name, modulo
  the bucket count. Confidence: high.
- Walk: `for b in 0..nb: for n = buckets[b]; n; n = n->next`. Read-only and cheap: 106k nodes on
  KRX. Confidence: high.
- Insert: `0x705740 (map*, obj*)` prepends a node and increments the count, with **no lock and no
  rehash**. Confidence: high. CAPIE should not insert sprites (spec §4.3).
- Sprite object, `CSpriteType` (RTTI, vtable `0x2BB3420`, 0x1F0 bytes, built by `0x22A6B60` from
  the `.gfx` parser `0x22A3410`): vtable `+0`, type token `+8`, name `+0x10`. Its slot `+0x88`
  (`0x22A8230`) calls the lazy loader `0x22A7F80` and returns the size at `+0xA4`. Confidence: high.
- **Lazy loader `0x22A7F80`** (read on 1.19.4.1):
  ```text
  if (sprite[+0x1D3] && sprite[+0x1A0] == -1 && sprite[+0x80] /*path length*/ != 0) {
      texmgr = *(sprite[+0x48] /*db*/ + 0x170)
      id = AddTexture(texmgr, sprite + 0x70 /*texturefile*/, {0, 0x1000001, 0}, 1)
      sprite[+0x1A0] = id
      sprite[+0x170] = GetTexture(texmgr, id)
      sprite[+0xA4]  = size(texmgr, id)       // 0x24894A0, two ints
  }
  ```
  So the texture file path is the MSVC string at `+0x70`, the texture id `+0x1A0` (`-1` = not loaded),
  the texture `+0x170`. Confidence: high. **This is what makes runtime display possible**: CAPIE can
  point an existing sprite at another texture with the same three writes, using the game's own
  thread-safe texture calls (spec §4.3).

**3.7 Portrait-like sprites and files.** Vanilla defines 5406 portrait-like sprites. Their textures
are under `gfx/leaders/**` (1138 `.dds`). KRX adds 2418 sprites and 9667 `.png` files. Section 7 has
the counts.

## 4. States, provinces and victory points

- **State array.** `InitGameState` (`0x1F6610`): `gs+0x2C8` is a CArray of `CState*` (data `+0x2C8`,
  count `+0x2D4`), indexed by state id, with slot 0 empty. `CState` is 0x928 bytes, built by
  `0xA1A830`. Confidence: high.
- **Provinces of a state.** `0xA2A170(state)` loops over `[state+0x18]` (count int `+0x24`), reads
  each province's VP value at `+0x38`, and stores the index of the largest at `state+0x85C` (`-1` when
  no VP). So a state's VPs are exactly its provinces with `+0x38 > 0`. Confidence: high.
- **Province object.** VP value int `+0x38`: the setter `0xEF2740` is `mov [rcx+38h],edx` and then
  tail-calls `0xA2A170(province+0xC0)`. Its state is the pointer at `+0xC0`, its id the int at
  `+0xA4`, and its player-given name the MSVC string at `+0x40` (length `+0x50`), written by
  `set_province_name`. Confidence: high.
- **Province by id.** `(*(gs+8))->vtbl[+8](gs+8, id)`, read in the history command
  `CSetStateVictoryPoints` apply (`0x163DA60`; vtable `0x2A546B8`). Confidence: high.
- **Province name as shown.** `0xEEF270(province, string* out, bool* is_vp)`: the custom name (`+0x40`)
  if set, else the VP name `0xEEED80`, else the state name or the number. `0xEEED80` tries the key
  `<owner TAG>_VICTORY_POINTS_<id>` first, then `VICTORY_POINTS_<id>`, and localises it with
  `0x22AEA40`. Confidence: high. **RA's static loc buckets can be replaced by this getter.** It also
  respects tag-specific VP names, which RA's buckets do not.
- **State id in a GUI scope.** On 1.19.3.0 the selected state's id is at scope `+0xA8` and a state's
  id at `state+0x58` (`state-view-internals.md`, `typed-state-variable-findings.md`). Not re-read on
  1.19.4.1 (open item O4). Confidence: medium.
- **When it changes.** VPs change only through effects (`set_victory_points`, `add_victory_points`,
  `CSetOrAddVictoryPointsEffect`, vtable `0x27FC280`) and history. Ownership changes do not move
  provinces between states. So reading the list at the moment of the click is exact.

## 5. Listing files: the game's VFS

- PhysFS is linked statically: `hoi4.exe` imports no `PHYSFS_*` symbol from a DLL. Confidence: high.
- `VFS::GetFiles` (`0x2560AA0`): `void (const char* dir, CArray<FileEntry>* out, const char*
  ends_with /*nullable*/, const char* contains /*nullable*/, int max_depth)`. `out` is
  `{data, int cap, int count, allocator*}`. Entries are 0x40 bytes and start with an MSVC string
  `"dir/" + relative path`. It clears `out` first. Confidence: high.
- The worker `0x255E830` is recursive. Its anchor is the string "Reached max depth when enumerating
  files recursive". It **honours `replace_path`** through the globals `0x36917A8`, `0x36917B0` and
  `0x36917BC`: for a replaced directory it keeps only files whose real directory comes at or before
  the cutoff mod. Confidence: high. This is the same file view the game loads from.
- Example caller: `0x1467A0` calls `GetFiles(folder, &arr, ext, NULL, 0)`. GetFiles has about 100
  callers.
- The game never lists `gfx/flags`. It loads flags by candidate name (B4 of the Ghidra review). The
  strings "gfx/flags/medium/" and "gfx/flags/small/" exist. Confidence: medium.
- A flag catalog therefore needs three listings: `gfx/flags`, `gfx/flags/medium` and
  `gfx/flags/small`, with `.tga` and depth 0. Keep the stems present in all three, so a chosen flag
  never mixes sizes (KRX note (d)4). The cost is one call per folder, once per session.

## 6. How a scripted GUI can show runtime data

**6.1 What the `image` property accepts.** The updater evaluates the property text with
`ProcessString` (`0x135F490`, anchor "Too deep recursion in ProcessString"). Confidence: high.

- It expands only bracket expressions: `[?var]` (`0x135F9E0`), `[(...)]` (`0x135F0B0`) and
  `[Scope.Func]`, which resolves through the context and reprocesses the result.
- It does **not** substitute `$KEY$` and does not look the whole string up as a key.
- Four bytes starting with `0x12` are a country flag icon; `portrait` in a character scope is the
  native portrait (3.5); any other value calls `SetSprite` (`0x231D6F0`) by name.

So an `image` can only name a sprite. A sprite name can be composed from variables, which RA already
does (`image = "GFX_rnfm_[?rn_fp_v|0]"`).

**6.2 Text.** An `instantTextBox` text that is a localisation key goes through the hooked lookups.
CAPIE already answers `CAPIE_STATUS` and the `RN1|` keys there. A key cannot be composed from a
variable (6.1). So per-row text needs one key per **slot**, which is what RA's Cities tab does today
(`RN_VPROW_0`..`RN_VPROW_n`, scripted localisation). Whether the game re-evaluates a plain key, or
caches it in the widget, is not established. A scripted-localisation wrapper (`[RnCapieRow3]`) is
re-evaluated, as RA's `RN_VPROW_*` show (open item R-T3).

**6.3 Sprites at runtime.** There are three options.

1. Insert new `CSpriteType` objects into the database. This would need the 0x1F0-byte constructor and
   the parser's virtual reader, and the insert has no lock. **Rejected.**
2. **Re-point existing sprites** (3.6): a mod declares a fixed set of slot sprites in its own `.gfx`.
   CAPIE finds them by name with `FindSprite`, checks their vtable against `CSpriteType`, and sets
   `+0x1A0`/`+0x170`/`+0xA4` to a texture it loaded with `AddTexture`/`GetTexture`, exactly as the
   lazy loader does. **Chosen.** The texture calls are thread-safe; the writes happen on the UI
   thread, which owns the sprites.
3. Show only things the game can already draw: country flags through the `0x12` flag icon, and
   characters through `portrait`. This works for the Characters list, but not for a picker of
   arbitrary flags or portraits.

Whether a widget reads the sprite's texture on every draw or copies it at `SetSprite` is not known.
The spec avoids depending on it: it uses two slot sets and switches the sprite **name** between pages,
so `SetSprite` always runs after the re-point (spec §4.4).

**6.4 Clicks and records.** CAPIE's dispatch hook (`0xBD9380`) already copies the clicked context's
temporary-variable records (`context+0x470`) and appends its own (`public_editor.hpp`). A dynamic
list entry's `value` is one of those records. This gives CAPIE the clicked slot on the clicking
machine, before the command is sent. Confidence: high (existing code path).

**6.5 Threads.** `ThreadForbidCount` is a TLS int at `+0x10` (TLS index `[0x3693A98]`). Gamestate
code asserts that it is 0. GUI code paths assert it too (`PortraitEntryValid`, `0xC08100`, the state
tooltip `0x17DE630`), so the UI thread may read the gamestate. Confidence: medium. CAPIE can read the
same TLS counter and refuse any catalog read when it is non-zero (spec §8).

## 7. Data measured in the installed mods

Measured with a throwaway script (session scratchpad, not in the repo) over the files on disk. It
reads vanilla, KaiserreduX (KRX, Workshop 2076426030) and The Road to 56 (R56, 820260968) layered on
vanilla. The script does not apply `replace_path`, which is harmless here: KRX replaces the folders it
counts, and R56 only adds to them. Counts are of definitions in files, not of what one game uses.
Confidence: high for counts. The VP count reads `victory_points = {` blocks, including dated ones, so it
is an upper bound.

| Item | Vanilla | KRX | R56 (+vanilla) |
|---|---|---|---|
| Tags (`common/country_tags`) | 439 | 406 | 510 |
| Cosmetic entries (`common/countries/cosmetic.txt`, top-level keys) | 388 | 1224 | 603 |
| Flag stems, large / medium / small | 2207 / 2222 / 2318 | 3987 / 3983 / 3980 | 3256 / 3237 / 3351 |
| Flag stems present in all three sizes | 2206 | 3974 | 3212 |
| Longest flag stem | 55 | 49 | — |
| Characters in flag stems outside `[A-Za-z0-9_]` | none | none | — |
| States (`history/states`) | 1081 | 1367 | 1143 |
| VP blocks, most in one state / total | 7 / 1535 | 13 / 1988 | 9 / 2142 |
| Sprites named in `interface/*.gfx` | 32 126 | 106 348 | — |
| Portrait-like sprites (`GFX_portrait*`, `GFX_leader*`, any case) | 5406 | 2418 | — |
| Longest portrait-like sprite name | 67 (3 over 63) | 59 | — |
| Distinct portrait texture paths written in script (`picture =`, `large =`, `small =`) | 8 | 4525 | 2608 |
| Longest such path | 52 | 73 (53 over 63) | 87 (331 over 63) |
| Characters in those paths outside `[A-Za-z0-9_]` | `/ .` | `/ . - '` and 3 paths with non-ASCII letters | `/ . -` and 5 with non-ASCII |
| Files under `gfx/leaders` | 1138 `.dds` | 9667 `.png` | — |

What follows from this:

- **The 17.3 alphabet is enough for flag stems.** It has 63 codes, and every one is used. It cannot
  express `/`, `.`, `-` or `'`, so it **cannot carry portrait paths**, and it misses a few sprite names
  (143 KRX sprites contain `-`, 122 contain `'`). API 9 needs a second packing for paths and for those
  sprite names (spec §6.3).
- **Length.** 63 characters is not enough for paths: R56 has 331 paths longer than that. A limit of 126
  covers every measured path.
- **Page sizes.** No state has more than 13 VPs, so a cities list never needs paging in practice. Flag
  and portrait grids need paging: 4000 flags and 9667 portrait files are far too many for one
  dynamic list.
- **Non-ASCII paths** (8 of about 7100) are rare. They can be left out (spec §6.3).

## 8. Does `dependencies` in a descriptor make the launcher warn or block?

**Short answer: no, it blocks nothing, and most likely it warns about nothing. The game uses it only to
order the load.** Confidence: high for "no block", medium for "no warning". One launcher check is still
worth doing (spec §12.5, L1).

Evidence:

1. **Documentation.** The community wiki text bundled with the hoi4-modding plugin
   (`modding_documentation.txt:469`) says `dependencies` "changes the order in which mods will be
   loaded, in particular forcing this mod to be loaded after every mod listed in this attribute. This
   doesn't make this mod require the listed mods to work." The same text (`:518`) says the name must
   match the dependency's `name=` exactly. The live wiki page `Mod structure` presents it only as a way
   to set the load order and mentions no launcher behaviour. No Paradox page says otherwise.
2. **Mods on the Workshop that rely on it.** On this machine, 25 installed Workshop items declare
   `dependencies`. Several list mods that cannot all be installed together. They would be unusable if
   a missing dependency blocked the launch:
   - "Universal Colored Puppets" (3114159543) lists 15 total conversions (Kaiserreich, KaiserreduX,
     R56, Old World Blues, Millennium Dawn, TNO and others). This is the same use RA needs: "load
     after whichever of these is present".
   - KRX submods (2924720944) list four name variants of KaiserreduX ("Kaiserredux", "Kaiserredux DEV",
     "Kaiserredux: A Kaiserreich Expansion", "KaiserreduX"), of which only one can exist.
   - "The Road to 56 RP" lists both "The Road to 56" and "The Road to 56 [Beta]".
3. **The launcher's own database.** `launcher-v2.sqlite` has a `mods_dependencies` table (columns
   `modId`, `dependencyType`, `dependencyId`, `dependencyName`). It is **empty**, although those 25 mods
   are registered in `mods`. So the launcher does not import descriptor `dependencies` into its own
   dependency model, which is the model it would need to show a "missing dependency" warning. The
   table most likely serves Paradox Mods (PDX) dependencies. This is an inference from the schema and
   an empty table, so it is medium confidence.
4. **Steam's "Required items"** is a different mechanism. It belongs to the Workshop page, not to the
   descriptor, and Steam prompts to subscribe to required items. RA must **not** set Required items for
   the total conversions.

Consequences for RA:

- A curated `dependencies` list of script-replacing total conversions is safe for players who have
  none of them. It only orders RA after whichever one is present.
- Names must match exactly, including variants (`"KaiserreduX"` and the older
  `"Kaiserredux: A Kaiserreich Expansion"`). Collect names from the descriptors of the Workshop items,
  as the KRX submods do.
- The list does not help with a total conversion that is not on it. CAPIE's self-check (spec §11, "Load order") is
  the safety net.

Sources: [HOI4 wiki, Mod structure](https://hoi4.paradoxwikis.com/Mod_structure); the local copy of
the wiki text in the hoi4-modding plugin; the Workshop descriptors and `launcher-v2.sqlite` on this
machine (read-only copy).

## 9. Open items

| # | Item | Blocks | How to close |
|---|---|---|---|
| O1 | Texture release (refcount decrement) in the texture manager | freeing re-pointed slot textures; without it, textures stay loaded for the session | find the function that decrements `+0x30` of a texture record (callers of the sprite destructor `0x22A7630`) |
| O2 | Whether a widget reads the sprite texture per draw | nothing (the two-set scheme avoids it) | in-game probe S2 |
| O3 | How to free a `GetFiles` result array (CArray destructor) | nothing (leaking it once per session costs a few hundred KB) | find the destructor used by a caller such as `0x1467A0` |
| O4 | The state id in a GUI scope (`+0xA8`) and in `CState` (`+0x58`) on 1.19.4.1 | the VP view | re-read `0x53B5F0` and `0xB8D1E0` on 1.19.4.1, then pin |
| O5 | The thread of the dispatch hook versus the logic thread | nothing (CAPIE checks `ThreadForbidCount`) | log the thread ids in the probe build |
| R-T1 | `dynamic_lists` with `change_scope = yes` over an array of characters gives character scope, so `image = "portrait"` and `[This.GetName]` work | the native Characters list | in-game probe (RA) |
| R-T2 | Dynamic tags are in `[gs+0x310]` | nothing in v1 | probe: `create_dynamic_country`, then count |
| R-T3 | A plain loc key in `text` is re-evaluated after a page change | per-slot text | in-game probe S3; fallback: a scripted-localisation wrapper |
