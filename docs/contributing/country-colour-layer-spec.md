# Country colour layer: implementation spec (CAPIE 1.6, API level 8)

Status: ready to implement. Nothing here is built yet.

- Evidence: `../research/country-colour-2026-10-28.md` (cited below as "research §n"). All RVAs are
  for `hoi4.exe` 1.19.4.0 (SHA-256 `B6650C85…D888`).
- Write ADR-0038 ("Country colour layer") when the work starts. It follows ADR-0023
  (display-only layers may switch off alone) and ADR-0032 (flag layer, cosmetic-tag reset).

## 1. What it does

A player picks a colour for a country. That country's provinces, borders, faction colour (if it
leads a faction without a custom colour) and UI colour swatches show the picked colour instead of
the `colors.txt` or `cosmetic.txt` colour. The flag, the name and the cosmetic tag are not
touched.

The layer is a pure function of synchronised country variables, set by script effects. CAPIE
never writes `CCountry` memory. It writes two kinds of things:

- a CAPIE-owned colour block, which the hooked getter returns;
- the same "repaint" flags the game sets after a cosmetic-tag change (it calls the game's own
  refresh function).

Priority on screen:

1. the colour layer, when it is set, valid, and its cosmetic-tag snapshot still matches (§4.3);
2. otherwise the game's colour (cosmetic tag colour, else base colour).

Subjects follow the game rule: a puppet drawn in its overlord's colour shows the overlord's
layer.

## 2. Hook

### 2.1 Getter detour

| Item | Value |
|---|---|
| Target | `GetMapColors`, RVA `0x734AE0` (`core::rva::country_map_colors`) |
| Signature | `using GetMapColorsFn = const void* (*)(const void* country);` (MSVC x64: `rcx` in, `rax` out) |
| Callers | 16 functions on the logic thread and the UI thread (research §6.2) |
| Frequency | Map-mode rebuilds call it about 23 × countries times. The map renderer calls it per update, and the UI per draw. Treat it as hot. |
| Install | MinHook inline. The first 6 bytes are `push rbx; sub rsp,20h`; the loop target `0x734AF0` lies outside the patch. |

```cpp
const void* colour_layer::map_colors(const void* country) noexcept {
    const void* game = originals_.map_colors(country);          // original walk to the overlord
    if (!enabled_.load(std::memory_order_relaxed) || live_.load(std::memory_order_acquire) == 0) {
        return game;                                             // fast path: no layer in this game
    }
    const uptr resolved = reinterpret_cast<uptr>(game) - off::country_colors;   // 0x350
    // resolved was just dereferenced by the original (+0xF88), so plain reads are safe here.
    if (*reinterpret_cast<const uptr*>(resolved) != country_vtable_) return game;  // RTTI-pinned vtable
    const std::int32_t id = *reinterpret_cast<const std::int32_t*>(resolved + off::country_tag); // +0x8
    if (id <= 0 || id >= static_cast<std::int32_t>(max_countries)) return game;
    const Slot& s = slots_[id];
    if (s.country.load(std::memory_order_acquire) != resolved) return game;
    const auto which = s.active.load(std::memory_order_acquire);   // 0 none, 1 = blocks[0], 2 = blocks[1]
    return which == 0 ? game : static_cast<const void*>(&s.blocks[which - 1]);
}
```

Rules:

- The detour is `noexcept`. It makes no game call besides the trampoline, takes no lock and does
  not log on this path.
- It looks up the **resolved** country (after the overlord walk), not the argument (research §6.5).
- The tag id is the country-list index. Flag pin `tag.index_is_int` already proves `tag → index`
  is `mov eax,[rcx]`.

### 2.2 Refresh (game call, logic thread only)

| Item | Value |
|---|---|
| Function | `RefreshCountryColor`, RVA `0xAB7F10`, `using RefreshColorFn = void (*)(void* map, const std::int32_t* tag);` |
| Map | `*(void**)(base + 0x33E7818)` (RIP-pinned through `0xAB7E30`) |
| Tag | `country + 0x8` (the game passes `lea rdx,[rdi+8]` at `0x75951D`) |
| Guard | `map != nullptr`, and `idler = *(void**)(base + 0x33DDB08)` is non-null, and `idler->vtbl[0x2D8 / 8]() == true` (true only for `CInGameIdler`) |
| Bounds | `1 <= id < country count`; the game's `0x140FBFC30` has no bounds check |

The guard check and the call go through a new SEH wrapper, `engine::guarded_refresh_colour(map,
tag)`, gated by `game_calls_allowed()` like the existing ones. The idler slot is compared by
address before it is called: slot `+0x2D8` of `*idler` must equal `base + 0x188D90` or `base +
0x11D530` (the only two implementations). Any other pointer is treated as "not in game", so CAPIE
never makes an unknown virtual call.

Effect: mode 0 (political) and the faction colour are refreshed on the next map update. This is
the game's own path after a cosmetic-tag change.

### 2.3 Mark every map mode dirty (optional, on by default)

After the refresh, and under the same guard, CAPIE sets
`modes = *(u8**)(*(uptr*)(map + 0x8) + 0x30)`, then `modes[0x48 + i] = 1` for `i = 0..22`.

- The game's own UI-thread update `0x140FBF5A0` then rebuilds every mode (research §6.3), so mode 19
  (claims) and any other mode are fresh too.
- These are the same bytes the game writes from the same thread (`0x140E86C10`). Each write goes
  through `__try` and `VirtualQuery` (`FlagMemory::write` style), at most once per changed country.
- Compile-time switch `colour::mark_all_modes` (default `true`). Turn it off only if in-game test
  T6 shows a frame hitch that is not worth it.
- Never call `RebuildAllMapModes` (`0xAB8040`): it rebuilds synchronously on render objects and
  belongs to the UI thread.

## 3. Data structures and threading

```cpp
struct ColourBlock {                 // laid out exactly as CCountryColors (0x60 bytes)
    uptr vtable;                     // base + 0x279F558 (CCountryColors, RTTI-checked)
    uptr pad08;                      // 0
    struct { uptr vtable; uptr pad; float rgba[4]; } color;     // +0x10; vtable base + 0x2796208 (CColor)
    struct { uptr vtable; uptr pad; float rgba[4]; } color_ui;  // +0x30
    std::uint8_t flags;              // +0x50 = 3 (both read)
    std::uint8_t pad51[15];
};
static_assert(sizeof(ColourBlock) == 0x60 && offsetof(ColourBlock, color) == 0x10 &&
              offsetof(ColourBlock, color_ui) == 0x30 && offsetof(ColourBlock, flags) == 0x50);

struct Slot {                        // one per country-list index, never freed during the process
    std::atomic<uptr> country{0};    // CCountry* this slot belongs to (0 = unused)
    std::atomic<std::uint8_t> active{0};   // 0 none, 1 blocks[0], 2 blocks[1]
    ColourBlock blocks[2];           // double buffer
    std::uint32_t rgb = 0;           // last applied 0xRRGGBB (writer only)
    bool shown = false;              // writer only
};
std::unique_ptr<Slot[]> slots_;      // max_countries = 4096 (shared bound with the flag layer), about 400 KB
std::atomic<std::uint32_t> live_{0}; // number of slots with active != 0
```

Threads and memory:

- **Writers:** only on the game's logic thread, inside the `UpdateNames` hook and the
  `set_variable` hook (the same places as the name, flag and portrait layers). A `std::mutex`
  serialises them; it is never held across the game refresh call.
- **Publish:**
  1. write the inactive block;
  2. then `active.store(other, release)`;
  3. adjust `live_`;
  4. to remove, `active.store(0, release)`.

  `country` is stored before the first publish. If the slot already belongs to another pointer
  (the index was reused after a load), the slot is reset first.
- **Readers:** any thread. Reads are lock-free with acquire loads. The returned pointer stays valid
  for the life of the process, because slots are never freed. A reader that holds a block across
  two rapid changes can see a torn colour for one frame on the map. The faction colour copy runs
  on the writer's own thread, so it never sees a torn block.
- **Determinism (multiplayer):** the slot state is a function of the country variables only. It
  is updated synchronously, at the effect step that commits them, on every client. Never defer an
  update to a UI frame.

## 4. Colour value and maths

### 4.1 Script value

Three integers `0..255` (`capie_cc_r`, `capie_cc_g`, `capie_cc_b`). CAPIE packs them into
`0xRRGGBB` internally. Three variables are used because they need no script arithmetic near the
fixed-point limits, the RGB steppers in the UI map one-to-one, and presets set three literals.

### 4.2 Adjustment (same as vanilla parsing)

For each of `color` (defines `COUNTRY_COLOR_HUE/SATURATION/BRIGHTNESS_MODIFIER`) and `color_ui`
(`COUNTRY_UI_COLOR_*`), in `float`:

1. `r,g,b = R/255, G/255, B/255`.
2. Convert to HSV with hue in `[0,1)`: `V = max`, `S = (max-min)/max` (0 when `max = 0`), `H` the
   usual sector formula divided by 6.
3. `H += dH`, `S *= mS`, `V *= mV`. There is no clamp, which matches the game.
4. Convert back with the sector `i = min(floor(6H), 5)` and `f = 6H - i`. When `S == 0`, the result
   is `(V,V,V)`.
5. Alpha is `1.0`.

The six define floats are read from their pinned globals each time a colour is computed, so modded
defines apply. A failed read means the slot is not published (fail closed for that country).
Vanilla values: map S ×0.6, V ×0.8; UI unchanged. The maths is a pure `constexpr` function in
`colour_layer.hpp` and is tested offline.

### 4.3 Decode (`colour_layer.hpp`, pure, fake-memory testable)

Reuse `open_layer()`, `find_variable()`, `whole()` and `flag::read_packed()`.

| Status | Condition |
|---|---|
| `absent` | `capie_cc_ver` missing |
| `invalid` | `capie_cc_ver != 1`; a channel missing, fractional or outside `0..255`; a malformed snapshot (as in CONTRACT 17.3) |
| `valid` | otherwise |
| `hidden` | valid, but `capie_cc_cos_len` is present and the current cosmetic tag (`+0x1488`) differs from the snapshot |

Only `valid` publishes. Every other status removes the slot's block (vanilla colour).

## 5. Script contract (CONTRACT.md section 19, API level 8)

Add record `capie_colour` (section 5), loc key `CAPIE_COLOUR`, and capability bit
`capability_colour_bit = 0x4` in the v4 family (`capability.hpp`).

### 19.2 Variables (on the country whose colour changes)

| Variable | Value |
|---|---|
| `capie_cc_ver` | `1` while the layer is set (format 1); absent means no layer |
| `capie_cc_r`, `capie_cc_g`, `capie_cc_b` | the picked colour, integers `0..255`, as you would write it in `colors.txt` `rgb { }`. CAPIE applies the `NGraphics` country colour defines to it, as the game does. |
| `capie_cc_cos_len`, `capie_cc_cos` | cosmetic-tag snapshot, exactly as in 17.2 and 17.3. Absent means no check. |
| `capie_cc_rev` | set by every change, as the **last** statement; the value is not used |

These are ordinary country variables: written only by effects, saved, and synchronised.

### 19.5 Writing and removing

```text
# Scope: the country. Colour 30 90 160, no cosmetic tag at the time.
set_variable = { capie_cc_r = 30 }
set_variable = { capie_cc_g = 90 }
set_variable = { capie_cc_b = 160 }
clear_array = capie_cc_cos
set_variable = { capie_cc_cos_len = 0 }
set_variable = { capie_cc_ver = 1 }
set_variable = { capie_cc_rev = 1 }        # last: CAPIE repaints now

# Remove it (the game's colour comes back):
clear_variable = capie_cc_r
clear_variable = capie_cc_g
clear_variable = capie_cc_b
clear_array = capie_cc_cos
clear_variable = capie_cc_cos_len
clear_variable = capie_cc_ver
set_variable = { capie_cc_rev = 1 }
```

### 19.6 What CAPIE does

- **On `capie_cc_rev`** (in the `set_variable` hook, after the original): re-evaluate every country
  in `gamestate+0x310`.
- **On `UpdateNames`** (after the original): re-evaluate that country. This covers load, cosmetic
  tag changes, subject changes and new countries.

For every country whose shown colour changed:

1. publish or remove its block;
2. refresh it (§2.2);
3. mark the map modes dirty (§2.3).

### 19.7 Activation, multiplayer, without CAPIE

- **Pins fail:** the layer is off, the getter is not hooked, colours stay vanilla, and the other
  layers keep working. `capie_colour = 0`.
- **Multiplayer:** the argument in research §6.2. The faction colour inherits the leader's layer
  and is saved, so it is identical on all clients. CONTRACT 12 (same build everywhere) is required.
- **A client without CAPIE:** sees vanilla colours. A save made with the layer keeps the overridden
  faction colour (cosmetic only).
- **Console:** none in v1.

### 19.4 Lifecycle rules (for mods)

| Case | Behaviour |
|---|---|
| Cosmetic tag change | Display: the layer is hidden at once (snapshot mismatch). State: the mod's `on_daily` clears it, as for the flag (17.4). |
| Civil war or new dynamic tag | If the game copies variables to the breakaway, the mod clears the layer there (in-game test T9 decides). |
| Annexation | The layer stays on the dead country and returns on release. Clearing it is the mod's choice. |
| Revert | Remove block (19.5); the cosmetic or base colour returns. |
| Save and load | Variables are saved; CAPIE rebuilds in `UpdateNames`. Nothing else is stored. |

## 6. Rename Anything side

Mirror the flag picker. All files are generated through `tools/gen.py`.

| Piece | Path (new) |
|---|---|
| Generator | `tools/modgen/features/colour_picker.py`, `tools/modgen/features/colours.py` (preset catalog) |
| GUI | `mod/interface/rn_colour_picker.gui` / `.gfx`, `mod/common/scripted_guis/rn_colour_picker.txt` |
| Effects / triggers | `scripted_effects/rn_colour_picker.txt`, `scripted_effects/rn_country_colour_carrier.txt` (the only writer of `capie_cc_*`), `scripted_triggers/rn_colour_picker.txt` |
| On actions | `on_actions/rn_colour_on_actions.txt` (daily cosmetic check, civil-war clear if T9 needs it) |
| Rules | `rn_feature_colour` in `game_rules/rn_feature_rules.txt` |

**Window.** One button in the politics tab next to the flag button (own country,
`player_context`). One in the diplomacy view for subjects (`selected_country_context`).

**Presets.**

- A generated grid of about 48 swatches: a hand palette of 24 plus 24 taken from vanilla
  `colors.txt` (major tags).
- Because CAPIE applies the same defines, a `colors.txt` preset looks exactly like that country.
- Swatches are one frame strip (`noOfFrames = N`, generated `.dds`).

**RGB.**

- Three channels, each with steppers `-16 -1 +1 +16` and the value shown as a number.
- The preview tile is a 512-frame strip (3 bits per channel, 8×8 px frames, 4096×8 px).
- The frame is `(r/32)*64 + (g/32)*8 + (b/32) + 1`, computed with `modulo_variable` and division.
- There is no exact live preview, because the real colour shows only after Apply.

**Apply, `rn_cp_apply`.** It checks in this order, as `rn_fp_apply` does:

1. epoch header;
2. `capie_cap >= 8`, else a new error "CAPIE too old";
3. `capie_colour = 1`, else a new error "colour layer off";
4. `rn_cp_switch` (feature rule);
5. `rn_cp_rights`;
6. cooldown.

Then `var:rn_cp_target = { rn_country_colour_carrier_set = yes rn_cp_cooldown_start = yes }`. The
carrier writes 19.5, including the cosmetic snapshot of the target.

**Restore.** The same checks without the CAPIE ones. It clears the layer (19.5) and starts the
cooldown.

**Rules.**

- `is_ai = no`. The own country, or `rn_sub_may_edit` under lobby rule `rn_edit_rights`.
- Cooldown `rn_ip_cd_colour_until = global.num_days + days`, using the shared `rn_rename_cooldown`
  length. It starts only on a successful Apply or Restore.
- Two countries may share a colour; this is not checked.

**Tests (RA).**

- `inline_checks/colour_cases.py`: the carrier writes exactly the 19.5 statement order, ending with
  `capie_cc_rev`, and every channel is clamped to `0..255`.
- `check.py` rule: no file other than the carrier writes `capie_cc_*`.
- Preset values are integers `0..255`.

## 7. Pins (`features/country/colour_layer_pins.hpp`)

```cpp
inline constexpr core::Signature signatures[] = {
    {"colour.map_colors", core::SignatureGroup::country_hook, core::rva::country_map_colors /*0x734AE0*/,
     "40534883ec20488bd90f1f8000000000488b83880f0000488b80b80300004885",
     "GetMapColors prologue + subject walk (+F88, +3B8) (research 2, 6.1)"},
    {"colour.refresh", core::SignatureGroup::country_hook, core::rva::refresh_country_color /*0xAB7F10*/,
     "40574883ec20488b4908488bfae8", "RefreshCountryColor prologue: map+8, rdx = tag (research 6.3)"},
};

inline constexpr core::InstructionPin instruction_pins[] = {
    {"getter.block_offset",   0x734B38, "488d8350030000"},               // lea rax,[rbx+350]
    {"colors.layout",         0x759340, "0f1042200f1181700300000f1042400f118190030000"
                                        "0fb642508881a0030000c3"},       // SetColors: +20->+370, +40->+390, +50->+3A0
    {"cosmetic.idler_slot",   0x75950E, "ff90d8020000"},                 // call [rax+2D8]
    {"cosmetic.tag_argument", 0x75951D, "488d5708"},                     // lea rdx,[rdi+8]
    {"refresh.map_manager",   0xAB7F16, "488b4908"},                     // mov rcx,[rcx+8]
    {"refresh.faction_copy",  0xAB7F81, "0f1040200f1183e0090000"},       // colour +20 -> faction +9E0
    {"mark.modes",            0xE86C20, "488b7930"},                     // mov rdi,[rcx+30]
    {"mark.mode0_dirty",      0xE86C48, "c6404801"},                     // mov byte[rax+48],1
    {"update.mode_dirty",     0xFBF7A0, "41807c1e4800"},                 // cmp byte[r14+rbx+48],0
    {"update.mode_count",     0xFBF819, "83ff17"},                       // cmp edi,23
    {"idler.ingame_true",     0x188D90, "b001c3"},                       // CInGameIdler::slot 2D8
    {"idler.default_false",   0x11D530, "32c0c3"},                       // base idler slot 2D8
};

inline constexpr core::RipPin target_pins[] = {
    {"cosmetic.calls_map",     0x759519, 0x75951D, 0xAB7E30},
    {"cosmetic.calls_refresh", 0x759525, 0x759529, core::rva::refresh_country_color},
    {"refresh.calls_getter",   0xAB7F7D, 0xAB7F81, core::rva::country_map_colors},
    {"cosmetic.idler_global",  0x759502, 0x759506, 0x33DDB08},
    {"map.global",             0xAB7E33, 0xAB7E37, 0x33E7818},
    {"define.ui_value",        0x752D3B, 0x752D3F, 0x33E542C},   // COUNTRY_UI_COLOR_BRIGHTNESS_MODIFIER
    {"define.ui_saturation",   0x752D48, 0x752D4C, 0x33E5384},
    {"define.ui_hue",          0x752D50, 0x752D54, 0x33E5308},
    {"define.value",           0x752D8D, 0x752D91, 0x33E5268},   // COUNTRY_COLOR_BRIGHTNESS_MODIFIER
    {"define.saturation",      0x752D9A, 0x752D9E, 0x33E51D4},
    {"define.hue",             0x752DA2, 0x752DA6, 0x33E5130},
};

inline constexpr core::NamedSlotPin slot_pins[] = {
    {"idler.ingame_slot", {0x29E5828 /*CInGameIdler*/, 0x2D8, 0x188D90}},
};
// expected sizes 2 / 12 / 11 / 1, static_assert as in flag_layer_pins.hpp
```

The define order is pinned by the token reader `0x752D30..0x752DA6`. The call `0x752D54` feeds
`color_ui` (`lea rcx,[rbx+0x30]` at `0x752D59`) and `0x752DA6` feeds `color`. Optionally add
`{"define.ui_target", 0x752D59, "488d4b30"}`.

**RTTI** (`core/rtti.hpp`, new table `colour_vtable_classes`):

| id | RVA | name |
|---|---|---|
| `vt_country_colors` | `0x279F558` | `.?AVCCountryColors@@` |
| `vt_color` | `0x2796208` | `.?AVCColor@@` |
| `vt_country` | `0x2864710` | `.?AVCCountry@@` |
| `vt_ingame_idler` | `0x29E5828` | `.?AVCInGameIdler@@` |

**`game_layout.hpp` additions:**

- `rva::`: `country_map_colors`, `refresh_country_color`, `map_singleton`, `current_idler`,
  `define_country_color[3]` and `define_country_ui_color[3]`;
- `off::`: `country_colors = 0x350`, `country_tag = 0x8`, `map_manager = 0x8`, `map_modes = 0x30`,
  `mode_dirty = 0x48`, `idler_in_game_slot = 0x2D8`;
- `value::map_mode_count = 23`.

**Fail-closed:**

- `colour_ok = layer_ok && colour pins ok && colour RTTI ok`, where `layer_ok` means the name-layer
  pins passed (the colour layer needs the `UpdateNames` and `set_variable` hooks).
- The getter hook is in `all_hooks[]` within the colour index range, and is installed only when
  `colour_ok`.
- `colour_layer.activate()` sets `enabled_` only when the trampoline is non-null and the six define
  globals are readable.
- At runtime, a faulted or refused game call turns the layer off (`game_call_failed()`). The getter
  then passes through, giving vanilla colours. No repaint is attempted after a fault.
- The init line gains `colour layer on|off <first failed id>`.

## 8. Integration checklist (CAPIE)

| File | Change |
|---|---|
| `features/country/colour_layer.hpp` | decode, HSV maths, `ColourBlock` (pure) |
| `features/country/colour_layer_hooks.hpp` | slots, `map_colors` detour, `update(country)`, `refresh_all()`, publish, refresh |
| `features/country/colour_layer_pins.hpp` | §7 |
| `engine/game_calls.hpp` | `guarded_refresh_colour`, `guarded_mark_modes` (SEH, fault latch) |
| `core/game_layout.hpp`, `core/rtti.hpp` | §7 |
| `dll/capie_core.cpp` | hook spec; `update_names_hook` calls `colour_layer.update(country)` after the flag and portrait layers; `set_variable_hook` gets a `capie_cc_rev` branch; `max_hooks` +1; init line |
| `features/country/capability.hpp` | `capability_colour_bit`; `capabilities_of(..., colour)` |
| `features/editor/public_schema.hpp`, `transport.hpp` | record `capie_colour` |
| `CONTRACT.md` | section 19, level 8, records table, section 1 table, section 13 rows |
| `build.ps1` | new tests in `Invoke-Test` and `$tidyFiles` |

## 9. Tests

### 9.1 Offline (`tests/colour_layer_test.cpp`, built with diagnostics 0 and 1)

1. **Decode:**
   - valid;
   - absent;
   - `ver = 2`;
   - a channel missing, `-1`, `256`, or `1.5`;
   - snapshot absent, `0`, matching, mismatching, or malformed;
   - variable container unreadable.
2. **Maths:**
   - reference vectors computed by an independent double-precision implementation (tolerance
     `1e-6`): black, white, pure R/G/B, greys (S = 0), and `GER` `rgb {57 66 52}` and three other
     `colors.txt` entries with the vanilla defines;
   - `dH = 0.5` with wrap past 1;
   - `mS = 0`;
   - `mV = 1.2` (no clamp);
   - UI defines (1, 1) give the identity.
3. **Block:** vtables equal `base + RVA`; floats at `+0x20` and `+0x40`; flags `3`; `sizeof` and
   offsets (static and runtime).
4. **Slots:**
   - id bounds (0, 1, 4095, 4096);
   - wrong pointer;
   - wrong vtable;
   - double-buffer flip order: a reader thread hammers while a writer flips, and must never see a
     block with flags ≠ 3;
   - the `live_` fast path;
   - reuse of an index by another pointer resets the slot.
5. **Detour:**
   - passes through when disabled, when `live_ = 0`, or when there is no slot;
   - returns the block for the resolved country, not the argument (fake overlord walk).
6. **Refresh:**
   - guard false (map null, idler null, idler slot is neither known address, slot returns false):
     no call;
   - guard true: one call per **changed** country, none for unchanged ones;
   - 23 dirty bytes set when `mark_all_modes` is on;
   - out-of-range id: no call;
   - a fault turns the layer off.
7. **Hooks:**
   - `set_variable` reacts only to `capie_cc_rev`;
   - `UpdateNames` re-evaluates only its country;
   - a cosmetic change between two `UpdateNames` calls hides the layer.
8. **Capability:** every combination of flag, portrait and colour bits.

### 9.2 EXE pin test (`tests/colour_layer_pins_exe_test.cpp`)

- Map `hoi4.exe` and run `colour::pins::first_failed_pin` and the RTTI table; both must pass.
- Then flip one byte each in a signature, an instruction pin, a target displacement and a slot,
  and expect that pin's id.
- Without an exe argument: every entry decodes and the sizes match.

### 9.3 Mutation targets (`tests/mutants.json`, `colour-layer-*`)

| Mutant | Must be killed by |
|---|---|
| drop the `live_ == 0` fast path return | detour test, counts game reads |
| drop the vtable check | wrong-vtable test |
| return the argument's slot instead of the resolved one | overlord test |
| `id >= max_countries` → `>` | bounds test |
| publish before writing the block | flip-order test |
| swap S and V multipliers | maths vectors |
| use the map defines for `color_ui` | UI identity test |
| flags `3` → `1` | block test |
| skip the guard | guard tests |
| `cmp` idler slot address removed | unknown-slot test |
| mark loop `22` instead of `23` | dirty-byte count |
| refresh unchanged countries | refresh count |
| cosmetic gate inverted | snapshot tests |
| commit name `capie_cc_rev` → `capie_cf_rev` | hook test |
| channel range `<= 255` → `< 255` | decode 255 |

### 9.4 In-game (CONTRACT section 13 rows; results in `in-game-test-record.md`)

| # | Steps | Expect |
|---|---|---|
| T1 | New game as POL. Apply a preset in the picker. | The political map repaints within about a second. Borders and country fill match. No log errors. |
| T2 | Pick the `colors.txt` RGB of `GER` for POL. | POL looks identical to GER on the map (adjustment correct). |
| T3 | Open the faction view and the faction colour picker while POL leads a faction without a custom colour. | Both show the new colour. The faction colour follows. |
| T4 | Puppet a country with the "subjects in overlord colour" rule on. | The puppet shows POL's override, and keeps its own override only when the rule is off. |
| T5 | `set_cosmetic_tag` via console, then wait one day. | The game's cosmetic colour shows at once. The mod clears the variables the next day. |
| T6 | Apply, then switch to the claims mode and two other modes. | Fresh colours in every mode. Measure the frame time at Apply. Set `mark_all_modes` accordingly. |
| T7 | Restore. | Vanilla colour returns, and the faction colour too. |
| T8 | Save, then load, with CAPIE; then load the same save without CAPIE. | With CAPIE: the colour persists. Without: vanilla country colour, and the faction colour keeps the override (expected leak). |
| T9 | Apply on a country, then start a civil war in it (console/effect). | Record whether the breakaway has `capie_cc_ver`. Implement the RA clear if so. |
| T10 | Multiplayer, 2 clients (multiplayer-test-plan): A applies; B hot-joins; create a faction with A as leader; play 1 year at speed 5. | Same colours on both. No out-of-sync. The hot-joiner sees the colour. |
| T11 | Replace one pinned byte in a copy of the exe (or use the wrong build). | Init line says the colour layer is off with the pin id. Colours are vanilla. The other layers work. |
| T12 | Use `tag_color POL 255 0 0` while a layer is set. | The layer still wins (the getter override). After Restore, `tag_color`'s colour shows (game state). |

## 10. Risks

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| Faction colour (game state, saved) differs between clients when one client's layer is off (runtime fault on one machine only) | low | possible out-of-sync if the faction colour is checksummed; wrong colour | the getter path makes no game call; refresh faults are rare and SEH-guarded; test T10; document |
| The getter is hot; a slow detour costs frame time | medium | map rebuild slower | lock-free fast path; `live_ == 0` exit; no `VirtualQuery` in the detour |
| Other modes stay stale if `mark_all_modes` is off | medium | claims map mode shows the old colour | default on; T6 |
| Rebuilding all 23 modes causes a hitch on Apply | low-medium | one-frame hitch | rare user action; T6 measures it; can fall back to mode 0 only |
| Mode index meaning changes in a later patch | medium (port) | wrong mode refreshed | `update.mode_count` and dirty-byte pins; the port tool re-checks |
| Map built before `UpdateNames` at load | low | vanilla colours until the next change | T8; fallback: flush a "pending repaint" at the next `set_variable` where the guard holds |
| Index reuse or stale pointer after loading another save | low | wrong colour on one country | slot keyed by index and validated by pointer; `UpdateNames` re-evaluates every country on load |
| Civil war copies variables | unknown | breakaway shares the colour | T9, then an RA on_action clear |
| Save leak: faction colour keeps the override without CAPIE | certain (by design) | cosmetic only | documented in 19.7 |
| Unknown idler implementations of slot `+0x2D8` | low | an unknown virtual call | compare the slot address against the two known RVAs, otherwise treat as not in game |
| Modded defines are unreadable or extreme | low | odd colours | the pinned globals are read live; no clamp, as in the game |
| Two-step script commit read half-written | none | — | CAPIE reads only on `capie_cc_rev`, which is the last statement |
