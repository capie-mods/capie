# Design: renaming the ruling party's sub-ideology (finding B1), and the country colour

Status: design only, not implemented. Build: HOI4 1.19.4.0 (`hoi4.exe` SHA-256 `B6650C85…D888`).
RVAs are relative to `0x140000000`. Evidence was re-read on 2026-10-28 with capstone on the
installed exe (`tools/port/locate.py` and its anchors); the Ghidra session had crashed (Java
OutOfMemoryError) and was not available again before this note was written. Claims that rest
on the review's Ghidra reading only are marked **pending Ghidra**.

## 1. Evidence (verified)

### 1.1 The writer: `1622B90`, the politics panel "ruling_party_info"

Anchor: the callee of `CCountryPoliticsView` observer slot `0x48` (`observer_refresh`,
`1620890`) that references `"ruling_party_info"` and `"POLITICS_NOELECTIONS"`
(`anchors.json`: `politics_panel`).

- Only caller: `1620890` at `1620BB6`, with `rcx = rdi = observer - 0x30`, i.e. the
  `CCountryPoliticsView` itself (`add rdi,-30h; mov rcx,rdi; call 1622B90`).
- Prologue `mov rax,rsp; mov [rax+10h],rbx; …` (7 bytes before the first branch, no RIP
  operand): hookable by MinHook.
- Body, in order (instruction addresses):

  ```text
  1622BBA  rcx = [view+5F0]; call [vtbl+A0]          ; idler -> shown tag
  1622BD0  call C07B80 (tag -> CCountry)
  1622BD8  call 717370 (country_politics_getter)      ; politics = [country+F90]
  1622BE0  r14 = [politics+D0]                        ; ruling party
  1622BEA  call 12C4E70 (view_get_root)               ; root = [view+5F8]
  1622BF9  FindX(root, "ruling_party_info")           ; root vtbl+1B8
  1622C25  if party == 0 -> skip
  1622C31  FindX(panel, "ideology")                   ; panel vtbl+78 (instant text)
  1622C65  rcx = [party+18]                           ; sub-ideology object
  1622C69  call 66A4E0(subideology, &tmp)             ; localised name -> tmp
  1622C74  call 23479F0(text_widget, &tmp)            ; SetText: copies into widget+110
  1622C7C  call 1230500 (party_first_leader_role)     ; then "leader_name", ...
  ```

- `66A4E0(obj, out)`: `rdx = obj+0x18` (MSVC string: SSO when `[obj+0x30] <= 0xF`), then
  `22AE6F0(out, key, 0, 0)` (Localise). 115 call sites use `66A4E0` (all objects with a key at
  `+0x18`); `22AE6F0` has 3 762.
- `22AE6F0` → formatter `22ADFC0` (anchor `loc_formatter`, string "Reached max recursive
  depth"), called with the key range. The formatter refuses depth `>= 0x20`, then
  `call 241ECE0` (`loc_lookup_n`, CAPIE's existing hook) with `(key, end - key)` at `22AE0F9`,
  then scans the result for `'$'` (`mov edx,24h; call 25C37E0`) and recurses (`22AE25B`) for
  nested `$KEY$`. The answer is copied into `out`; nothing keeps the pointer that
  `loc_lookup_n` returned after the formatter returns.
- `23479F0` copies the string into the widget (`widget+0x110`, the same
  `off::instant_text`), so the temporary may die right after.

Thread: `1622B90` runs inside the politics observer refresh, i.e. in the GUI frame loop
(`observer_consume` → slot `0x48`), the thread that also runs Tick (review G6: the
gamestate-access thread). It reads game state directly (`C07B80`, `[country+F90]`), so the game
itself treats that thread as allowed to read the country.

### 1.2 What is not verified

- The meaning of `[party+0x18]` as "sub-ideology object" and of its `+0x18` string as the
  sub-ideology token (for example `despotism`) comes from the review (**pending Ghidra**:
  the object's class has no RTTI read here). The capstone reading shows only "object at
  `party+0x18`, string at `object+0x18`, localised".
- Which other places localise the same key (tooltips of the ideology icon, diplomacy, the
  government change event) was not enumerated (115 callers of `66A4E0`).
- Whether `observer_refresh` runs only when the view is dirty or every frame while visible
  (the dirty byte `+0x44` suggests "when dirty"; **pending Ghidra** or a runtime trace).

## 2. Hook and frame

```text
hook 1622B90(view):                               // UI thread, one caller
    country = shown country (the same calls the original makes, read-only, guarded)
    entry   = subideology_cache.find(country)     // filled on the logic thread (section 3)
    if !entry or !enabled: return original(view)
    key = ruling party's sub-ideology key (guarded reads: [country+F90]+D0 -> +18 -> +18)
    if key != entry.key: return original(view)     // party changed since the commit: fail closed
    Frame f{key, entry.text}; previous = active_sub_frame; active_sub_frame = &f
    original(view)
    active_sub_frame = previous

hook loc_lookup_n(key, len):                      // existing hook, any thread
    f = active_sub_frame                           // thread_local; null on every other thread
    if f && len == f.key.size() && memcmp(key, f.key) == 0: return f.text.c_str()
    ... existing behaviour ...
```

- Same pattern as the `BuildNameKey` frame (`name_layer_hooks.hpp`: `in_frame`,
  `thread_local Frame* active_frame`). No game call is added and nothing is written.
- Exact key match only. Nested lookups the formatter makes for `$…$` inside the answer, and the
  `leader_name`/election texts after `66A4E0`, have other keys and pass through.
- The answer must not contain `$` (it would be expanded) or `§` codes: the section 7 alphabet
  already excludes them; the cache builder rejects such text anyway.
- Lifetime: `f.text` lives in the frame until `original(view)` returns; the formatter copies
  the answer before that.
- Cost on the hot localisation path: one `thread_local` load and a null test per lookup, on
  every thread. No lock, no atomic RMW.
- Do not hook `66A4E0` or `22AE6F0`: 115 / 3 762 callers, many on worker threads.
- Reading the country inside the hook duplicates three calls of the original. Cheaper and
  safer: compare the frame by the **key** only and let the cache be keyed by the key the
  layer was committed for plus the country tag index read from `[view+5F0]` slot `0xA0`
  (one guarded virtual call, no gamestate walk). Either is acceptable; the first is simpler to
  test.

New layout constants (with `anchors.json` entries, which already exist as helpers):
`politics_panel = 0x1622B90` (`layout: true`), `off::party_subideology = 0x18`,
`off::subideology_key = 0x18`. New pins: the `mov rcx,[r14+18h]; call 66A4E0` pair at
`1622C65` (proves both offsets and the callee), and the `call 241ECE0` in the formatter at
`22AE0F9` (proves the lookup is the one CAPIE hooks).

## 3. Carrier and API (CAPIE contract, Rename Anything)

Follow the faction layer (CONTRACT section 15): variables written by script, a cache filled on
the logic thread, the UI hook reads only the cache.

- Contract section 19 "Sub-ideology name layer", API level 8. Variables on the country:
  `capie_sn_n` (array of section 7 codes, 1..32), `capie_sn_ver` (`1`), `capie_sn_rev`
  (set last; commit). SDK effects `capie_v1_set_subideology_name` (from text slot 1 of ROOT)
  and `capie_v1_clear_subideology_name`.
- Commit (logic thread, inside the existing SetVariable hook; see
  `setvariable-slot-hook.md`): on `capie_sn_rev`, read the layer of the scope country, read its
  current ruling sub-ideology key, and store `{tag index, key, text}` in a fixed-size,
  lock-free cache (same publication scheme as the faction cache). Also rebuild the cache after
  a load from the `update_names` hook, like the faction cache.
- The key captured at commit time binds the name to that sub-ideology: after a coup, an
  election or `set_politics` the panel shows the game's name again without any script. Rename
  Anything may clear the variables in `on_ruling_party_change` / `on_government_change` to
  keep the save tidy, but CAPIE does not depend on it.
- `RNCountryCapabilities` gains a bit for this layer; Rename Anything shows the "rename
  sub-ideology" field only when CAPIE reports level 8 and the layer's pins passed.
- Rename Anything UI: one more target in the country editor (text slot 1, same alphabet,
  32 characters), Apply runs the SDK effect in the country's scope. Multiplayer: the variables
  are game state written by an effect on every client; CAPIE changes only what one panel shows.
- Redraw: the panel shows the new name at its next refresh (reopening the politics tab).
  `capie_core.dll` has no view-refresh call; asking the editor DLL's caption chain
  (`RequestViewRefresh(gui, 12)`) to refresh the politics view after a commit is optional and
  would need its own thread check (commit is on the logic thread). Decide after the in-game test.

## 4. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Other screens show the original sub-ideology name | inconsistent text | documented limit, like section 14's "not covered" list |
| `[party+18]`/`+18` meaning wrong | wrong key compared, no match | exact key match fails closed; pin of `1622C65` |
| the panel writer gets inlined or split on a rebuild | hook target missing | anchor `politics_panel` fails in `locate --check`; layer off |
| the same key localised on the same thread during the frame by an unrelated nested text | that text also renamed | only inside `1622B90`'s call; its only nested localisations are the leader and election texts |
| `thread_local` access from a hook on a game thread created before the DLL loaded | MSVC TLS of a DLL loaded later | `capie_core.dll` already uses `thread_local` in hooks (`name_layer_hooks.hpp`); same rule |
| a `$` in the answer | expansion of an unknown key | alphabet has no `$`; builder rejects it |

## 5. Tests

- Offline: `subideology_layer_test.cpp` with a fake `loc_lookup_n` original: frame set → exact
  key answered, other keys and other lengths pass through, nested frames restore the previous
  pointer, no frame on another thread (`std::thread`), cache miss and key mismatch call the
  original unchanged.
- Cache: commit with a valid/invalid layer (codes out of range, count 0 or 33, missing
  `capie_sn_ver`), key captured at commit, publication under a concurrent reader.
- Exe tests: the new pins (`1622C65` pair, `22AE0F9` call) and `politics_panel` through
  `port.py locate --check`.
- In game: rename the sub-ideology of a country, open the politics tab (name shown), change the
  ruling party by console (`set_politics`), check the game's name returns, save/load, and a
  multiplayer client with and without CAPIE.

## 6. Country colour storage (left open by the review, B3)

Found (capstone, 2026-10-28):

- `1E31B0` opens `common/countries/colors.txt` (string at `1E3208`) and, per tag, calls the
  country's vtable slot `0x18` (review B3). `CCountry` slot `0x18` is `2540C70`, the generic
  persistent reader; it starts with `call [this->vtbl+0x30]`, and `CCountry` slot `0x30` is
  `154030` = `ret 0` (no field table from that slot). So the per-field handling of `color` is
  not in a `CCountry` override of the reader.
- The tokens are registered in the big token-registration function `83760`: `"color"` at
  `845FA` into the token object `343C1B0`, `"color_ui"` at `EDFF0` into `34C1860`
  (`lea r8,"…"; mov edx,[rax]; lea rcx,[rip+token]; call 25471A0`). The only other RIP
  references to those two token objects are in two small functions (`2711310`,
  `274C7D0`), most likely their atexit destructors.
  The field parser therefore does not load the token objects by address; it compares the
  numeric token id copied elsewhere (a token-id table or a switch on a constant). The review's
  "token id at `EDFF0`" is the registration site, not the parser.
- Other users of the `"color"` literal: `36F0` (a static initialiser building a 0x120-byte
  descriptor with the name `"color"`; candidate for the persistent field table of some class),
  `2CE300` and `2160030` (an effect parameter check: `"Parameter '%s' is invalid"`). None of them
  was tied to `CCountry` yet.

Not found: the `CCountry` field offsets of `color` / `color_ui`, and the map redraw path after a
colour change. **Pending Ghidra**, next steps:

1. Decompile the `36F0` initialiser around `11A62` and find which class's descriptor table it
   fills (the descriptor holds the field offset); check whether the same table is reached from
   `2540C70` through a non-virtual path.
2. Search `.text` for `cmp …, imm` with the numeric ids of `color`/`color_ui` (read the ids at
   runtime or from the registration order) inside functions called from `2540C70`.
3. For the redraw: start from `set_cosmetic_tag`'s effect (one of the `update_names` callers,
   review B3), follow what marks the political map mode dirty after `UpdateNames`, and look for
   a map-mode refresh that reads the colour back from `CCountry`.

Design constraint (unchanged, ADR 0023): a colour change written into `CCountry` is game state
and would desync multiplayer unless every machine runs it from an effect; the safe design is a
display-only override at the point where the map mode reads the colour, keyed like the layers
above. It cannot be designed concretely until step 3 finds that read.
