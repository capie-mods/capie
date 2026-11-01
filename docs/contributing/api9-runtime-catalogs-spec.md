# Runtime catalogs: implementation spec (CAPIE 1.7, API level 9)

Status: design for review. Nothing here is built. The spikes of §12.1 (S1 to S3 and two short
checks) must pass before the slot-sprite part (§4) is coded. Everything else is ready to
implement.

- Evidence: `../research/runtime-catalogs-2026-11.md` (cited as "research §n"). All RVAs are for
  `hoi4.exe` 1.19.4.1 (SHA-256 `CF781ED1…476F`).
- Background: `rename-anything/docs/research/mod-compatibility-kaiserredux.md`, plan (c).
- Write ADR-0039 ("Runtime catalogs") when the work starts. It extends ADR-0036 (portrait layer)
  with path portraits (§7) and follows ADR-0023 (each feature switches off alone).

## 1. What it does

Rename Anything (RA) ships static, generated catalogs built from vanilla files: 2206 flags, 1153
portraits, 5322 character tokens, and 1081 states with their VPs. On a total conversion they are
wrong or empty (KRX note §1). API 9 lets a mod browse what the **running** game has, with no file
generated per mod:

| Catalog | Content | Source (research) |
|---|---|---|
| `flag` | flag stems that exist in `gfx/flags`, `medium/` and `small/` | VFS listing, §5 |
| `portrait` | portrait sprites, plus portrait files under `gfx/leaders/` | sprite database §3.6; VFS §5 |
| `vp` | the VP provinces of one state, with their shown names | state and province objects, §4 |

Three things need **no** CAPIE catalog, because script and the GUI already reach them for any mod:

- countries and tags (`every_country`, `every_possible_country`);
- characters, including KRX's runtime leaders (`every_character`, plus the native `image =
  "portrait"`, research §3.4 and §3.5);
- states and their names (`every_state`, `[This.GetName]`).

API 9 also extends the portrait layer (CONTRACT 18) to accept **texture paths**, so KRX's about 14 000
`picture = "gfx/leaders/….png"` leaders can be applied (§7).

Principles:

1. **Catalogs are local and only for display.** They live in CAPIE's memory on the machine whose
   player is browsing. Nothing about them is saved or synchronised.
2. **Names travel in the command.** When the player picks an item, CAPIE adds the item's **name** (a
   stem, a sprite name or a path), or a province id, to that click's command as records (CONTRACT 5).
   Every client runs the effect with the same records, so the state the mod stores is identical
   everywhere. An index into a catalog is never stored.
3. **CAPIE writes no game state.** As for every layer, it never writes a variable, a country or a
   province. Its only writes are to its own memory, to the texture fields of slot sprites the mod
   declared for it (§4.3), and to command records.
4. **Fail closed.** Without CAPIE, or with a catalog pin failed, the click runs with no catalog
   records, and the mod falls back to its static catalog or to a "not available" message.

Non-goals for v1:

- text search;
- a CAPIE character catalog (script lists characters);
- a cosmetic-tag catalog;
- live updates of an open page when the game changes underneath it (the next page click
  refreshes it).

## 2. The model

```text
 player clicks "next" in a catalog window      (UI thread, clicking machine only)
   │
   ├─ CAPIE dispatch hook: selector capie_v1_cat__rn__fp__next_click
   │     view = local view of (rn, fp): catalog flag, filter own:GER, page 3 → 4
   │     loads page 4 into the hidden slot set (texture re-point, §4.3), flips the set
   │     adds records  capie_cat=1 capie_cn=57 capie_pg=4 capie_pgs=2 capie_par=1 capie_pn=9
   └─ forwards the command (normal game path)
                    │
 every client runs the mod's effect with those records   (logic thread, all machines)
   └─ ROOT.rn_cat_par = 1, ROOT.rn_cat_pn = 9, …          (synchronised script state)
                    │
 the window refreshes (variables changed) on the clicking machine:
   image = "GFX_capie_v1_s__rn__fp__[?ROOT.rn_cat_par]_[?rn_fp_v]"   → set 1, slot k
   text  = "[RnFpRow3]" → CAPIE_V1_T__RN__FP__3                        → CAPIE's local text
```

A pick works the same way. The selector `…__pick_click` carries the clicked slot (§5.2). CAPIE turns
the slot into an item **locally**, then adds the item's name as records. The effect stores the name
in the mod's own variables, and the mod's existing Apply writes the layer from them (CONTRACT 17,
18).

## 3. Catalogs

### 3.1 Building

Each catalog is built once per game session, the first time a view opens it, on the UI thread
inside the dispatch hook (§8.1). It is rebuilt only when its source changes:

| Catalog | Build | Cost (KRX) | Rebuild when |
|---|---|---|---|
| `flag` | `GetFiles("gfx/flags", ".tga", depth 0)` and the same for `medium` and `small`. Keep the stems that appear in all three, are 1..63 characters long and use only the 17.3 alphabet. | 3 listings, about 12 000 entries | never in a session (`replace_path` is fixed after the main menu) |
| `portrait` | (a) Walk the sprite map (research §3.6) and keep each `CSpriteType` whose name starts, case-insensitively, with `GFX_portrait` or `GFX_leader`, and does not end with `_small`. (b) `GetFiles("gfx/leaders", NULL, depth 8)` and keep `.dds`, `.png` and `.tga` paths that pass the path rules (§6.3). (c) Drop a file from (b) when it is the `texturefile` (`+0x70`) of a sprite kept in (a); the sprite wins. | 106 000 sprite nodes walked, about 10 000 files | the sprite map count (`db+0x100`) changes (a GUI reload) |
| `vp` | For the scope state: provinces `[state+0x18]` (count `+0x24`) with VP value `+0x38 > 0`. | 13 provinces at most | every `vp` action (cheap) |

The sort is by byte order of the name (`memcmp`), with duplicates removed. The VP list is sorted by
VP value, highest first, then by province id. Positions are stable within a session and mean nothing
outside it.

### 3.2 Filters (views)

| Action | Catalog | Items |
|---|---|---|
| `flag` | flag | all |
| `flagown` | flag | stem `== T` or starts with `T_`, where `T` is the scope country's tag text (`0xC08100`); also every stem that starts with the country's current cosmetic tag (`+0x1488`) followed by `_`, or equals it |
| `portrait` | portrait | all |
| `portraitown` | portrait | name contains `_T_` (KRX `Portrait_ABK_…`, vanilla `GFX_portrait_POL_…`), or path contains `/T/` |
| `vp` | vp | the VPs of the scope state |

The scope is the clicked window's context scope (`context+0x3C0`): a country for flag and portrait
views, and a state for `vp` (research §4, O4). An `*own` action in a scope that is not a country
gives an empty view, not an error.

### 3.3 Limits

- At most 16 views per process, and 4 per mod id (registered at Bind, like editors).
- At most 65 535 items per catalog. Items past that are dropped and logged.
- Page size = the number of slot sprites the mod declared for the view (§4.1), from 1 to 48.
- At most 1024 distinct textures loaded through catalogs per session, until O1 (texture release) is
  closed. Past the cap, a slot shows the mod's placeholder and the log says so once.

## 4. Display

### 4.1 Slot sprites (declared by the mod)

The mod declares two sets of slot sprites per view in its own `.gfx`. CAPIE never creates sprites:
the sprite map has no lock (research §3.6).

```text
spriteType = { name = "GFX_capie_v1_s__rn__fp__0_0"  texturefile = "gfx/interface/rename_anything/capie_slot_blank.dds" }
...                                                   # set 0, slots 0..N-1
spriteType = { name = "GFX_capie_v1_s__rn__fp__1_0"  texturefile = "gfx/interface/rename_anything/capie_slot_blank.dds" }
...                                                   # set 1, slots 0..N-1
spriteType = { name = "GFX_capie_v1_s__rn__fp__p0"   texturefile = "…blank.dds" }   # preview, set 0
spriteType = { name = "GFX_capie_v1_s__rn__fp__p1"   texturefile = "…blank.dds" }   # preview, set 1
```

- The name grammar is `GFX_capie_v1_s__<mod>__<view>__<set>_<slot>` with `set` 0 or 1 and `slot`
  0..47 in decimal without padding, and the preview `…__p<set>`. The longest is 45 bytes.
- The slot count N is the number of consecutive slots found in set 0 from 0, and set 1 must have the
  same N. Otherwise the view is invalid (sentinel `-3`, §5.4).
- `noOfFrames` must be 1. Every slot texture must be a real file of the mod (the placeholder).
- Without CAPIE the placeholder shows. That is the "nothing to show" state the mod designs.

### 4.2 Which texture a slot shows

| Catalog | Grid slot | Preview |
|---|---|---|
| flag | `gfx/flags/medium/<stem>.tga` | `gfx/flags/<stem>.tga` |
| portrait (sprite) | the sprite's own texture (its `+0x70` path) | the same |
| portrait (path) | the path | the path |
| vp | none (text only) | none |

### 4.3 Re-pointing a slot (UI thread)

For each slot of the hidden set:

1. `s = FindSprite(db, name)`. It must be non-null, and `*(uptr*)s == base + vt_sprite_type`
   (RTTI `.?AVCSpriteType@@`). Otherwise the view is off.
2. `texmgr = *(uptr*)(*(uptr*)(s+0x48) + 0x170)`, which must equal `*(uptr*)(db + 0x170)`.
3. `id = AddTexture(texmgr, &path, &opts{0, 0x1000001, 0}, 1)`, the same options as the lazy
   loader. Then `tex = GetTexture(texmgr, id)` and `size = TextureSize(texmgr, id)`.
4. If `tex == 0` (a missing or unreadable file), keep the placeholder. Then write, in this order,
   `+0x1A0 = id`, `+0x170 = tex`, `+0xA4 = size`. For a slot past the end of the page, write the
   placeholder's values, which CAPIE saved when the view was registered.

These are the three fields the game's own lazy loader `0x22A7F80` writes, with the same game calls
(research §3.6). The texture manager takes its own mutex. The writes run on the UI thread, which owns
the sprites, inside a guarded frame (`__try`; a fault turns the catalog feature off, §8.3). The
`+0x70` path string is never touched.

### 4.4 Why two sets

Whether a widget reads the sprite's texture on every draw is unknown (research O2). So CAPIE always
loads the **hidden** set and then tells the mod to switch (`capie_par`). The `image` text then
changes, the updater calls `SetSprite` with the other name, and the widget binds a sprite that is
already complete. The visible set is never touched while it is shown, except in the double-click
case (§11, R4).

### 4.5 Text: keys CAPIE answers

| Key | Text |
|---|---|
| `CAPIE_V1_T__<MOD>__<VIEW>__<k>` | the label of slot k of the current page: the stem, the sprite name, the file name without its folders, or the province's shown name (`0xEEF270`: the player-given name, else the VP name, research §4). Empty past the end of the page. |
| `CAPIE_V1_T__<MOD>__<VIEW>__SEL` | the label of the picked item |
| `CAPIE_V1_T__<MOD>__<VIEW>__PAGE` | `4 / 7` |
| `CAPIE_V1_T__<MOD>__<VIEW>__COUNT` | `57` |

- `<MOD>` and `<VIEW>` are upper case; `k` is decimal. Keys answer only for a registered view;
  otherwise the original lookup runs, so the SDK fallback (empty) shows.
- Answers are interned and never freed, as the `RN1|` answers are (`country_names.hpp`).
- VP names are computed on the UI thread: when the view is filled, and again at most every 500 ms
  while the view's window is visible (from the existing Tick hook, with the same `ThreadForbidCount`
  check; at most 32 calls of `0xEEF270`). A rename therefore shows within half a second, with no
  click. The localisation hook only returns cached strings and never reads the game.
- Use the keys through a scripted-localisation wrapper (`text = "[RnFpRow3]"`), as RA's
  `RN_VPROW_*` do, unless spike S3 shows that a plain key is re-evaluated (research R-T3).

## 5. Clicks

### 5.1 Selector grammar

```text
scripted GUI   capie_v1_cat__<mod>__<view>                     (a "catalog view"; ids [a-z0-9]{1,12})
action         capie_v1_cat__<mod>__<view>__<action>_click
actions        flag | flagown | portrait | portraitown | vp     (open or switch the view; page 1)
               first | prev | next | last                       (page)
               pick                                            (needs record capie_v1_slot)
```

CAPIE recognises a view at Bind (the same place as editors, `capie_editor.dll`) and checks its slot
sprites then (§4.1). Every action is forwarded as a normal command with records. None is dropped, so
the mod always learns the new state through synchronised variables.

### 5.2 The clicked slot

A dynamic list entry exposes its value as a temporary variable of the click. The mod names it
`capie_v1_slot` (`dynamic_lists = { … value = capie_v1_slot }`). In the dispatch hook, CAPIE reads
that record from the context record vector (`context+0x470`, research §6.4) **before** sanitising
reserved keys. It then removes it, like every incoming `capie_` record. A missing or non-whole
value, or a slot outside the page, gives `pick` sentinel `-4`.

A spoofed slot can only choose another item of the clicker's own local page, so it carries no
authority. Permissions stay the effect's job (CONTRACT 9).

### 5.3 Records (all actions)

| Record | Value |
|---|---|
| `capie_cap`, `capie_ver` | as in CONTRACT 5.1 (`9`, `10700`) |
| `capie_catalog` | `1` when the catalog feature is on (pins, RTTI, no fault), else `0` and nothing below |
| `capie_cat` | the catalog shown: 1 flag, 2 portrait, 3 vp |
| `capie_cn` | items in the view |
| `capie_pg`, `capie_pgs` | the page shown (1-based) and the number of pages (`0` and `0` when empty) |
| `capie_pn` | items on the page shown, `0..N` |
| `capie_par` | the slot set to show, `0` or `1` |
| `capie_ppar` | the preview set to show, `0` or `1`; it flips on every `pick` (CAPIE loads the picked item into the hidden preview sprite first) |

Additional records for `pick`:

| Record | Value |
|---|---|
| `capie_sel_fmt` | `1` = 17.3 packing (flag stems), `2` = packing B (§6.3) |
| `capie_sel_len`, `capie_sel_0` .. `capie_sel_41` | the item's name: a stem, a sprite name or a path. Up to 63 characters (21 elements) in format 1 and 126 (42 elements) in format 2. |
| `capie_sml_len`, `capie_sml_0` .. `capie_sml_41` | portrait only: the small name, packing B. For a sprite: `<name>_small` when that sprite exists, else the name. For a path: the same path. |
| `capie_cos_len`, `capie_cos_0` .. `capie_cos_20` | flag only: the scope country's current cosmetic tag (`+0x1488`) in 17.3 packing; `0` = none. The mod copies it to `capie_cf_cos` as the snapshot, so RA no longer needs a static cosmetic table (KRX note §1.6). A tag outside the 17.3 alphabet gives `capie_cos_len = -1`, and the mod then leaves the snapshot absent (CONTRACT 17.4). |

Additional records for `vp`:

| Record | Value |
|---|---|
| `capie_st` | the state's numeric id. This replaces RA's 1081-branch `rn_load_selected_state`. |
| `capie_vp_0` .. `capie_vp_31` | province ids, in the view order (`capie_cn` of them, at most 32) |
| `capie_vpv_0` .. `capie_vpv_31` | their VP values |

Every name fits the 15-byte short-string layout (the longest is `capie_catalog`, 13 bytes). The
largest `pick` adds 10 + 1 + 43 + 43 + 22 = 119 records; a `vp` action adds 10 + 1 + 64 = 75.

### 5.4 Sentinels

As in CONTRACT 5.2, the click is forwarded with only `capie_cap`, `capie_ver` and
`capie_catalog = -code`:

| Code | Reason |
|---|---|
| `-1` | the window is not visible, or it failed the consistency check |
| `-2` | the view registry is full |
| `-3` | the view is invalid: slot sets missing or of different sizes, or a slot sprite that is not a `CSpriteType` |
| `-4` | `pick` without a valid `capie_v1_slot` |
| `-5` | `ThreadForbidCount != 0` at the click (§8.1) |

## 6. Script contract (CONTRACT.md section 21, API level 9)

### 6.1 Versions (CONTRACT 21.1)

API level `9`, build `10700` (CAPIE 1.7.0). Add the record `capie_catalog` (section 5) to every
grammar Apply too, so an editor can see whether catalogs are available. Add the loc keys of §4.5.

### 6.2 Names (CONTRACT 21.2)

§4.1 (sprites), §5.1 (GUI and selectors), §4.5 (keys) and §5.3 (records). All are under the reserved
`capie_` / `CAPIE_` / `GFX_capie_` prefixes (CONTRACT 2).

### 6.3 Packing B, for names with `/ . - '` (CONTRACT 21.3)

The 17.3 alphabet is full (63 codes) and cannot express paths (research §7). Packing B:

- The allowed characters are `A-Z a-z 0-9 _ - . / '`, and the code is the ASCII value (39..122).
- Three characters per element: `c0 * 16384 + c1 * 128 + c2`, so an element is at most 2 097 151,
  which stays below the classic script limit of 2 147 483. The last element is padded with 0. The
  length is 1..126, so `^num` = ceil(length / 3), at most 42.
- Invalid: a code outside the set; a 0 before the end or a non-zero code after it; a fraction; a
  bad length.

Path rules (CAPIE checks them when it builds a catalog and when it reads a layer):

- the path starts with `gfx/` and ends with `.dds`, `.png` or `.tga` (case-insensitive);
- it has no `..`, no `//` and no leading `/`;
- it is ASCII only.

The 8 non-ASCII portrait files measured in KRX and R56 are left out (research §7).

### 6.4 Without CAPIE, multiplayer (CONTRACT 21.4)

- **Without CAPIE:** the actions run the mod's effect with no `capie_*` record. The slot sprites show
  their placeholder, and the keys show the mod's fallback text. The mod uses its static catalog or a
  message.
- **Multiplayer:** §10.

## 7. Portrait layer, format 2 (CONTRACT 18 amendment)

| Variable | Format 1 (unchanged) | Format 2 (new) |
|---|---|---|
| `capie_pt_ver` | `1` | `2` |
| `capie_pt_s`, `capie_pt_len` | sprite name, 17.3 packing, 1..63 | sprite name **or path**, packing B, 1..126 |
| `capie_pt_ss`, `capie_pt_slen` | small sprite name, 17.3 | small sprite name or path, packing B |

A name containing `/` is a path; otherwise it is a sprite name. Mixing is allowed (a path large, a
sprite small).

What CAPIE does for a path (logic thread, in `update(country)` and `refresh_all()`):

1. Check the path rules (§6.3) and that the file exists. The existence check is
   `GetFiles(<folder>, <file name>, NULL, 0)`, with the result cached per folder for the session.
2. Build the entry with the game's builder `0x14393D0(out, &path, warn=false, flag=false)`. It loads
   the texture at once through the thread-safe texture manager (research §3.1). This is the same thing
   `create_country_leader` does, on the same kind of thread.
3. Keep the built entry in a per-path cache (`std::map<std::string, Entry>`, never freed in the
   session; the textures are refcounted by the game). A layer naming a cached path reuses it.

In the `ResolvePortrait` hook, for a path layer:

- run `PortraitEntryValid(&cached)`, which is true only when the handle is non-zero;
- then fill the caller's buffer as the game's copy does: `set = 1`, `kind = 1`, the name
  copy-constructed with `string_copy`, and the handle copied (it is a `texture*` the game shares the
  same way);
- otherwise the original runs, as today (fail-closed).

Sprite names (kind 0) are unchanged. The console command `change_leader_portrait` accepts a path in
format 2.

Validity adds: format 2 with a bad packing or a path that fails the rules is `invalid`, and the game's
portrait shows.

## 8. Threads, data structures, fail-closed

### 8.1 Threads

| Work | Thread | Guard |
|---|---|---|
| catalog build, view change, slot re-point, records | UI thread, inside the dispatch hook (before the command is sent) | `ThreadForbidCount == 0`, read from the game's TLS slot (`[0x3693A98]`, `+0x10`); otherwise sentinel `-5` and no gamestate read |
| VP label refresh | same | same |
| loc answers | any thread (hooked lookups) | shared lock; returns interned strings only |
| path portrait entries | logic thread (`UpdateNames`, `set_variable` hooks) | as for the portrait layer |
| `ResolvePortrait` hook | UI | unchanged |

### 8.2 Data

```cpp
struct Item { std::uint32_t name;       // offset into the catalog's string pool
              std::uint8_t kind; };      // 0 sprite, 1 path, 2 flag stem
struct Catalog { std::vector<char> pool; std::vector<Item> items; std::uint32_t source_stamp; };
struct View {                            // one per registered (mod, view); never freed
    std::array<uptr, 2 * 48 + 2> sprites; // resolved slot sprites (pointer used only after FindSprite)
    SlotTexture placeholder;              // id/texture/size saved at registration
    std::uint8_t catalog, set_shown;
    std::vector<std::uint32_t> filtered;  // indexes into Catalog::items
    std::uint32_t page, picked;
    std::array<const char*, 48 + 4> text; // interned answers for §4.5
};
```

- Views and catalogs are written only on the UI thread. The loc hook reads `text` under a
  `std::shared_mutex`; the writer swaps whole pointer arrays.
- A sprite pointer is re-resolved with `FindSprite` on every page fill. It is never kept across a GUI
  reload: Bind runs again after a reload and re-registers the view.

### 8.3 Fail-closed

- `catalog_ok = editor hooks ok && catalog pins ok && RTTI ok`. The editor hooks are needed because
  the feature lives in the dispatch hook.
- `catalog_ok` false: `capie_catalog = 0`, no sprite is touched, no key answers, and the editors and
  layers keep working.
- A fault in a game call or a sprite write turns the feature off for the session
  (`game_call_failed()`). Re-pointed slots then keep their last texture, which is harmless and local.
- The portrait path support has its own switch inside the portrait layer: if its two pins
  (`0x14393D0` and `MakeIconEntry`'s texture-manager read) fail, format 2 paths are `invalid` and
  sprite names keep working.

## 9. Pins and RTTI

Signatures are generated with `tools/port/gen_tables.py` at implementation time. The instruction and
target pins below were read on 1.19.4.1 with capstone (research §3 to §5).

```cpp
// features/catalog/catalog_pins.hpp
inline constexpr core::InstructionPin instruction_pins[] = {
    // CSpriteType lazy loader 0x22A7F80 (research 3.6)
    {"sprite.lazy_flag",     0x22A7F94, "388ed3010000"},        // cmp [rsi+1D3],cl
    {"sprite.texture_id",    0x22A7FA0, "83bea0010000ff"},      // cmp dword [rsi+1A0],-1
    {"sprite.path_length",   0x22A7FAD, "48398e80000000"},      // cmp [rsi+80],rcx
    {"sprite.database",      0x22A7FBA, "488b4648"},            // mov rax,[rsi+48]
    {"sprite.texmgr",        0x22A7FBE, "488bb870010000"},      // mov rdi,[rax+170]
    {"sprite.path",          0x22A7FDD, "488d5670"},            // lea rdx,[rsi+70]
    {"sprite.store_id",      0x22A7FE9, "8986a0010000"},        // mov [rsi+1A0],eax
    {"sprite.store_texture", 0x22A7FF9, "48898670010000"},      // mov [rsi+170],rax
    // State provinces and VP (research 4)
    {"state.provinces",      0xA2A18B, "4c8b5918"},             // mov r11,[rcx+18]
    {"state.province_count", 0xA2A17A, "8b7924"},               // mov edi,[rcx+24]
    {"province.vp_read",     0xA2A197, "448b4038"},             // mov r8d,[rax+38]
    {"province.vp_write",    0xEF2746, "895138"},               // mov [rcx+38],edx
    {"province.state",       0xEF275E, "4883b9c000000000"},     // cmp qword [rcx+C0],0
    {"province.custom_name", 0xEEF288, "4883795000"},           // cmp qword [rcx+50],0 (string +40)
    {"province.id",          0xEEEDAB, "8b91a4000000"},         // mov edx,[rcx+A4]
    // Path portrait entries (research 3.1)
    {"icon.texmgr",          0x1438B66, "488b9870010000"},      // mov rbx,[rax+170]
};
inline constexpr core::RipPin target_pins[] = {
    {"sprite.calls_add_texture", 0x22A7FE5, 0x22A7FE9, 0x2488BD0},
    {"sprite.calls_get_texture", 0x22A7FF5, 0x22A7FF9, 0x2489670},
    {"icon.calls_add_texture",   0x1438B7D, 0x1438B81, 0x2488BD0},
    {"icon.calls_get_texture",   0x1438B87, 0x1438B8B, 0x2489670},
    {"vp.notifies_state",        0xEF27BF, 0xEF27C3, 0xA2A170},   // jmp
};
// plus signatures: GetFiles 0x2560AA0, AddTexture 0x2488BD0, GetTexture 0x2489670,
// TextureSize 0x24894A0, ProvinceName 0xEEF270, PathPortraitEntry 0x14393D0, TagText 0xC08100,
// FindSprite 0xA88D30 (map +0x100/+0x104/+0x108), and two pins still to read (research O4):
// the scope's state id (+0xA8) and the state's id (+0x58).
```

**Anchors** for the port tool (research §4 of the Ghidra review, A1):

- `GetFiles` worker: "Reached max depth when enumerating files recursive";
- `ProcessString`: "Too deep recursion in ProcessString";
- the CSpriteType lazy loader: "have forbidden compression, have you tried DXT3?";
- the VP setter assert at `0xEF279A` and the VP key strings `_VICTORY_POINTS_` / `VICTORY_POINTS_`
  (`0xEEED80`);
- the history command `CSetStateVictoryPoints` ("set victory points takes 2 parameters").

**RTTI** (new table `catalog_vtable_classes`):

| id | RVA | name |
|---|---|---|
| `vt_sprite_type` | `0x2BB3420` | `.?AVCSpriteType@@` |
| `vt_set_state_vp` | `0x2A546B8` | `.?AVCSetStateVictoryPoints@@` (anchor only) |
| `vt_legacy_create_leader` | `0x27E39C8` | `.?AVCLegacyCreateCountryLeaderEffect@@` (anchor only) |

**`game_layout.hpp`** additions:

- `rva::`: `vfs_get_files`, `texture_add`, `texture_get`, `texture_size`, `portrait_path_entry`,
  `province_name`, `tag_text`, `thread_forbid_tls_index`;
- `off::`: `sprite_map = 0x100`, `sprite_texture_manager = 0x170` (db), `sprite_db = 0x48`,
  `sprite_path = 0x70`, `sprite_size = 0xA4`, `sprite_texture = 0x170`, `sprite_texture_id = 0x1A0`,
  `gamestate_states = 0x2C8`, `gamestate_state_count = 0x2D4`, `state_provinces = 0x18`,
  `state_province_count = 0x24`, `province_vp = 0x38`, `province_custom_name = 0x40`,
  `province_id = 0xA4`, `province_state = 0xC0`, `tls_thread_forbid = 0x10`.

## 10. Multiplayer

The claim: **API 9 cannot cause an out-of-sync**, even when two clients' catalogs differ.

1. **What enters game state.** Only the effect's writes, made from records that travel **inside the
   command** (CONTRACT 5). Each client executes the same command with the same records, so each writes
   the same values. Which records a command carries is decided once, on the clicking machine. This is
   the mechanism the editors (API 2) and `capie_pf1_*` (API 5) already use. It holds if records survive
   network serialisation, the open item of CONTRACT 12 (multiplayer test plan).
2. **What the records contain.** Names (stems, sprite names, paths), province ids and the state id,
   page numbers and counts. Province and state ids are read from synchronised game state, so they are
   the same everywhere anyway. Names come from the clicker's local catalog. Another client may not
   have that file or sprite (a DLC-only sprite, a locally edited mod). It still stores the same name,
   and only its **display** differs. The layers already fail closed per client: CONTRACT 17.6
   (missing file: the game's flag) and 18.5 (missing sprite: the game's portrait). Path portraits do
   the same through the handle check (§7).
3. **What stays local.** Views, pages, slot textures, labels and the picked index are never written to
   game state, so they cannot diverge it.
4. **Identity.** RA stores names. A save is valid on any machine and after any mod update, as long as
   the named file or sprite still exists. If it does not, the game's own flag or portrait shows. This
   is the rule of CONTRACT 18.1 ("catalog indexes are forbidden"), now enforced by construction.
5. **No catalog hash in the handshake.** The KRX note proposed one. It is not needed, because no
   index crosses the network. The log line `CAPIE_CATALOG built flag=3974 portrait=12011 hash=…`
   keeps a hash for diagnostics only.
6. **Same build.** CONTRACT 12 (same CAPIE build on all clients) still applies to the layers.

Mods must not branch an effect on `capie_cn` or other catalog counts in a way that changes saved state
beyond storing them. They are the clicker's local counts. They travel in the command, so they are
consistent, but they describe one player's machine.

## 11. Rename Anything side

Mirror the existing pickers. Generate the slot sprites, the scripted-localisation wrappers and the
grammar names with `tools/gen.py`, once and not per mod.

**Flag picker** (`rn_flag_picker`, view `fp`, 40 slots):

1. The "Flag" button in the politics or diplomacy view runs `capie_v1_cat__rn__fp__flagown_click`
   (scope: target country).
   - The effect: if `capie_catalog = 1`, store `rn_cat_par`, `rn_cat_pn`, `rn_cat_pg`, `rn_cat_pgs`
     and `rn_cat_cn`; rebuild `ROOT.rn_fp_cells` as `0..pn-1`; open the runtime window.
   - Otherwise: open the static vanilla picker as today. Show it only when the KRX-style "foreign
     content" signal is off (KRX note (d)3); else show "Flags not available for this mod without
     CAPIE".
2. The grid is a dynamic list over `ROOT.rn_fp_cells` with `value = capie_v1_slot`. Its image is
   `GFX_capie_v1_s__rn__fp__[?ROOT.rn_cat_par]_[?capie_v1_slot]`. The tooltip is `[RnFpRowN]`, done
   with one wrapper per slot, or left out.
3. "Own / All" buttons: `…__flagown_click` and `…__flag_click`. Paging: `…__prev_click`,
   `…__next_click`. The page label is `[RnFpPage]`.
4. A cell click runs `…__pick_click`. The effect copies `capie_sel_*` into `ROOT.rn_fp_sel_s`
   (17.3, format 1) and `capie_cos_*` into `ROOT.rn_fp_cos`. The preview is `GFX_capie_v1_s__rn__fp__p[?ROOT.rn_fp_ppar]`
   (`rn_fp_ppar` is stored from `capie_ppar`; CAPIE has already loaded the large flag into that
   set).
5. "Apply" (unchanged rules, cooldown, rights): `rn_country_flag_carrier_set` writes `capie_cf_*`
   from `rn_fp_sel_s` and `rn_fp_cos`.
6. **Removed:** `rn_flag_catalog.*` (2206 sprites), `rn_fp_group_of`, `rn_fp_cos_write` and
   `rn_fp_cos_match`. They are kept only as the no-CAPIE fallback, and only for vanilla.

**Portrait picker** (`pp`, 24 slots): the same flow, with `portraitown` / `portrait` actions. A pick
stores `capie_sel_*` and `capie_sml_*` (packing B, format 2) on ROOT. Apply writes the CONTRACT 18
format 2 variables on the ruling leader (§7). KRX's path portraits and mod sprites appear without a
generated catalog.

**Characters list** (no CAPIE catalog):

- `every_character` into `ROOT.rn_chars` (already done);
- a dynamic list with `change_scope = yes` (spike R-T1);
- `image = "portrait"` (native, R2) and `text = "[This.GetName]"`;
- for portraits it removes `rn_char_portraits.txt` (855 KB) and `.gfx` (356 KB), and it works for
  KRX's runtime leaders because they are real characters (research §3.4);
- order: the game's order, as today. Sorting by name is not possible in script; a CAPIE character
  catalog could add it later (non-goal).

**Cities list** (`vp`, 16 rows; no sprites):

1. Selecting a state (the state list entry with `change_scope = yes`, or the state view's
   `selected_state_view` window) runs `capie_v1_cat__rn__vp__vp_click` in state scope.
2. The effect: `rn_sel_state_id = capie_st`; `rn_vp_cnt = capie_cn`; `rn_vp_k = capie_vp_k`.
   This replaces `rn_load_selected_state` and its 1081 branches.
3. Row k shows `[RnVpRowK]` → `CAPIE_V1_T__RN__VP__K`, the province's shown name (custom name, else
   the tag-specific VP name, else the VP name). This replaces the 25 000 lines of
   `RN_VPROW` loc buckets.
4. Rename: `set_province_name = { id = [P] … }` through `meta_effect` from `rn_vp_k`, as today. The
   row label follows within 500 ms (§4.5); no extra click is needed.
5. Without CAPIE: on vanilla, the generated table as today; otherwise "Cities not available for this
   map without CAPIE".

**Load order** (not API 9, but needed for any TC test): add `dependencies` to RA's descriptor for the
script-replacing TCs (research §8: the launcher neither blocks nor needs them installed). Name the
dev descriptors `zz_*.mod`. An optional CAPIE self-check logs "no `rn_` scripted GUI templates
loaded" when the template list (`[0x33DD4A8]+0x28`) has none of RA's model names at game start.

## 12. Tests

### 12.1 Spikes (before coding §4; one evening each)

| # | Question | Pass |
|---|---|---|
| S1 | Re-point one declared sprite (`+0x1A0/+0x170/+0xA4`) from a debug console command to `gfx/flags/GER.tga` | the image shows GER after the next refresh, no crash, nothing in error.log |
| S2 | Two sets: switch the image text between set 0 and set 1 after re-pointing the hidden one, 200 times | always the right texture, no flicker, no leak beyond the texture cap |
| S3 | A plain `CAPIE_V1_T__…` key in `text` versus a scripted-localisation wrapper, after a variable change | which one updates (decides §4.5) |
| R-T1 | (RA) `dynamic_lists` with `change_scope = yes` over characters, `image = "portrait"` | the portraits show, including a KRX `create_country_leader` leader |
| O4 | read the scope's state id on 1.19.4.1 (`0x53B5F0`, `0xB8D1E0`) | two instruction pins |

### 12.2 Offline (`tests/catalog_test.cpp`, diagnostics 0 and 1)

1. **Packing B:** round trip of every measured path; rejects (`"`, a space, non-ASCII, a 0 in the
   middle, length 127, a wrong `^num`, an element over 2 097 151); the path rules (`..`, `//`,
   leading `/`, `gfx/` missing, a wrong extension, mixed case extension accepted).
2. **Catalog build** over a fake `GetFiles` and a fake sprite map: the three-size intersection; the
   17.3 filter; `_small` excluded; a sprite's file dropped from the paths; byte-order sort; dedupe;
   the 65 535 cap.
3. **Filters:** `flagown` for `GER`, for a cosmetic `GER_KAISERREICH`, and for a dynamic `D01`;
   `portraitown` for `_ABK_` and `/ABK/`; a non-country scope gives an empty view.
4. **Paging:** first, prev at 1, next at the end, last; `pn` on the last page; an empty view.
5. **Records:** exact names and values per action; `pick` of slot 0, N-1 and N (sentinel `-4`);
   `capie_v1_slot` removed; the 15-byte limit; the record count limit.
6. **Slots:** the fake sprite has the wrong vtable (view off); texture missing (placeholder kept);
   write order `id`, `texture`, `size`; the hidden set only; the texture cap.
7. **VP view** over fake states and provinces: order by value, then id; VP 0 excluded; 32 cap;
   custom name first.
8. **Loc keys:** unknown view → original; an interned pointer is stable across pages; upper case.
9. **Portrait format 2:** decode; path versus sprite; the builder is called once per path; a
   handle of 0 → the game's portrait; format 1 unchanged.
10. **Thread guard:** `ThreadForbidCount != 0` → sentinel `-5`, no read.

### 12.3 EXE pin test (`tests/catalog_pins_exe_test.cpp`)

The colour layer's method (spec §9.2): all pins and the RTTI table pass on 1.19.4.1, and one flipped
byte per kind is detected.

### 12.4 Mutants (`catalog-*`)

| Mutant | Killed by |
|---|---|
| intersection → union of sizes | build test |
| keep `_small` sprites | build test |
| re-point the visible set | slot test |
| write `+0x170` before `+0x1A0` | slot order test |
| skip the vtable check | slot test |
| keep `capie_v1_slot` | records test |
| index instead of name in `capie_sel_*` | records test |
| packing B `* 128` → `* 64` | packing test |
| allow `..` | path test |
| VP order ascending | VP test |
| answer keys of an unregistered view | loc test |
| skip the ThreadForbidCount check | guard test |

### 12.5 In-game (CONTRACT section 13 rows)

Run on three set-ups: **vanilla**, **KaiserreduX** (RA with `dependencies` or a `zz_` descriptor),
and **The Road to 56** (additive, 3212 flags).

| # | Steps | Expect (vanilla / KRX / R56) |
|---|---|---|
| T1 | Open the flag picker on GER, "own" | vanilla GER flags / KRX `GER_totalist` etc. / R56 GER set; counts match the file lists (2206 / 3974 / 3212 in "all") |
| T2 | Page to the end and back, quickly | right images, page label right, no flicker beyond one frame, no error.log lines |
| T3 | Pick and Apply a flag; check large, medium, small | all three the same file (no mixed sizes) |
| T4 | KRX: a country with a KRX-only tag (e.g. `DKB`) | own flags listed (static RA had none) |
| T5 | Cosmetic change after a pick (event `set_cosmetic_tag`) | the snapshot from `capie_cos_*` hides the custom flag at once; RA's daily check clears it |
| T6 | Portrait picker "own" on KRX ABK | `Portrait_ABK_*.png` paths listed; Apply on the ruling leader shows it in politics and diplomacy |
| T7 | Portrait picker on vanilla POL | the same sprites as RA's static catalog for POL, plus any DLC sprite present |
| T8 | Characters list on KRX | runtime leaders with their real portraits (R-T1) |
| T9 | Cities: state 1000 and a state > 1081 on KRX | KRX VPs (`10472`, `13671` for state 1000); names from KRX localisation; rename works and the new name shows within a second |
| T10 | Save, quit, load; then load the same save without CAPIE | names kept; without CAPIE: game flags and portraits, pickers show the fallback message |
| T11 | Multiplayer, 2 clients, vanilla + KRX: A picks and applies a flag and a path portrait while B watches; B hot-joins later | same flag and portrait on both; no out-of-sync; B's own picker works independently |
| T12 | Break one catalog pin (wrong build copy) | init line "catalog off <pin>"; pickers fall back; layers and editors work |
| T13 | Measure a page flip with 40 KRX portraits | record the hitch; acceptable under about 150 ms |
| L1 | Launcher: RA with `dependencies = { "KaiserreduX" "Kaiserreich" }`, neither installed | no warning, no block; the game starts; RA loads (research §8) |

## 13. Risks

| # | Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|---|
| R1 | A sprite re-point races the renderer (draw on another thread while CAPIE writes) | low-medium | one wrong frame, or a crash if the renderer reads a half-written pointer | write order id → texture → size; only the hidden set; S1/S2; a 64-bit aligned pointer store is atomic on x64 |
| R2 | Textures are never released (O1) | certain until O1 is closed | memory grows while browsing | cap of 1024 textures per session; find the release function |
| R3 | A slow page flip (synchronous loads of 40 PNGs) | medium | a hitch up to about 150 ms | T13; later, load `loadNow = 0` then `GetTexture` lazily on draw |
| R4 | A double click before the first command executes re-points the visible set | low | a page shows briefly under the wrong label | the label comes from the same records; it settles within one tick |
| R5 | A GUI reload (`reload gfx`) frees the sprites | low (developer only) | a stale pointer | sprites are re-resolved with `FindSprite` on every fill; the view is re-registered at Bind |
| R6 | `GetFiles` result never freed (O3) | certain | a few hundred KB once | accept, or find the CArray destructor |
| R7 | The UI thread is not allowed gamestate access in some state | low | wrong reads | `ThreadForbidCount` check per click (sentinel `-5`) |
| R8 | Prefix rules for portraits miss a mod's naming (`GFX_KX_leader…`) | medium | some sprites not offered | the path catalog still offers their files; add rules from T6/T7; later a `portrait_prefixes` loc key a mod can extend |
| R9 | Records do not survive multiplayer serialisation | unknown (CONTRACT 12) | picks lost in MP | the same open item as every editor; T11 |
| R10 | Port cost: 21 new pins and RTTI | certain | port work | anchors in §9; the same tools as for 1.19.4.1 |
| R11 | A total conversion is not in RA's `dependencies` list and unloads RA | medium | RA is dead on that TC | the CAPIE self-check log; a quick list update |

## 14. Order of work

1. Spikes S1 to S3, O4, R-T1 (§12.1).
2. Packing B and portrait format 2 (§6.3, §7). These are independent of the sprite work and give KRX
   path portraits through the console at once.
3. The `vp` view (no sprites). It replaces RA's biggest generated table.
4. The flag and portrait catalogs with slot sprites.
5. RA 0.4: switch the pickers to the runtime views when `capie_catalog = 1`; keep the static
   catalogs as the vanilla fallback.
