# Country map colour: storage, redraw and runtime changes (HOI4 1.19.4.0)

Status: research only, read-only static analysis in Ghidra 12.1.4 (via REA) plus a capstone linear
sweep of `.text`. Build: `hoi4.exe` SHA-256 `B6650C85…D888`, image base `0x140000000`. Addresses are
absolute; RVA = address minus `0x140000000` (`0x140759370` is RVA `0x759370`, CAPIE's
`country_set_cosmetic_rva`). Second pass (same day) settled the open points; see section 6. The
implementation spec is `../contributing/country-colour-layer-spec.md`.

Follows up `ghidra-review-2026-10-28.md` (B3) and section 6 of
`capie-wt-anchors/docs/research/subideology-rename-design.md`.

## Summary

- Each `CCountry` embeds two `CCountryColors` (RTTI `.?AVCCountryColors@@`): the **current**
  colours at `+0x350` and the **base** colours (from `colors.txt`) at `+0x3B0`. Each holds `color`
  and `color_ui`, both `CColor` (RTTI `.?AVCColor@@`) with four `float`s (RGBA, 0..1).
- **Every reader of the current block goes through the getter `0x140734AE0`.** A sweep of all
  displacements `0x350..0x3AF` found no other reader; only writers and copies (section 6.1).
- The getter has 16 caller functions (22 call sites). None is AI, checksum or a save writer. One
  piece of game state depends on it: the **faction colour** (`faction+0x9D0`, floats `+0x9E0`). It
  is copied from the leader's colour when a faction is created and in `0x140AB7F10`, and it **is
  saved** (6.2).
- `0x140AB7F10(map, &tag)` repaints the political map mode (mode 0) on the next map update. Other
  modes that read the colour (mode 19, claims) are only rebuilt when their own dirty byte is set
  (6.3).
- `color` drives the map, borders and the faction colour. `color_ui` drives UI icons. Parsing runs
  both through an HSV step: `NGraphics.COUNTRY_COLOR_*` for `color`, `COUNTRY_UI_COLOR_*` for
  `color_ui` (6.4).
- Colours are not saved. After a load they come from `colors.txt` plus the cosmetic tag (6.5).
- **Verdict:** a getter detour plus `0x140AB7F10`, driven by synchronised script variables, is enough
  and is safe in multiplayer. It needs every client to update CAPIE's cache at the same logic step.

## 1. Storage

### `CCountryColors` layout (size 0x60) — confidence high

The cosmetic loader `0x1401E34F0` (`"common/countries/cosmetic.txt"`) allocates `0x60` bytes and sets
the vtable `0x14279F558`. It builds two `CColor`s (vtable `0x142796208`; ctors `0x1422B4780` rgba and
`0x1422B47B0` white) and reads tokens.

| offset in `CCountryColors` | content |
|---|---|
| `+0x00` | vtable `0x14279F558` |
| `+0x10` | `CColor color` (vtable; floats R,G,B,A at `+0x20..+0x2F`) |
| `+0x30` | `CColor color_ui` (floats at `+0x40..+0x4F`) |
| `+0x50` | `u8` flags: bit0 `color` read, bit1 `color_ui` read |

Token reader `0x140752D00` (vtable slot `+0x20`): token `0x56` = `color`, `0x31B8` = `color_ui`.
Each value is read by `0x1425438B0`, adjusted by `0x14074B4B0` (6.4), then assigned and flagged.

### Inside `CCountry` — confidence high

`CCountry` (vtable `0x142864710`, RTTI `.?AVCCountry@@`) holds:

- `+0x350` current colours (floats `+0x370` and `+0x390`, flags `+0x3A0`);
- `+0x3B0` base colours (floats `+0x3D0` and `+0x3F0`, flags `+0x400`);
- `+0x1488` cosmetic tag.

The functions that touch these fields:

- `CCountry::SetColors` `0x140759340` copies `[rdx+0x20]`, `[rdx+0x40]` and `[rdx+0x50]` into
  `+0x370`, `+0x390` and `+0x3A0`.
- The token reader `0x14074DE70` parses into the current block, then copies it to the base block.

## 2. The getter and its consumers

```c
// 0x140734AE0  const CCountryColors* GetMapColors(CCountry* c)   (0x65 bytes, leaf + 2 calls)
while ((rel = *(*(c+0xF88)+0x3B8)) && (lvl = *(rel+0x28))) {   // subject relation
    if (!*(u8*)(lvl+0x2C) && *(int*)(FUN_140210e60()+0x2D0) != 1) break;   // game rule
    c = FUN_140c07b80(*(c+0xF88)+0x188);                         // -> overlord
}
return c + 0x350;
```

Prologue `40 53 48 83 EC 20 48 8B D9`. The loop's back edge jumps to `0x140734AF0`, past the
first 6 bytes, so a 5-byte detour is safe. No caller inlines the getter: the sweep found no other
function that reads `+0x3B8` and computes `+0x350`.

### Map modes and refresh — confidence high (code), medium (mode names)

- The map singleton is `[0x1433E7818]`; the getter `0x140AB7E30` returns it.
- The map manager is at `[map+8]`, and the map-mode container is at `[[map+8]+0x30]` ("modes").
- `modes+0x18` is the array of 23 modes. `modes+0x48+i` is the dirty byte of mode `i`.

`0x140FB7F80(modes, i, 0)` rebuilds mode `i` through a 23-way jump table. Mode 0 is `0x140FB8250`,
the per-country builder that reads `GetMapColors()+0x10`. Mode 19 is `0x140FBAE80` (claims,
`pClaim->_Claimant`); it reaches the getter through `0x140FB4780`.

```c
// 0x140AB7F10  RefreshCountryColor(map, const int* tag)
FUN_140e86c10(*(map+8), tag);   // entry[0][idx-1]+0x48 = 1 (0x140FBFC30, NO bounds check);
                                // modes+0x48 = 1   (mode 0 dirty)
if (faction = faction_of(tag) && !faction+0x9C0 /*no custom colour*/ && leader == tag)
    *(u128*)(faction+0x9E0) = *(u128*)(GetMapColors(country)+0x20);   // GAME STATE, saved
```

`0x140AB8040(map, 0)` rebuilds all 23 modes directly (`0x140FBEE80`, `0x140FBF480`) and re-applies
the current one (`0x140E85DA0`). It skips this when `[0x1433DD08C]` is set (the `-quickstart`
command-line switch).

## 3. Runtime colour changes by the game

- **Cosmetic tag** `0x140759370`: `UpdateNames` (`0x14075E660`), then `0x1401F13A0`, which copies
  the cosmetic or base colours into `+0x350`. If they changed, it calls
  `RefreshCountryColor(0x140AB7E30(), this+8)` under the guard in 6.3. The save loader does the same
  for token `0x372F` (`0x1407511B4`).
- **Console `tag_color TAG R G B`** `0x1402A4A10`: `CColor(r/255, g/255, b/255, 1)` for both colours,
  with **no HSV adjustment**, then `SetColors` and `RebuildAllMapModes(map, 0)`. Local and unsaved.
- **Reset** `0x14072A920`: copies base into current next to a cosmetic-tag reset (`+0x1488`).
- **Subject relation change** `0x140D8B040` (`CSubjectRelation` vtable `0x1429DD328` slot `+0xA8`):
  snapshots `GetMapColors`, applies the change, and refreshes if the colour differs.

## 4. Cosmetic tags

`cosmetic.txt` entries are a hash map at `gamestate+0x8E8`. A cosmetic tag change **overwrites** the
current block. So a write to `+0x350` would be undone. A getter override wins regardless.

## 5. Save and multiplayer (short)

Colours are not saved. The `CCountry` writer `0x1407612D0` saves only the cosmetic tag (`0x372F`).
The faction writer `0x140DFA770` **does** save the faction colour:

```
lea r8,[r14+0x9D0]; mov edx,0x56; call 0x142545430      ; "color" = faction CColor
movzx r8d,byte[r14+0x9C0]; mov edx,0x4205; call ...     ; custom-colour flag
```

The multiplayer argument is in 6.2.

## 6. Resolved points (second pass)

### 6.1 Direct readers of `CCountry+0x350..+0x3AF` — resolved, getter override is enough

Method: a capstone linear sweep of the whole `.text`, with skipdata so that leaf functions without
`.pdata` are included (`SetColors` has none). It listed every memory operand with displacement
`0x350..0x3AF` (4 108 hits). These were filtered for:

- 16-byte and `movss` float accesses at `+0x370..+0x39C`;
- `lea` of `+0x350`, `+0x360` or `+0x380` followed by a call;
- `add reg, 0x350..0x390`;
- trivial getters (`lea rax,[rcx+0x350]; ret` and the like);
- inlined copies of the getter.

Each candidate was then checked for a CCountry base: same function, same base register as `+0xF88`
or `+0x1488`, or an RTTI check of the vtable.

| site | kind | class |
|---|---|---|
| `0x1407117A0` (CCountry ctor, `0x140711C21..C39`, `0x140712E2B..E5A`) | init: white `CColor`s, default colour, `color_ui = color` | write |
| `0x14074DE70` (`0x14074FC6B..FC98`, `0x140751165`) | parse; copy current to base; cosmetic apply | write/save-load |
| `0x14072A920` (`0x14072ACC9..ACEC`) | copy base to current | write |
| `0x140759340` SetColors, `0x140759370` SetCosmeticTag (`0x1407594E8`) | write | write |
| `0x140734AE0` getter (`0x140734B38` `lea rax,[rbx+0x350]`) | the only reader | — |

Rejected look-alikes:

- `0x14065E660` and `0x140B52530` are memberwise copies of a different POD.
- `0x140D05300` (`lea rax,[rcx+0x350]; ret`) is `CResourceOrigin` slot `+0x78`.
- `0x140DA0FB0` (`+0x380`) is applied to `country+0xF88`.
- The `0x1422xxxx` float hits are engine classes (timers and rects).
- The `add reg,imm` hits are stack frames and serialisers of other classes.

**Conclusion:** no map, UI, AI, checksum or save code reads the block directly. Overriding the
getter covers every reader. Writers do not matter, because the override ignores the block's content.

### 6.2 The 16 getter callers — resolved

| caller | reads | role | class |
|---|---|---|---|
| `0x1402CE300` | — (result unused) | `history_dump` console command | debug |
| `0x140AB7F10` | `+0x20` | refresh: dirty flags plus **faction colour `+0x9E0`** | display + game state |
| `0x140B9D1D0`, `0x140B9D320` | `+0x40` color_ui | UI colour objects (callers `0x14197EE40`, `0x141A25280` → `CNavalMissionMapIcon`) | UI |
| `0x140D8B040` | whole block | `CSubjectRelation` change: compare, then refresh | gameplay command, display effect |
| `0x140DF1F40` | `+0x20` | faction creation (`CCreateFactionCommand`, `CCreateFactionAction`): **faction colour `+0x9E0`** | game state |
| `0x140E38240` | `+0x40` | "Map look saved:" export | debug |
| `0x140E83660`, `0x140E86C60`, `0x140E8AC10` | `+0x10`/`+0x20` | map renderer (map-mode apply `0x140E85DA0`, frame update `0x140AB8AD0`) | map |
| `0x140FB4780` | `+0x10` | mode 19 (claims) builder | map |
| `0x140FB8250` | `+0x10` | mode 0 builder (political) | map |
| `0x14150FA50` | `+0x20` | `CCountryFactionView` | UI |
| `0x1419630F0`, `0x141EFBB40` | `+0x40` | UI widgets (exact view not named) | UI |
| `0x141EAEDD0` | `+0x20` | `CFactionColorPickerWindow` (initial colour) | UI |

**Sync argument:**

- The only game-state write that depends on the getter is the faction colour. `0x140DF1F40` writes
  it during a synchronised command. `0x140AB7F10` writes it during effect execution (cosmetic tag)
  or on load. `0x140DFA770` saves it.
- These writes run on the logic thread, at the same step on every client. Their value is the
  getter's result.
- If CAPIE's override is a pure function of synchronised country variables, every client computes
  the same faction colour. This requires that CAPIE update its cache synchronously inside the
  effect that commits the variables (the `set_variable` hook) and in `UpdateNames`, never deferred
  to a UI frame.
- A hot-join receives the variables and the faction colours in the save, then rebuilds the same
  cache on load.
- It fails only if one client runs without the layer: different exe, failed pins, or a runtime
  self-disable. CONTRACT 12 (same build on all human countries) covers the first two. The last is
  a listed risk.
- Whether the faction colour is part of the out-of-sync checksum is unknown. It does not matter
  while the clients agree.

### 6.3 Repaint — resolved (static), needs one in-game check

The frame update is `CInGameIdler` (vtable `0x1429E5828` slot `+0x20`) → `0x140E41600` →
`0x140AB8AD0` → `0x140E8B920` → `0x140FBF5A0(modes, …, now)`. This runs on the UI/main thread and
is throttled by a timer at `modes+0x70`:

```c
for (i = 0; i < 23; i++) {                       // 0x140FBF7A0: cmp byte[r14+rbx+0x48],0
    if (modes[0x48+i]) { modes[0x48+i] = 0; FUN_140fb7f80(modes, i, 0); }   // rebuild mode i
    FUN_141638380(mode[i], …);                   // per-entry refresh (checks entry +0x48)
}
… FUN_14170f1a0(…)                               // upload the active mode(s)
```

- `0x140AB7F10` sets `modes+0x48` (mode 0) and the entry flag. The next update therefore rebuilds
  mode 0 for **all** countries (so subjects drawn in the overlord's colour follow), then uploads it.
  So `0x140AB7F10` alone repaints the political map. It does not dirty mode 19 or the other modes,
  so those stay stale until something rebuilds them. Vanilla has the same staleness after a
  cosmetic tag change.
- To repaint everything without `0x140AB8040`, CAPIE can set `modes+0x48+i = 1` for `i = 0..22`,
  from the same logic-thread point. The game itself writes these bytes from that thread (via
  `0x140E86C10`). The UI-thread update then rebuilds each mode.
- `0x140AB8040` rebuilds synchronously and touches render-side objects. It must run only on the
  UI/main thread: the game calls it from the console, idlers and nudgers. CAPIE should not call it
  (the core DLL has no UI-thread hook).

**Guard** (copied from the game, `0x1407594FF..0x140759516`):

- `[0x1433DDB08]` is the current idler (written by `CGameIdler` slot `+0x58` at `0x1402BA1A0`);
- its vtable slot `+0x2D8` returns 1 only for `CInGameIdler` (`0x140188D90` `mov al,1`); the base,
  front-end and nudge idlers return 0 (`0x14011D530`);
- add `[0x1433E7818] != 0` (map exists).

**Do not call** when:

- not in game (front end, nudger, map not built);
- the country's list index is out of range (`0x140FBFC30` has no bounds check);
- on a thread other than the game's logic thread (the faction colour is game state).

During a save load the game itself calls it under this guard (`0x1407511B4`), so the same call from
`UpdateNames` is at parity.

### 6.4 `color` versus `color_ui`, and the HSV step — resolved

- **`color`** feeds the map modes (0 and 19), the map renderer (borders and fills,
  `0x140E8xxxx`), the faction colour, the faction view and the faction colour picker.
- **`color_ui`** feeds UI icons and widgets and the map-look export.
- Flags are textures and use neither.
- `color_ui` is not derived from `color` at parse time. The CCountry ctor copies `color` into
  `color_ui` only as a default.

`0x14074B4B0(float rgb[3], dH, mS, mV)`:

- converts RGB to HSV with `0x1425757E0`, using hue in [0,1]; the HSV-to-RGB step `0x1425756A0` uses
  sector `min(floor(6H),5)`;
- applies `H += dH`, `S *= mS`, `V *= mV`, with no clamp;
- converts back to RGB.

The define names come from the registration code `0x140894BA0..0x140896880`:

| global | define (`NGraphics`) | vanilla |
|---|---|---|
| `0x1433E5130` | `COUNTRY_COLOR_HUE_MODIFIER` | 0.0 |
| `0x1433E51D4` | `COUNTRY_COLOR_SATURATION_MODIFIER` | 0.6 |
| `0x1433E5268` | `COUNTRY_COLOR_BRIGHTNESS_MODIFIER` | 0.8 |
| `0x1433E5308` | `COUNTRY_UI_COLOR_HUE_MODIFIER` | 0.0 |
| `0x1433E5384` | `COUNTRY_UI_COLOR_SATURATION_MODIFIER` | 1.0 |
| `0x1433E542C` | `COUNTRY_UI_COLOR_BRIGHTNESS_MODIFIER` | 1.0 |

**Decision:** the override takes **one RGB** and sets **both** colours, each through its own define
set, reading the live globals so modded defines apply. Map `color = adjust(rgb, COUNTRY_COLOR_*)`;
`color_ui = adjust(rgb, COUNTRY_UI_COLOR_*)`; flags `3`. Picking the `colors.txt` RGB of a country
then gives exactly that country's map colour. `tag_color` skips this step, which is why its
colours look brighter than vanilla.

### 6.5 Persistence and lifecycle — resolved (design)

- **Load.** The script variables are saved. CAPIE rebuilds its cache from them in `UpdateNames`,
  which the game calls for each country on load, as for the other layers. Changed entries refresh
  under the guard. The faction colour is already in the save.
- **Cosmetic tag change.** The game overwrites `+0x350`, but the override still wins. For
  consistency with the flag layer (CONTRACT 17.4), the colour layer keeps a cosmetic-tag snapshot
  and hides itself when the tag no longer matches. The mod clears it daily. `SetCosmeticTag` calls
  `UpdateNames` before its own colour refresh, so CAPIE's gate is current when the game repaints.
- **Civil war or new dynamic country.** The breakaway is another `CCountry`. Whether
  `start_civil_war` copies country variables is unverified: needs an in-game test. Rename Anything
  clears the layer on the breakaway in its civil-war on_action if they are copied.
- **Annexation.** The `CCountry` and its variables persist. The override returns if the country is
  released, like the name and flag layers. A mod may clear it on annexation.
- **Revert.** The script clears the variables and commits. CAPIE drops the entry and refreshes, so
  the getter returns `+0x350` again (cosmetic or base colour).
- **Subjects in the overlord's colour.** CAPIE looks up the country the getter resolved to (after
  the overlord walk). A puppet painted in its overlord's colour shows the overlord's override, and
  its own override shows only when it is not painted in the overlord's colour.

### 6.6 Pins — resolved

The full table, in pin-table style, is in the spec (section 7). In short:

- signatures: the getter `0x734AE0` and `RefreshCountryColor` `0xAB7F10`;
- instruction pins:
  - getter `lea rax,[rbx+0x350]` at `0x734B38`;
  - `SetColors` field copies at `0x759340..0x75935A`;
  - the guard: `0x7594FF` and `0x75950E`, and `CInGameIdler` slot `0x188D90`;
  - modes and dirty bytes: `0xAB7F16`, `0xE86C20`, `0xE86C48`, `0xFBF7A0`, `0xFBF819`;
- rel32 and RIP pins: the call sites `0x759518`, `0x759524` and `0xAB7F7C`, and the six define
  loads `0x752D37..0x752D9E`;
- RTTI: `CCountryColors`, `CColor`, `CCountry` and `CInGameIdler`.

Failure means the layer is off and the getter is not hooked, so colours stay vanilla.

## Remaining unknowns

1. That mode 0 is the political map mode and mode 19 the claims mode (inferred from builders and
   strings). The in-game test decides whether marking all 23 modes is needed.
2. The two UI callers `0x1419630F0` and `0x141EFBB40` are not tied to a named view (they read
   `color_ui`).
3. Whether `start_civil_war` copies country variables to the breakaway.
4. Whether the faction colour is in the out-of-sync checksum (it does not matter while all clients
   agree).
5. The overlord game rule `FUN_140210E60()+0x2D0` and autonomy `+0x2C`. They are not needed:
   CAPIE calls the original walk.
6. Thread identity ("logic" versus "main") is inferred from the `_ThreadForbidCount` asserts and the
   call chains; it was not observed at runtime.

## Address index

| address | role | conf. |
|---|---|---|
| `0x1401E31B0` / `0x1401E34F0` | `colors.txt` / `cosmetic.txt` loaders | high |
| `0x14279F558` / `0x142796208` | `CCountryColors` / `CColor` vtables (RTTI checked) | high |
| `0x14074B4B0` | RGB→HSV, modify, →RGB (defines in 6.4) | high |
| `0x140752D00` | `CCountryColors` token reader (define loads `0x140752D37..D9E`) | high |
| `0x14074DE70` / `0x1407612D0` | `CCountry` reader / writer | high / medium-high |
| `0x1407117A0` | `CCountry` ctor (colour init) | high |
| `0x14072A920` | base→current reset | medium |
| `0x140734AE0` | `GetMapColors` (only reader) | high |
| `0x140759340` / `0x140759370` | `SetColors` / `SetCosmeticTag` | high |
| `0x1401F13A0` | apply cosmetic or base colours | high |
| `0x1402A4A10` | `tag_color` console handler | high |
| `0x140AB7E30` / `0x1433E7818` | map singleton getter / global | high |
| `0x140AB7F10` | refresh one country (mode 0 dirty + faction colour) | high |
| `0x140AB8040` | rebuild all modes (UI thread only) | high |
| `0x140E86C10` / `0x140FBFC30` | mark mode 0 / its entry dirty | high |
| `0x140FBF5A0` | per-frame map-mode update (consumes `modes+0x48+i`) | high |
| `0x140FB7F80` | rebuild one mode (23-way table) | high |
| `0x140FB8250` / `0x140FBAE80` | mode 0 / mode 19 builders | medium-high |
| `0x140DF1F40` | faction creation: faction colour from leader | high |
| `0x140DFA770` | faction writer (saves colour `0x56`, flag `0x4205`) | high |
| `0x140D8B040` | subject relation change: compare and refresh | medium-high |
| `0x1433DDB08` / `0x140188D90` | current idler / `CInGameIdler` slot `+0x2D8` (true) | high |
| `0x1433DD08C` | `-quickstart` switch (skips map rebuilds) | medium |
| `0x1433E5130..0x1433E542C` | the six `NGraphics` colour defines | high |
