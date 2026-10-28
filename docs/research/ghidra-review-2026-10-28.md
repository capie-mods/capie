# Ghidra review 2026-10-28 (HOI4 1.19.4.0)

Read-only review of how CAPIE uses `hoi4.exe` 1.19.4.0 (SHA-256 `B6650C85…D888`), done in Ghidra 12.1.4
through REA on the fully analysed session. No source file was edited and nothing was committed; this
report is the only file written.

- RVAs are relative to `0x140000000`. Pseudocode excerpts are Ghidra output, shortened; names such as
  `FUN_…` are Ghidra's, class names come from MSVC RTTI.
- Items already settled in `port-1.19.4.0.md` (layout table, Ghidra verification H2/H3/M1, `+0xBF9`)
  and in `../contributing/code-scan-2026-10-28.md` (C1-M18, L1-L32) are not repeated. Where this
  review adds evidence to one of them, it says so.
- Effort: S up to 2 h, M up to a day, L more. Confidence: high (read in the decompiled code), medium
  (inferred from one call site or from structure), low (plausible, needs a runtime check).

---

## 1. Hooked and called functions

### What each function really is

| RVA | CAPIE name | What it does (evidence) | Thread | Frequency | Re-entry | Typedef |
|---|---|---|---|---|---|---|
| `BDB970` | (not hooked) | `CScriptedWindowManager` vtable `29CB368` slot `0x38` (entry `29CB3A0`). Opens profiler scope `"scripted_gui.window"`, then `for w in [mgr+0x20 .. +0x2C]: Tick(w, counter_changed)`, then `BE00C0(mgr)`. When the global `33DDAA5` is 0 it hides every root instead of ticking. | UI | once per frame | no | — |
| `BDB500` | `window_tick` | Per-window visibility state machine: refreshes `+0xBF8` (parent shown), evaluates the template's visible trigger into `+0xBF9`, shows (`root+0x30` slot `0x78`) or hides (slot `0x80`, sets root `+0xA5` bit `0x10`) the root, and calls the updater `BDD5B0(window+0x110)` when visible and something changed. Logs `"window for %s is destroyed."` / `"Parent window for %s is destroyed."` and nulls `+0xF8`/`+0x100` when root/parent `+0xA5 & 4`. | UI (gamestate-access thread, see G6) | **every scripted window, every frame** | no (no callback into scripts that could re-tick) | `void(window*, char)` matches (`FUN_140bdb500(longlong,char)`) |
| `BD8ED0` | `window_bind` | Bind. Called directly from the manager rebuild `BDBAB0` (new windows) and from `BDBA60`, which is `CScriptedWindow` vtable `29CB328` slot `0x10` (rebind: `BDA930` unbind, then Bind). | UI | per window at creation and on each rebind | no | `void(window*)` matches |
| `BDBAB0` | (called "window ctor" in the port notes) | Actually `CScriptedWindowManager` **rebuild**: destroys every window (`(*vtbl[0])(w,1)`), then for each scripted GUI template allocates `0xC10` bytes, constructs the window **inline** (`[+0xE0]=mgr+0x18` idler, `[+0xE8]=template`, `[+0xF0]=mgr`, context ctor `BD7D40(w+0x110)`, `*(u16*)(w+0xBF8)=1`, `+0xBFA=0`) and calls `BD8ED0(w)`. Logs `"Window %s not found.\n"`. | UI | game start, load, GUI reload | — | — |
| `BDD5B0` | (not used) | `CScriptedWindowGUIUpdater` refresh: `_click_enabled` and `_visible` triggers per widget (sets `+0x75` bit `0x10` when hidden), text properties, `image` properties (`"portrait"`, flag), frame properties. Runs only for a visible window that needs a refresh. | UI | on change, not every frame | — | — |
| `37D1C0` | `set_variable_execute` | `CSetVariableEffect` (RTTI `.?AVCSetVariableEffect@@`, vtable `27F2F88`, slot `0x68` = `27F2FF0`, the **only** reference besides `.pdata`). Body: `slot = FindOrCreate(effect+0x58, ctx, 0); value = Eval(effect+0x128); *slot = value`; an invalid variant index calls `1403D2FC0` (throws). | logic | per `set_variable` | can nest (effects inside effects) | `void(effect*, ctx*)` matches |
| `75E660` | `update_names` | 15 direct callers (effects such as `set_cosmetic_tag`, load, government change). Event-driven. | logic | per name-changing event | no | matches |
| `241ECE0` | `loc_lookup_n` | Called by the formatter `1422ADFC0`, which **calls itself** (nested `$KEY$`). | any (UI and workers) | many times per frame | **yes, on the same thread** | — |
| `1622B90` | (backlog) | Politics view "ruling_party_info" writer, single caller `1620890` (= `observer_refresh`). See B1. | UI | per politics refresh | no | — |
| `1438E40` | `portrait_valid` | Called by the game as `char FUN_141438e40(entry*)` (e.g. in `B9B1F0`). | UI | per portrait draw | no | `bool(const void*)` matches |
| `11F770` | `string_copy` | MSVC `std::string` copy-construct; used everywhere as `FUN_14011f770(dst, src)`. | any | — | — | `void*(void*, const void*)` matches |
| `717370` | `country_politics_getter` | `country+0xF90`; called by `1622B90` right before reading `+0xD0`. | — | — | — | — |
| `1230500` | `party_first_leader_role` | Called by `1622B90` on the ruling party, result fed to `leader_name`. | — | — | — | — |

Typedef check: every typedef in `engine/game_calls.hpp` that could be compared with a game call site
matches (FindWidget/SetText/Refresh/FocusSwitch/RequestViewRefresh were already checked in the port
notes; PortraitValid, StringCopy, Tick, Bind and SetVariable are confirmed above).

### Findings

**G1 — `window_manager` (`+0xF0`) is the `CScriptedWindowManager`; the OPEN mark can go (area 3)**
- Evidence: `BDBAB0` inline ctor: `puVar6[0x1c] = lVar4 /*mgr+0x18*/; puVar6[0x1d] = lVar8 /*template*/;
  puVar6[0x1e] = param_1 /*manager*/;`, and `param_1` is the object whose vtable `29CB368` has the
  RTTI name `.?AVCScriptedWindowManager@@` (COL `2D452C8`, type descriptor `32D2858`).
- Impact: `game_layout.hpp:212` says "manager identity is OPEN (used as an equality guard only)". It is
  now known, and `manager_idler = 0x18` is the manager's idler (`+0xE0` is a copy of it).
- Recommendation: update the comment; add an RTTI check of `[window+0xF0]` vtable against
  `CScriptedWindowManager` to `core/rtti.hpp`. Effort S. Confidence high.

**G2 — the "window ctor `BDBAB0`" in the port notes is the manager rebuild (area 3, doc)**
- Evidence: see the table; the function first destroys all windows, then builds them in a loop.
- Impact: misleading for the next port: `window_pins.hpp` pins stores inside a loop body of the rebuild,
  which is fine, but whoever re-finds it will look for a constructor. It also proves that **windows are
  created and destroyed only here** (plus the manager dtor), which G7 relies on.
- Recommendation: rename in `port-1.19.4.0.md` and `window_pins.hpp` comments ("window construction,
  inlined in CScriptedWindowManager::Rebuild `BDBAB0`"). Effort S. Confidence high.

**G3 — root flag bit `0x04` means "destroyed", not "hidden" (area 3)**
- Evidence: `BDB500`: `if ((*(byte*)(root+0xa5) & 4) != 0) … "window for %s is destroyed."; *(w+0xf8)=0`
  (same for the parent at `+0x100`). Tick hides with `root+0xa5 |= 0x10` and shows with `&= 0xef`.
  `BDD5B0` does the same with bit `0x10` at widget `+0x75`.
- Impact: `flag::hidden_a = 0x04` treats a root that is being destroyed as "hidden". For the editor
  guards this is safe (refuse), but focus release (`focus_release.hpp`) may then call `FocusSwitch` on a
  window that the game is about to drop; that is the same window-in-teardown case as scan M3.
- Recommendation: rename `hidden_a` to `destroyed`, and make every guard return `invalid` (not
  "hidden") when it is set, so no game call is made for that window. Effort S. Confidence high.

**G4 — the `+0xBFA` byte forces the next Tick to refresh (area 3)**
- Evidence: `BDA930` (unbind, run before every rebind): `*(char*)(w+0xbfa) = 1; … (*root_vtbl[0xC8])();
  *(w+0xf8) = 0;`; `BDB500`: `if (*(char*)(w+0xbfa)) { param_2 = 1; *(w+0xbfa) = 0; }`.
- Impact: none today. CAPIE resets its lifecycle in the Bind hook; the game itself marks "freshly
  rebound" in this byte. It also shows that rebind **destroys the old root** (`vtbl[0xC8]`), which
  confirms the "never cache a root" rule.
- Recommendation: note it in `game_layout.hpp` (diagnostic only). Effort S. Confidence high.

**G5 — with "Scripted GUI Disabled" no Tick runs, so CAPIE's hide path never fires (area 1, low)**
- Evidence: `26EB00` toggles `33DDAA5` and answers `"Scripted GUI Disabled"`/`"…Enabled"` (console
  command); `BDB970`: `if (DAT_1433ddaa5 == 0) { for w: (*(root+0x30)->vtbl[0x80])(); }` with no Tick.
- Impact: a CAPIE edit box that had keyboard focus keeps it while its window is hidden this way; the
  lifecycle keeps `visible = true` until scripted GUI is enabled again. Developer console only.
- Recommendation: document; optionally treat "no Tick for an own window for N frames" as hidden. Effort
  S. Confidence high.

**G6 — Tick, Bind, the updater and the portrait code run on the gamestate-access thread (area 1)**
- Evidence: the manager's own helpers assert `"_ThreadForbidCount == 0 && \"Current thread is forbidden to
  access gamestate\""` (`BE00C0`, `BDBAB0`), and read `[33DD6B0]+0x310` (game state countries)
  directly. The port notes already found the same assert in `PortraitEntryValid`.
- Impact: confirms the existing assumption ("UI thread after the original Tick"). The localisation
  lookups are the exception: they also run on other threads and recursively (`1422ADFC0` calls itself),
  so the localisation hooks must stay lock-free across the original call (they are).
- Recommendation: none; keep the `GetCurrentThreadId` debug lines. Confidence medium (thread identity
  inferred from the assert, not observed).

**G7 — C++ exceptions: possible, but only fatal kinds; `noexcept` changes how the crash is reported (scan N6)**
- Evidence: `_CxxThrowException` (`25C20D0`) has about 135 direct callers, almost all STL helpers
  (`_Xlength_error` `12B2D0`, iostream `12B710`/`12B930`, `operator new` failure paths `25A0FCC…`).
  `CSetVariableEffect::Execute` can reach `1403D2FC0`: `FUN_140345bc0(&e); _CxxThrowException(&e,
  &DAT_1430f60c0)` when the value variant is valueless (a `std::bad_variant_access`-style invariant).
  None of the hooked functions has a recoverable error that is reported by throwing.
- Impact: N6's "the engine barely uses exceptions" holds. One detail is worth recording: a throw through
  a `noexcept` frame calls `std::terminate` → `abort()`, which in the release CRT ends with `__fastfail`.
  That **bypasses the game's unhandled-exception filter**, so the player gets no Paradox crash dump, and
  the crash looks like a silent exit that points at CAPIE.
- Recommendation: keep N6 "later". When the hook-table refactor happens, drop `noexcept` only on the
  thin entry functions (the original call is the only thing that can throw) and keep CAPIE's own work in
  `noexcept` helpers. Effort M. Confidence high for the call sites, medium for the CRT detail.

---

## 2. Hot path cost

**P1 — the per-window pre-filter pays three `VirtualQuery` calls for memory the game just used**
- Evidence: `tick_observed` (`dll/capie_editor.cpp:110`) calls `window_may_be_own` for every window;
  it does `member(window,+0xE8)`, `member(model,0)` and `guarded_copy(model+0x8, 32)`, each a
  `readable()` → `VirtualQuery` (`core/memory.hpp:70`). The original Tick has just dereferenced
  `window+0xE8`, `+0xF8`, `+0x100`, `[+0xE8]+0x28`, `[+0xE8]+0x2C` and `[+0xE8]+0x538`
  (`BDB500` excerpt in §1).
- Estimate: vanilla defines about 20 scripted windows, Rename Anything 21 more; large overhaul mods
  define hundreds. At 60 fps and roughly 1 µs per `VirtualQuery` (a syscall): 41 windows ≈ 7 400
  syscalls/s ≈ 0.7 % of one core; 300 windows ≈ 5 %. The CAPIE-owned windows then run the full guards
  (tens of further `VirtualQuery` calls each) every frame while visible.
- Recommendation (S): inside the Tick hook, read `window+0xE8` and the template header with a plain
  `__try` copy (no `VirtualQuery`): the window is a live element of the manager array and the game read
  the same fields microseconds earlier. Keep `VirtualQuery` for everything else. Confidence high.

**P2 — classify windows once, at Bind, and compare pointers in Tick**
- Evidence: G2: every window is created in `BDBAB0`, which calls `BD8ED0` before the window is put in the
  array; every rebind goes through `BDBA60` → `BD8ED0`; windows are destroyed only at the top of
  `BDBAB0`. So the window pointer Tick receives has always been through Bind first.
- Recommendation (S-M): in `bind_observed`, after the existing `inspect_window`, store
  `{window pointer, template pointer, model index}` in a fixed array of at most 8 entries (own models
  plus grammar editors), and remove any entry whose pointer is re-bound as a foreign window. Tick then
  does one acquire-load and up to 8 pointer compares, never a memory read, for foreign windows. The
  pointer is used as an identity key only and never dereferenced from the cache. Even simpler variant:
  cache the **template** pointers (templates live in the global list `[33DD4A8]+0x28` for the whole
  session) and compare `[window+0xE8]` read under `__try`. Confidence high.

**P3 — a per-frame hook point exists: `CScriptedWindowManager` vtable slot `0x38`**
- Evidence: `29CB3A0` → `BDB970`, one call per frame, windows at manager `+0x20` (count `+0x2C`).
- Recommendation (M, later): replacing the inline Tick detour with an atomic vtable-slot patch (same
  method as N11) would give one CAPIE call per frame instead of one per window, and run the observers
  after all windows ticked (still the same frame). It costs one more RTTI-pinned vtable and a loop over
  CAPIE's cached windows. Only worth it if P1+P2 still show up in a profile. Confidence high.

**P4 — no cheaper "visible changed" function; the updater is not a replacement**
- Evidence: show/hide transitions are inline in `BDB500` (`vtbl[0x78]` show / `vtbl[0x80]` hide on
  `root+0x30`). The updater `BDD5B0` runs only while visible and dirty, so it misses the hide edge that
  focus release and lifecycle need. `+0xBF9` is the single source of truth, written once per Tick.
- Recommendation: keep reading `+0xBF9` after the original Tick. Confidence high.

---

## 3. Offsets and struct semantics

Re-confirmed here from the decompiled readers (in addition to the port notes):

| Offset | Meaning | Evidence |
|---|---|---|
| window `+0xE0` | idler (copy of manager `+0x18`) | `BDBAB0` `puVar6[0x1c]=*(mgr+0x18)` |
| window `+0xE8` | `CScriptedWindowTemplate*` | `BDBAB0` `puVar6[0x1d]`; Tick reads `+0x28`, `+0x2C`, `+0x90` (visible trigger, vtbl `0x18`), `+0xA4`, `+0x530/+0x538` (dirty trigger) |
| window `+0xF0` | `CScriptedWindowManager*` | G1 |
| window `+0xF8` / `+0x100` | root / parent root, nulled when destroyed | Tick |
| window `+0x110` | updater (context) | `BD7D40(w+0x110,…)`; Tick calls `BDD5B0(w+0x110)` |
| window `+0xBF8` / `+0xBF9` / `+0xBFA` | parent shown / desired / force-refresh | Tick, `BDA930` (G4) |
| context `+0x20` | template | `BDD5B0` uses `[ctx+0x20]+0x128..0x158` (trigger hash map) |
| context `+0x3C0` | scope used for every trigger and property | `BDD5B0` passes `param_1+0x3c0` to every trigger `vtbl[0x18]` |
| root `+0xA5` | `0x04` destroyed, `0x08` shown, `0x10` hidden by script | G3 |
| widget `+0x75` | same bits for child widgets (`0x10` = `_visible` false) | `BDD5B0` |
| politics `+0xD0` → party | ruling party; party `+0x18` = sub-ideology object, whose `+0x18` string is its key | `1622B90`, `66A4E0` (B1) |
| view `+0x5F0` | idler; slot `0xA0` returns the shown country tag | `1622B90`: `(*[view+0x5f0]->vtbl[0xa0])()` → `C07B80(tag)` |
| country `+0xFF0` | character manager (map `+0x10`, count `+0x1C`; sorted list `+0x28`, count `+0x34`; entries 16 bytes `{token, character*}`) | `716D30`, `1173230` (B4) |

**O1 — characters are identified by two ints, not one (area 3/6)**
- Evidence: `is_character` (`460A30`) accepts a match only when
  `*(uint*)(a+0xc) == *(uint*)(b+0xc) && *(uint*)(a+8) == *(uint*)(b+8)` on the scope objects
  returned by `57C980` (which itself requires `scope+0x50`/`+0x54` non-zero).
- Impact: CAPIE keys characters by `off::character_id = 0xC` only. If `+8` is a type or a generation
  field, two different objects could share `+0xC` (for example after loading another save, scan L4).
- Recommendation: key the portrait cache by the pair `+0x8`/`+0xC` (one more pinned read in the same
  instruction neighbourhood). Effort S. Confidence medium (meaning of `+8` not established).

**O2 — the template pointer is a more robust identity than the template name**
- Evidence: templates are built once into the global list `[33DD4A8]+0x28` (count `+0x34`) and the
  window stores the pointer (`BDBAB0`). The name at template `+0x8` is an MSVC string whose bytes CAPIE
  re-reads and compares every frame.
- Recommendation: part of P2: resolve the five own template pointers once (when the first own Bind is
  seen), compare pointers afterwards. Effort S. Confidence high.

---

## 4. Anchors that survive updates

Byte signatures break on every compiler change. The functions below carry RTTI or unique strings that
have survived several builds and can find the function first; the existing byte pins then only confirm.

| Target | Anchor | How |
|---|---|---|
| Tick `BDB500` | strings `"window for %s is destroyed.\n"` and `"Parent window for %s is destroyed.\n"` | xref → containing function |
| per-frame loop `BDB970` | `.?AVCScriptedWindowManager@@` → vtable `29CB368` slot `0x38`; string `"scripted_gui.window"` | RTTI, then the only `call` in the loop is Tick |
| Bind `BD8ED0` | `.?AVCScriptedWindow@@` → vtable `29CB328` slot `0x10` (`BDBA60`), whose second call is Bind | RTTI |
| Rebuild `BDBAB0` / window stores | string `"Window %s not found.\n"` | xref |
| Updater `BDD5B0` | strings `"_click_enabled"`, `"_visible"` (both inline in this function) | xref |
| SetVariable `37D1C0` | `.?AVCSetVariableEffect@@` → vtable `27F2F88` slot `0x68` | RTTI |
| `CCountry` | `.?AVCCountry@@` (TD `3251D78`, COL `2CA1F70`) → vtable `2864710` | RTTI |
| Party panel `1622B90` | `"ruling_party_info"`, `"leader_name"`, `"pol_faction_icon"`, `"POLITICS_NOELECTIONS"` | xref |
| FindX family | `"Could not find \"%s\" in window %s"` (port notes) | xref |
| `is_character` `460A30` | `": Scope is not a valid character "` | xref |
| has_dlc name table `1B7030` | `"Poland: United and Ready"` … `"Axis Armor Pack"` | xref |

**A1 — add a string/RTTI locator pass to `tools/port/`**
- Recommendation: for each hook, store `{anchor kind, anchor text, expected relation}` next to the
  pattern in the accepted JSON. The port tool resolves the anchor first, then checks the masked pattern
  inside that one function. That turns most of the 41 "needed analysis" cases of the last port into
  automatic hits, and catches a pattern that happens to match the wrong function. Effort M. Confidence
  high (all anchors above were resolved in this session).

---

## 5. Backlog design notes

**B1 — renaming the ruling party's sub-ideology (hook candidate `1622B90`)**
- Evidence: `1622B90` is the only writer of the panel `ruling_party_info`:
  ```
  party = *(politics(country)+0xd0);
  txt = root->FindText("ideology");          // vtbl 0x78
  SetText(txt, FUN_14066a4e0(*(party+0x18), tmp));
  ```
  `66A4E0(obj, out)` is `Localise(out, obj+0x18 /*key*/)` (`1422AE6F0`), and the formatter
  `1422ADFC0` calls `loc_lookup_n` (`241ECE0`), which CAPIE already hooks. `66A4E0` has 114 callers.
- Design: hook `1622B90` (one caller, UI thread) only to set a thread-local frame
  `{country, sub-ideology key}`; the existing `loc_lookup_n` hook answers the custom name when the frame is
  set and the key matches, exactly like the BuildNameKey frame. No new game call, no write. Do **not**
  hook `66A4E0` (too many callers). Other places that show the sub-ideology (tooltips, diplomacy) would
  still show the original; decide whether that is wanted.
- Effort M. Confidence high for the call chain.

**B2 — SetVariable through vtable slot `27F2FF0` (N11)**
- Evidence: RTTI name `.?AVCSetVariableEffect@@` (TD `31F8D88`), slot value `14037D1C0`, and the only
  references to `37D1C0` are this slot and `.pdata` `36BEE6C`. No subclass shares it.
- Design: `VirtualProtect` the 8 bytes, `InterlockedExchange64` the pointer, restore protection; keep the
  original pointer as the "trampoline". Verify the slot value equals `image_base + 0x37D1C0` before and
  after, and the RTTI name at init. Unhook = write back. The function is 60 bytes and its body is
  trivial, so the inline detour could also be dropped from the signature table.
- Effort S-M. Confidence high.

**B3 — country colour (colour picker wishlist)**
- Evidence: `1E31B0` loads `common/countries/colors.txt`; for each 3-letter tag it calls
  `(*country->vtbl[0x18])(country, reader)` on `[gamestate+0x310][index]`; slot `0x18` of the `CCountry`
  vtable `2864710` is the generic persistent reader `2540C70`. So colours are fields of each `CCountry`
  (`color` and `color_ui`, both registered tokens), loaded once.
- Open: the field offsets and the map redraw path were not found in this session. Next steps: find the
  `color_ui` token's parser through the token id at `EDFF0`, and the map-mode refresh that a
  `set_cosmetic_tag` triggers (one of the 15 `UpdateNames` callers, `379DC0` and neighbours).
- Note: writing a colour is a gamestate write and would desync multiplayer unless done through an effect
  on every machine; a display-only override in the map colour lookup is the safer design (ADR 0023).
- Effort L. Confidence medium (load path), low (storage).

**B4 — listing leaders and flags at runtime (API 8 catalog)**
- Evidence: `CCountry+0xFF0` is a character manager (`716D30` returns it, asserting `_pPtr`);
  `1173230(mgr, token, include_all)` looks the token up in the hash map at `+0x10` (16-byte entries,
  count `+0x1C`) and, when `include_all`, in the sorted array `+0x28` (count `+0x34`). Portraits are
  resolved per character by `ResolvePortrait` (already hooked) and checked with `PortraitEntryValid`;
  the fallback name `"GFX_leader_unknown"` appears in `B9B1F0`. Flags are not listed anywhere: they are
  probed by name (`BuildFlagCandidates` `1518DE0` builds `TAG_ideology` candidates and `LoadCountryFlag`
  tries them).
- Design: for leaders, walk `gamestate+0x310` countries → `+0xFF0` → `+0x28` array once after load (logic
  thread, read-only), and resolve each portrait with the game's own `ResolvePortrait` inside the existing
  hook context, so modded characters appear. For flags, build the catalog from the same candidate rule
  and test existence with the game's texture lookup, or keep the generator-side catalog. Gate the walk on
  the `_ThreadForbidCount` rule (only from a hook that the game calls on the logic thread).
- Effort L. Confidence medium.

---

## 6. Script-engine facts for Rename Anything

**R1 — `is_character` first tries the argument as a scope reference, and logs only on the token fallback**
- Evidence (`460A30`):
  ```
  lVar4 = scope_character(scope);                         // 57C980, needs scope+0x50/+0x54
  if (!lVar4) log(": Scope is not a valid character ");
  lVar5 = FUN_14058aa00(trigger+0x58, scope, 0);          // evaluate as value/scope reference
  if (!lVar5 && !(lVar5 = FUN_14058a900(trigger+0x58, scope)))   // token, relative to the scope's country
      log(": Input %s is not a valid character");
  return a+0xc == b+0xc && a+8 == b+8;
  ```
  `58AA00` uses the same evaluator (`58C090`) as `set_variable`'s value. `58A900` walks up to 20 scope
  parents to the first country and looks the token up in that country's character manager (`1173230`,
  B4); that is why a character of another country fails and logs.
- Recommendation for Rename Anything: store the target character in a variable once (in character scope,
  `set_variable = { rn_target_char = THIS }`), then test `is_character = var:rn_target_char`. The first
  path resolves it with no country lookup and no log. Cheaper still, compare a value RA already stores on
  each character (`check_variable = { rn_cp_id = … }`), which never resolves a character at all.
  Needs an in-game check that a character-valued variable resolves through `58B820`. Effort S.
  Confidence medium.

**R2 — scripted GUI properties can show a character portrait directly**
- Evidence (`BDD5B0`, image properties at updater `+0xA88`, 16-byte entries `{widget, def}`):
  ```
  value = EvalText(prop, scope=ctx+0x3c0);
  if (len==4 && value[0]==0x12)  → parse tag, B96A80(...)            // flag icon
  else if (scope_character(scope) && len==8 && value=="portrait")   // 0x7469617274726f70
       → if (hash changed) B9B1F0(gfx, widget, character, …)       // portrait, fallback GFX_leader_unknown
  else SetSprite(widget, value);
  ```
  `B9B1F0` builds a portrait entry from the character (`+0x100` override path) and validates it with
  `PortraitEntryValid` (`1438E40`).
- Impact for S4 (mini portraits): `image = "portrait"` in `properties` shows the scope character's
  portrait natively, with the game's own fallback, and is redrawn only when the character changes. It
  needs a window (or dynamic-list entry) whose scope is the character, which is why the typed row
  variable did not work.
- Recommendation: test one entry with a character context before keeping the numeric overlay grid;
  if it works, the generated `GFX_rnchp_*` catalog and its DLC mapping can go. Effort S (test).
  Confidence medium (scope source for list entries not traced).

**R3 — `has_dlc` knows exactly 18 hard-coded names**
- Evidence: the has_dlc parser `3FC9C0` stores `FUN_1401b7030(name)` at `+0x58` and fails when it is 0;
  `1B7030` is a chain of `memcmp`s against fixed display names returning bit flags:
  `Poland: United and Ready` 1, `Together for Victory` 2, `Death or Dishonor` 4, `Waking the Tiger` 8,
  `Man the Guns` 0x10, `La Resistance` 0x20, `Battle for the Bosporus` 0x40, `No Step Back` 0x80,
  `By Blood Alone` 0x100, `Axis Armor Pack` 0x200, `Arms Against Tyranny` 0x400, `Trial of Allegiance`
  0x800, `Expansion Pass 1: Supporter Pack` 0x1000, `Gotterdammerung` 0x2000, `Graveyard of Empires`
  0x4000, `No Compromise, No Surrender` 0x8000, `Peace For Our Time` 0x10000, `Thunder at Our Gates`
  0x20000. Comparison is exact (case and length). Ten other callers (game rules `A84300`, `6C3E50`
  `"invalid dlc name: "`) use the same table.
- Impact: cosmetic packs (unit, music, portrait packs) can never be gated with `has_dlc`; any other name
  is a script error at load. The four integrated DLCs are still valid names (scan M15).
- Recommendation: in `tools/modgen`, gate only on these 18 names (generate the list from this table on
  each port) and treat cosmetic-pack portraits as "present if the texture exists" (pack 0, no gate), or
  drop them. Add a `check.py` rule that rejects any `has_dlc` value outside the list. Effort S.
  Confidence high.

---

## Top 10

1. **P1** — read the Tick window and template fields with a plain `__try` copy, without `VirtualQuery`. S.
2. **P2 / O2** — classify windows once in Bind (pointer or template-pointer set), pointer compare in Tick. S-M.
3. **R3** — restrict generated `has_dlc` gates to the 18 hard-coded names; add the `check.py` rule. S.
4. **R2** — test `image = "portrait"` with a character-scoped entry; it could replace the overlay grid (S4). S.
5. **G3** — treat root flag `0x04` as "destroyed" and refuse every game call for that window (focus release). S.
6. **R1** — use `is_character = var:…` or a stored id compare in Rename Anything to avoid the token path and its log lines. S.
7. **B2 (N11)** — move the SetVariable hook to vtable slot `27F2FF0`, RTTI-checked. S-M.
8. **A1** — add string/RTTI anchors to `tools/port/` so the next port locates functions before matching bytes. M.
9. **G1 / G2 / G4** — documentation: manager identity at `+0xF0`, `BDBAB0` is the rebuild, `+0xBFA`; RTTI check for the manager. S.
10. **B1** — sub-ideology rename via a thread-local frame in `1622B90` plus the existing `loc_lookup_n` hook. M.
