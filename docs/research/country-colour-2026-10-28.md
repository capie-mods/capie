# Country map colour: storage, redraw and runtime changes (HOI4 1.19.4.0)

Status: research only, read-only static analysis in Ghidra 12.1.4 (via REA). Build: `hoi4.exe`
SHA-256 `B6650C85…D888`, image base `0x140000000`. Addresses below are absolute; RVA = address
minus `0x140000000` (so `0x140759370` is RVA `0x759370`, the same as CAPIE's
`country_set_cosmetic_rva`).

Follows up `ghidra-review-2026-10-28.md` (B3) and section 6 of
`capie-wt-anchors/docs/research/subideology-rename-design.md`.

## Summary

- Each `CCountry` embeds **two** copies of a `CCountryColors` object (RTTI name):
  the **current** colours at `+0x350` and the **base** colours (from `colors.txt`) at `+0x3B0`.
  Each holds a `color` and a `color_ui`. Each is a `CColor` holding four `float`s (RGBA, 0..1).
- The map does not read `+0x350` directly. Every reader goes through a small getter
  `0x140734AE0` (16 callers). For some subjects the getter returns the overlord's colours, so the
  game has a "dynamic colour" path, but it is computed at read time and never written.
- The game changes colours at runtime in two places: the cosmetic-tag setter `0x140759370`, and the
  debug console command `tag_color TAG R G B` (`0x1402A4A10`). Both write `CCountry+0x350`, then
  ask the map to refresh.
- The refresh is a dirty-flag scheme. `0x140AB7F10(map, &tag)` refreshes one country (it marks its
  map-mode entry dirty and copies the colour into its faction's cached colour).
  `0x140AB8040(map, 0)` rebuilds every map mode.
- The save writer saves the cosmetic tag but not the colours. On load the colours are rebuilt from
  `colors.txt` plus the cosmetic tag.
- A CAPIE colour layer is **feasible**. The best design is a display-only detour on the getter
  `0x140734AE0`, plus a call to `0x140AB7F10` after each change.

## 1. Storage

### `CCountryColors` layout (size 0x60) — confidence high

The cosmetic loader `0x1401E34F0` (it opens `"common/countries/cosmetic.txt"`) allocates and builds
one object per cosmetic tag:

```c
plVar6 = alloc(0x60);
*plVar6 = CCountryColors::vftable;          // 0x14279F558
FUN_1422b47b0(plVar6 + 2);                  // CColor ctor at +0x10
FUN_1422b47b0(plVar6 + 6);                  // CColor ctor at +0x30
*(u8*)(plVar6 + 10) = 0;                    // flags at +0x50
(*vtbl[0x18])(plVar6, reader);              // generic persistent reader 0x142540C70
if (!FUN_140748090(plVar6)) log("... has missing colors. Ignoring it.");
else map_insert(gamestate + 0x8E8, name)->value(+0x30) = plVar6;
```

`CColor` constructors:

```
0x1422B4780 CColor(r,g,b,a): lea rax,[0x142796208]; mov [rcx],rax;
            movss [rcx+0x10],xmm1; [rcx+0x14],xmm2; [rcx+0x18],xmm3; [rcx+0x1c],a
0x1422B47B0 CColor():        same vtable, all four floats = 1.0f (0x3f800000)
```

So the colour layout is as follows:

| offset in `CCountryColors` | content |
|---|---|
| `+0x00` | vtable `0x14279F558` |
| `+0x10` | `CColor color` (vtable `0x142796208`; floats R,G,B,A at `+0x20..+0x2F`) |
| `+0x30` | `CColor color_ui` (floats at `+0x40..+0x4F`) |
| `+0x50` | `u8` flags: bit0 = `color` read, bit1 = `color_ui` read |

The values are linear `float` RGBA in 0..1. They are not bytes or HSV. HSV is used only as a step
while parsing (see below).

### Token reader (`CCountryColors` vtable slot `+0x20` = `0x140752D00`) — confidence high

```
cmp r8d,0x56   ; jz  -> "color":    read colour (0x1425438B0), adjust with
                 FUN_14074b4b0(col, [0x1433E5130],[0x1433E51D4],[0x1433E5268]),
                 CColor::assign(this+0x10), or byte[this+0x50],1
cmp r8d,0x31b8 ; jnz -> "color_ui": same with [0x1433E5308],[0x1433E5384],[0x1433E542C],
                 assign(this+0x30), or byte[this+0x50],2
```

`0x14074B4B0` converts RGB to HSV (`0x1425757E0`). It then does `H += a`, `S *= b`, `V *= c` and
converts back to RGB (`0x1425756A0`). The three floats are globals, almost certainly the
NDefines country colour HSV modifiers. Token ids are **0x56 = `color`** and **0x31B8 = `color_ui`**
(confidence high: two branches, two target fields, two flag bits).

### Inside `CCountry` — confidence high

`CCountry` (the objects in `gamestate[+0x310][index]`, vtable `0x142864710`) embeds:

- `+0x350` **current** `CCountryColors` (colour floats `+0x370`, color_ui floats `+0x390`, flags `+0x3A0`);
- `+0x3B0` **base** `CCountryColors` (colour floats `+0x3D0`, color_ui floats `+0x3F0`, flags `+0x400`);
- `+0x1488` cosmetic tag `std::string` (already `layout::country_cosmetic_tag` in CAPIE).

Evidence:

```
; 0x140759340  CCountry::SetColors(const CCountryColors&)   (only caller: tag_color)
movups xmm0,[rdx+0x20] ; movups [rcx+0x370],xmm0
movups xmm0,[rdx+0x40] ; movups [rcx+0x390],xmm0
movzx  eax,byte[rdx+0x50] ; mov [rcx+0x3a0],al
```

```c
// 0x14074DE70  CCountry token reader (vtable 0x142864710 slot +0x20), token 0x56:
(*(this+0x350)->vtbl[0x20])(...);                // parse into CURRENT
*(u128*)(this+0x3d0) = *(u128*)(this+0x370);     // copy to BASE
*(u128*)(this+0x3f0) = *(u128*)(this+0x390);
*(u8*)(this+0x400)   = *(u8*)(this+0x3a0);
```

`colors.txt` (loader `0x1401E31B0`) feeds every `TAG = { color = … color_ui = … }` into the
`CCountry` reader (`(*country->vtbl[0x18])(country, reader)`). This is the path above, so after load
the current colours equal the base colours.

## 2. Who reads it, caches and refresh

### The getter — confidence high

```c
// 0x140734AE0  const CCountryColors* GetMapColors(CCountry* c)
while ((rel = *(*(c+0xF88)+0x3B8)) && (lvl = *(rel+0x28))) {   // subject relation, autonomy level
    if (!*(u8*)(lvl+0x2C) && *(int*)(FUN_140210e60()+0x2D0) != 1) break; // game-rule check
    c = FUN_140c07b80(*(c+0xF88)+0x188);                         // -> overlord
}
return c + 0x350;
```

There are 16 callers: `0x1402CE300`, `0x140AB7F10`, `0x140B9D1D0`, `0x140B9D320`, `0x140D8B040`,
`0x140DF1F40`, `0x140E38240`, `0x140E83660`, `0x140E86C60`, `0x140E8AC10`, `0x140FB4780`,
`0x140FB8250`, `0x14150FA50`, `0x1419630F0`, `0x141EAEDD0`, `0x141EFBB40`. Puppets drawn in their
overlord's colour come from this loop. The game computes that colour at read time and never writes it.

### Map-mode builder — confidence medium-high

`0x140FB8250` is case 0 of the map-mode rebuild loop `0x140FBEE80`, which iterates 23 map modes held
at `mapmodes+0x18`. For every country `gamestate[+0x310][i+1]` it builds one per-country map object.
That object holds the list of the country's provinces, a highlight flag `+0x57`, a dirty flag `+0x48`
and a label callback `0x140FB7030`. It then sets the object's colour:

```c
default:  FUN_1416389c0(obj, FUN_140734ae0(country) + 0x10);       // CColor 'color'
case 3:   colour of GetMapColors(country) or of a linked object +0x470, then FUN_1422b50b0(col, define)
case 1:   faction colour  *(*(country+0xF88)+0x2D8) + 0x9E0
case 2/4: GetMapColors(country) colour, tinted when not the selected country
```

So the map uses `color` (`+0x10`), not `color_ui`. `color_ui` (`+0x30`) is presumably used by the UI;
those readers are not traced.

The colour is **cached** in these per-country map objects, which are rebuilt from the getter. A
second cache is the **faction colour** at `faction+0x9E0`, which copies the faction leader's colour.

### Refresh entry points — confidence high (code), medium (frame-level effect)

```c
// 0x140AB7F10  RefreshCountryColor(map = *(0x1433E7818), const int* tag)
FUN_140e86c10(*(map+8), tag);          // -> 0x140FBFC30(mapmodes, 0, idx): entry[0][idx-1]->+0x48 = 1
                                       //    and mapmodes->+0x48 = 1   (dirty flags)
if (faction = FUN_14121a350(FUN_140c077f0(tag))) && !faction+0x9C0 && leader == tag)
    *(u128*)(faction+0x9E0) = *(u128*)(GetMapColors(country)+0x20);
```

```c
// 0x140AB8040  RebuildAllMapModes(map, bool)
if (!DAT_1433dd08c) {                               // probably "no graphics / headless"
    FUN_140fbee80(*(map+8)+0x30, arg);              // rebuild all 23 map modes (above)
    FUN_140fbf480(*(map+8)+0x30, arg);
    FUN_140e85da0(*(map+8), *(int*)*(map+8), 1);    // re-apply current map mode (forced)
}
```

`0x140AB7E30` is `mov rax,[0x1433E7818]; ret` (the map singleton getter). The refresh is driven by
dirty flags. A per-frame read of `CCountry` was not seen. Open point: I did not trace which per-frame
code consumes the `+0x48` flags. The game itself relies on `0x140AB7F10` alone after a cosmetic tag
change, so it must be enough.

Every game caller guards the refresh with `DAT_1433DDB08 && (*DAT_1433DDB08->vtbl[0x2D8])()`, which
probably means "the graphical client exists". A CAPIE call should copy this guard.

## 3. Runtime colour changes by the game itself

### a) Cosmetic tag — confidence high (the safest model)

```c
// 0x140759370  CCountry::SetCosmeticTag(const std::string&)   (CAPIE: country_set_cosmetic_rva)
if (same string) return;
assign(this+0x1488, tag);
FUN_14075e660(this);                                   // names update
if (FUN_1401f13a0(gamestate, this+0x1488, this+0x350, this+0x3B0)   // returns "changed"
    && DAT_1433ddb08 && DAT_1433ddb08->vtbl[0x2d8]())
    RefreshCountryColor(map, this+8);                  // 0x140AB7F10
```

```c
// 0x1401F13A0  ApplyCosmeticColors(gs, tag, cur, base)
e = hashmap_find(gs+0x8E8, tag /*FNV-1a*/);
if (!e) { if (cur == base) return 0; copy base -> cur (+0x20,+0x40,+0x50); }
else    { if (e->colors == cur) return 0; copy e->colors -> cur; }
return 1;
```

The callers of `0x140759370` are `0x14026A6E0`, `0x140299AC0` (the `set_cosmetic_tag` console action,
already pinned by CAPIE), `0x14036E2C0` and `0x140379CC0`. The last two are presumably the
`set_cosmetic_tag` / `drop_cosmetic_tag` effects (not verified one by one). The same pair of calls,
`1401F13A0` then `140AB7F10`, also runs in the `CCountry` token reader for token `0x372F` (the
cosmetic tag field), so a loaded save rebuilds its colours.

### b) Console command `tag_color` — confidence high

The registration is in the static initialiser `0x1400036F0` at `0x14000E7A5..E7F6`: name
`"tag_color"` (`0x1427A91D0`), help text `"Test setting a country's color"` (`0x1427A91E0`),
handler `0x1402A4A10`:

```c
if (argc != 4) return "4 more arguments needed: country tag, red, green, blue";
CColor c(r/255.f, g/255.f, b/255.f, 1.f);         // NOTE: no HSV define adjustment
CCountryColors cc{ color = c, color_ui = c, flags = 0 };
CCountry::SetColors(country_of(tag), cc);          // 0x140759340 -> writes +0x370/+0x390/+0x3A0
RebuildAllMapModes(map, 0);                        // 0x140AB8040
"Color applied to <TAG>"
```

This is a debug and test command. It writes the current colours directly, sends no network command,
and does not touch the base colours. A later `SetCosmeticTag` can overwrite it, and it is lost on
save and reload (see 5).

### c) Subject and overlord colour — confidence medium

There is no write here. The getter's overlord loop does the work. `0x140D8B040` (a command that
handles a two-tag relation change, the fields at `+0x10`/`+0x14`) snapshots the subject's
`GetMapColors()` before the change. After the change it calls `RefreshCountryColor` if
`FUN_1407150F0(old, new)` reports a difference (`0x1407150F0` = `!equal`, `0x1407150E0` =
`0x1407316A0` = `equal`). This is a second model of "refresh on colour change".

No scripted effect that sets an arbitrary RGB was found. The `set_country_color` string does not
exist. The string `"country_color"` (`0x142AC8DC8`) is used only at `0x141CACEF7`, which I did not
trace.

## 4. Cosmetic tag colours

`cosmetic.txt` entries become `CCountryColors` objects in a hash map at `gamestate+0x8E8` (sentinel
`+0x8F0`). On `SetCosmeticTag`, `0x1401F13A0` copies the cosmetic entry, or the base colours when
there is no entry, into `CCountry+0x350`, then refreshes. So a cosmetic tag changes the colour by
**overwriting the current colours**. As a result:

- a CAPIE write to `+0x350` (option A below) is undone by any later cosmetic tag change;
- a CAPIE override in the getter (option B) wins regardless of cosmetic tag changes.

## 5. Save file, multiplayer and feasibility

**Save** — confidence medium-high. The `CCountry` writer, vtable slot `+0x10` = `0x1407612D0`, writes
token `0x372F` with `+0x1488` (cosmetic tag). It has no reference to `+0x350`/`+0x3B0` and no write of
token `0x56`/`0x31B8`. So colours are not saved. They come from `colors.txt` plus the cosmetic tag on
every load. A CAPIE colour must be persisted by CAPIE itself (as the other layers are) and re-applied
after load.

**Multiplayer.** `+0x350` lives in `CCountry`, which is gamestate memory. However:

- the game writes it only in reaction to synchronised events (the cosmetic tag) and from a local
  debug command;
- it is not saved, and the save is what a hot-join receives;
- every reader found is display code (map modes, faction colour cache, UI).

A desync from a different colour is therefore unlikely. This is **not proven**: an out-of-sync
checksum or AI code that reads `GetMapColors` was not ruled out (the 16 callers were not all
classified). Confidence: medium.

### Options

**Option B (recommended): display-only getter detour.**
- Detour `0x140734AE0` (RVA `0x734AE0`). It is short and its prologue walks `*(rcx+0xF88)`; sign it.
  If CAPIE holds an override for the resolved country (after the overlord walk, or for the original
  tag, as the design prefers), return a pointer to a CAPIE-owned 0x60-byte block laid out as a
  `CCountryColors`: vtable `0x14279F558` at `+0x00`, `CColor` vtable `0x142796208` at
  `+0x10`/`+0x30`, floats at `+0x20`/`+0x40`, flags `3` at `+0x50`. Otherwise return the original.
- After a change, call `0x140AB7F10(*(void**)0x1433E7818, &tag_id)` under the same guard as the game
  (`DAT_1433DDB08` and its vtable `+0x2D8`). If a targeted refresh proves too narrow in testing, fall
  back to `0x140AB8040(map, 0)`, which `tag_color` uses.
- Gamestate is never written, so the change cannot desync or leak into saves. Cosmetic tag changes do
  not undo it. Puppets that inherit the colour do so naturally if the override is keyed on the
  resolved country.
- To match `colors.txt` colours, run the RGB through `0x14074B4B0` with the same three define floats
  (`0x1433E5130/51D4/5268` for `color`, `0x1433E5308/5384/542C` for `color_ui`), or document that
  CAPIE colours are taken as-is (as `tag_color` does).

**Option A (fallback): copy `tag_color`.** Write `CCountry+0x370`/`+0x390` locally (or call
`0x140759340`), then refresh. It is simpler, but the cosmetic setter overwrites it (so CAPIE must
re-apply it after hooking `0x140759370`, which CAPIE already pins). It is also a local gamestate
write, with the multiplayer caveat above.

## Open points

1. Readers that use `CCountry+0x350..0x3A0` directly instead of the getter (for example the UI's
   `color_ui`) are not enumerated. Option B would miss them. Next step: search `.text` for
   displacements `0x370`/`0x390` with a `CCountry` base.
2. Which per-frame code consumes the `+0x48` dirty flags (that is, that `0x140AB7F10` alone
   repaints the province colours). Next step: runtime test with `tag_color` versus a getter override
   plus `0x140AB7F10`.
3. Out-of-sync checksum and AI use of `GetMapColors`: classify the 16 callers (in particular
   `0x1402CE300`, an 11 KB function that also references `"color"`, and `0x14150FA50`,
   `0x1419630F0`).
4. The exact meaning of the overlord-colour game rule (`FUN_140210E60()+0x2D0`) and of
   autonomy `+0x2C`.
5. The three define floats: confirm their NDefines names (likely `COUNTRY_COLOR_*` HSV modifiers).
6. `"country_color"` at `0x141CACEF7` (probably a GUI or scripted-loc key): not traced.

## Address index

| address | role | conf. |
|---|---|---|
| `0x1401E31B0` | `colors.txt` loader (per-tag `CCountry` reader) | high |
| `0x1401E34F0` | `cosmetic.txt` loader -> `gamestate+0x8E8` map of `CCountryColors*` | high |
| `0x14279F558` | `CCountryColors` vtable (slot `+0x20` = `0x140752D00` token reader) | high |
| `0x142796208` | `CColor` vtable; ctors `0x1422B4780` (rgba), `0x1422B47B0` (white) | high |
| `0x14074B4B0` | RGB->HSV modify->RGB using define floats | high |
| `0x14074DE70` | `CCountry` token reader (`0x56` colours, `0x372F` cosmetic) | high |
| `0x1407612D0` | `CCountry` writer (saves cosmetic tag, not colours) | medium-high |
| `0x140734AE0` | `GetMapColors(country)` -> `country+0x350` (with overlord walk) | high |
| `0x140759340` | `CCountry::SetColors` (writes `+0x370/+0x390/+0x3A0`) | high |
| `0x140759370` | `CCountry::SetCosmeticTag` | high |
| `0x1401F13A0` | apply cosmetic or base colours into current | high |
| `0x1402A4A10` | `tag_color` console handler | high |
| `0x140AB7E30` | map singleton getter (`[0x1433E7818]`) | high |
| `0x140AB7F10` | refresh one country (dirty flag plus faction colour) | high |
| `0x140AB8040` | rebuild all map modes | high |
| `0x140FBEE80` | rebuild loop over 23 map modes | high |
| `0x140FB8250` | per-country map-mode builder (reads `GetMapColors()+0x10`) | medium-high |
| `0x140FBFC30` | mark one country entry of a map mode dirty (`+0x48`) | high |
| `0x140D8B040` | relation-change command: compare colours, then refresh | medium |
