# Texture release in HOI4 1.19.4.1 (closes O1)

Date: 2026-11-01. Binary: `hoi4.exe` 1.19.4.1, sha256 `cf781ed1…0476f`, image base `0x140000000`.
Tools: Ghidra 12.1.4 (REA bridge), pefile/capstone. Static reading only, no in-game probe yet.
The addresses below are virtual addresses (VAs). CAPIE's RVA = VA − `0x140000000`.

Context: [api9-runtime-catalogs-spec.md](../contributing/api9-runtime-catalogs-spec.md) §3.3 and R2;
[runtime-catalogs-2026-11.md](runtime-catalogs-2026-11.md) §3.1, §3.6 and O1.

## 1. Summary

- **A release function exists and is usable.** `ReleaseTexture(texmgr, id)` at `0x14248B4F0` takes
  the same global mutex as `AddTexture` (`0x1431383F0`) and decrements the record refcount `+0x30`.
  At zero it destroys the GPU texture at once (D3D11 `Release` of the COM objects, then `delete`),
  erases the path from the hash and puts the id on a free list. Confidence: high.
- The game uses it itself. Its own **sprite re-point** `CSpriteType::SetTextureFile`
  (`0x1422A77D0`) does `Release(old id)` → `AddTexture(new path)` → `GetTexture` → `TextureSize`. That
  is exactly CAPIE's §4.3 sequence plus the release. It runs from the `reload_textures` console
  command and from the gfx/interface file watcher, on live and visible sprites.
- **A sprite owns one reference** on the id in `+0x1A0`. The CSpriteType destructor and
  `UnloadTexture` (vtable `+0x50`) release that id. **This changes CAPIE's protocol even if CAPIE never
  frees anything** (§4).
- Nothing in the texture manager evicts textures. There is no LRU, no frame counter and no
  deferred-destroy queue: the 0x48-byte record has no timestamp field.
- Portrait path entries (`+0x28`) hold no reference. Copies of them add none, and no release call was
  found in the portrait or character code. Runtime leader textures stay loaded for the session.

## 2. The texture manager (`texmgr = *(spriteDB + 0x170)`)

Record array `texmgr+8`, 0x48 bytes per id:

| off | field |
|---|---|
| `+0x00` | `texture*` (game wrapper, 0x58 bytes; COM objects inside) |
| `+0x08` | path, MSVC `std::string` |
| `+0x28` | size (two ints) |
| `+0x30` | **refcount** (int) |
| `+0x38` | byte: loaded / loadNow |
| `+0x3C` | the 12-byte options passed to `AddTexture` |

The other fields: the path hash table is at `texmgr+0x28` (FNV-1a) and the free-list head at
`texmgr+0x20`. The per-id side table at `texmgr+0x48`/`+0x50` is used only for async loads.

| VA | Role | Lock |
|---|---|---|
| `0x142488BD0` | `AddTexture(texmgr, const string* path, opts*, bool loadNow) -> id`. If the path exists, `refcount++`. Otherwise it allocates an id from the free list and sets `refcount = 1`. | takes `0x1431383F0` |
| `0x142489670` | `GetTexture(texmgr, id) -> texture*`. Adds **no** reference; loads lazily when the record is empty. | takes it on the load path |
| `0x1424894A0` | `TextureSize(texmgr, out, id)` | – |
| **`0x14248B4F0`** | **`ReleaseTexture(texmgr, id)`**: `lock; Release_nolock(texmgr, id); unlock` | **takes `0x1431383F0`** |
| `0x14248B450` | `ReleaseTextureByPath(texmgr, const string* path)`: looks up the hash, then `Release_nolock(id)` once | takes it |
| `0x14248B550` | `Release_nolock(texmgr, id)` (body below) | none: never call directly |
| `0x142489880` | `FindIdByTexture(texmgr, texture*) -> id`: linear scan of the hash | takes it |
| `0x1424951B0` (`[0x143138308]`) | GPU destroy: `0x142493ED0` then `Release` (`vtbl+0x10`) on three COM pointers, then `delete` of 0x18 and 0x58 bytes | – |

Excerpt, `Release_nolock` `0x14248B550`:

```c
rec = texmgr->records + id*0x48;
if (--rec->refcount == 0) {
    if (rec->texture) { GpuDestroy(rec->texture); rec->texture = 0; }  // [0x143138308] = 0x1424951B0
    erase path from hash (texmgr+0x28);            // texmgr+0x38 count--
    erase id from async side table (texmgr+0x50);  // texmgr+0x58 count--
    push id on free list (0x1424890D0, head texmgr+0x20);
}
```

There is no bounds check, no `refcount > 0` check and no deferral. An over-release drives the
refcount negative, or frees a texture that someone else still draws. **Ids are reused**: a stale id
may name another file later.

## 3. Candidates examined

### 3.1 Refcount on the texture handle: found
- `AddTexture` `0x142488BD0`: `*(int*)(records + id*0x48 + 0x30) += 1` on a hash hit.
- `ReleaseTexture` `0x14248B4F0`, excerpt:
  `mov ebx,edx; mov rdi,rcx; lea rsi,[0x1431383F0]; call 0x1425567C0 (lock); call 0x14248B550; call 0x1425569F0 (unlock)`.
- Thread: any thread (it takes the mutex). GPU destroy runs on the caller's thread, inside the lock.
  It only calls COM `Release` and frees memory, with no device-context call, so D3D11 is fine with it.
- Safe from a CAPIE hook on the UI thread if (a) CAPIE owns the reference it drops, and (b) no
  game object still holds the `texture*` without a reference (see 3.6).

### 3.2 Container erase path: found
The erase happens inside `0x14248B550`, as shown in §2. Callers of the locked wrappers: 16 of
`0x14248B4F0` (among them sprites, map symbols, fonts and text sprites) and 6 of `0x14248B450`
(`CMapSymbolDefinitionInstance` `0x141BB1E10`, `CTextSprite` vtable `0x142BBC9F0` slots `0x1423529C0`
and `0x142353B10`, `0x1412FAF90`, `0x142352670`).

### 3.3 Reload commands: found, and they prove the game re-points live sprites
- Console `reload_textures` / `reload_texture` (strings `0x1427A8010`/`0x1427A8028`; help text
  `"Reloads all textures, with an optional filter…"`). Implementation `0x1402929C0`: walks the
  sprite map `*(*(0x1433DDB08)+0x4F0)` (+0x104 count, +0x108 buckets). For each `CSpriteType` whose
  path matches the filter it calls `0x1422A77D0(sprite, sprite+0x70)`, and for each
  `CCorneredTileSpriteType` it calls `0x142303750`. It then prints `"<n> textures were reloaded."`.
- File watcher `0x140188DC0`, registered with `0x1422B75A0` for the `interface` and `gfx` folders
  (`0x14018B454`/`0x14018B488`). On a changed `.dds/.tga/.png/.bmp`, it calls `0x1422A77D0` on every
  sprite with that path.
- `CSpriteType::SetTextureFile` `0x1422A77D0`, excerpt:
  ```c
  sprite->path(+0x70) = newPath;
  if (!sprite[+0x1D3]) { vt[+0x50](unload); vt[+0x48](load); }
  else if (sprite[+0x1A0] >= 0) {
      ReleaseTexture(texmgr, sprite[+0x1A0]);              // 0x14248B4F0
      id = AddTexture(texmgr, &sprite->path, {0,0x1000001,0}, 1);
      sprite[+0x1A0] = id; sprite[+0x170] = GetTexture(texmgr, id);
      sprite[+0xA4]  = TextureSize(texmgr, id);
  }
  ```
- Thread: console commands and the file watcher run on the main (UI) thread. This is inferred
  from the code, not proven by an assert. They act on sprites that may be on screen, and the game
  does not crash after `reload_textures`. **This is strong evidence for O2: widgets read
  `sprite+0x170` per draw and do not cache `texture*` at `SetSprite`.** Confidence: medium-high. A
  probe should still confirm it (§5).
- Safe for CAPIE? CAPIE could call `0x1422A77D0` directly, but it rewrites `+0x70`, and it releases
  before adding: when a refcount is 1 and the same path is set again, the file loads from disk again.
  Prefer CAPIE's own sequence with release-after-swap (§4).

### 3.4 Sprite and texture destructors: found
- `CSpriteType` vtable `0x142BB3420` (COL at `-8`). Slot `+0x00` dtor `0x1422A7630` →
  `~CSpriteType` `0x1422A7090` → `UnloadTexture` `0x1422A79D0` (also slot `+0x50` via thunk
  `0x1422A7ED0`). Slot `+0x48` = load `0x1422A82D0`, slot `+0x88` = `0x1422A8230` (size, as in the research).
- `UnloadTexture` `0x1422A79D0`, excerpt:
  ```c
  if (sprite[+0x1A0] >= 0) { ReleaseTexture(texmgr, sprite[+0x1A0]); sprite[+0x1A0] = -1; }
  for each frame f in sprite[+0x118] (count +0x124, stride 0x90):
      if (f+0x80) { ReleaseTexture(texmgr, FindIdByTexture(texmgr, f+0x80)); f+0x80 = 0; }
      if (f+0x78) { … same … }
  ```
  It does **not** clear `+0x170`.
- The same unload slot (`0x1422A7ED0`) is used by `CMaskedSpriteType` (`0x142A60430`),
  `CFrameAnimatedSpriteType` (`0x142BC5AB8`) and `CTileSpriteType` (`0x142BC65E8`).
- Thread: the main thread, during interface or sprite database teardown and reloads.

### 3.5 DirectX 11 release and caching: immediate, no LRU
- `[0x143138308]` = `0x1424951B0` (GPU destroy), `[0x143138310]` = `0x142493ED0` (inner release),
  `[0x143138358]` = `0x1424966C0` (GetTextureDesc). The destroy releases its COM pointers through
  `call [rax+0x10]` (`IUnknown::Release`) and frees the 0x58-byte wrapper. No queue exists.
- No LRU or unused-frame counter exists anywhere in the record layout, in `GetTexture` or in the release path.
- Safety: D3D11 keeps resources that are still bound to the immediate context alive, so the
  GPU side is safe. The game's **wrapper** (0x58 bytes) is freed at once, so a CPU-side holder of the
  `texture*` would dangle. Holders without a reference are the problem, not the GPU.
- `0x140125FA0` releases a held `texture*` via `FindIdByTexture` + `ReleaseTexture` after asserting
  `ThreadIsMainThread()` (string `0x142794980`). It is the only explicit thread assert seen on the
  release path, and it implies the game treats texture swaps as main-thread work.

### 3.6 Runtime leader portraits: never freed
- `MakeIconEntry` `0x1438AC0` and the builders `0x14393D0` and `0x1439480` call `AddTexture` (+1). The
  portrait entry copy copies `+0x28` and adds no reference (research §3.1).
- None of the 16 + 6 + 5 direct callers of the three release functions lie in the portrait or
  character code (`0x1438000–0x143A000`, `0x1170000–0x1175000`, `0x371D60`). The string
  `set_character_portrait` does not exist in this build.
- Conclusion: a texture loaded for a portrait path (KRX `create_country_leader` with `.png`) holds
  its reference for the whole session. Because copies hold no reference, **nobody, CAPIE included,
  may release a portrait-path texture.** Doing so would leave dangling `texture*` in character
  entries and GUI copies. Confidence: high for "the game does not free them"; direct calls only,
  since indirect calls were not enumerated.

### 3.7 Loading screen
Not identified in the time available. No texture-manager release call sits near loading-screen
strings. Fonts (`CGameBitmapFont`, vtable `0x142A602A0` → `0x1416E1EE0`) and text sprites
release by id or path when they are rebuilt. This does not change the conclusions.

## 4. What CAPIE should do

### 4.1 Slot sprites (flag and portrait-sprite catalogs, spec §4.3)
**Ownership rule: a slot sprite owns exactly one reference on the id in its `+0x1A0`** (the
game's destructor, `UnloadTexture`, `reload_textures` and the file watcher all drop it). CAPIE holds
no separate references for slots.

Re-point, on the UI thread, hidden set only:

1. `new = AddTexture(texmgr, &path, {0,0x1000001,0}, 1)` (+1, owned by the slot from now on).
2. `tex = GetTexture(texmgr, new)`. If `tex == 0`: `ReleaseTexture(texmgr, new)` and keep the slot unchanged.
3. `old = sprite[+0x1A0]`. Write `+0x1A0 = new`, `+0x170 = tex`, `+0xA4 = size` (the order is unchanged).
4. If `old >= 0`: **defer** `ReleaseTexture(texmgr, old)` to a UI-thread queue drained at least
   2 GUI updates later. Release after the swap, never before, unlike the game's `0x1422A77D0`.

For the placeholder, do **not** write the saved placeholder id as raw values any more. Re-point to
the placeholder path with the same 4 steps. `AddTexture` hits the cache, so this is cheap and
correct. With the release in place, writing a saved id without `+1` lets `reload_textures`, the
file watcher or interface teardown over-release the placeholder. The spec as written already has this
bug for the first id CAPIE overwrites, because the lazy loader's reference on the placeholder leaks. That
bug is harmless, but under the new rule it has to go.

Preconditions:
- UI (main) thread, inside the guarded frame. Never on a logic thread. The mutex makes the call
  itself safe anywhere, but the sprite fields are only coherent on the UI thread.
- Re-find the sprite with `FindSprite` and check its vtable before each write: a GFX reload destroys
  sprites and releases their ids.
- Never release an id CAPIE did not obtain from its own `AddTexture`, and release each obtained id
  exactly once. Ids are reused after they are freed.
- Never touch the visible set (the existing two-set rule). The deferred release covers the frame in
  which the hidden set was last visible.
- Gate it on signatures: add `rva::texture_release = 0x248B4F0` and pin the prologue bytes
  `48895C2408 4889742410 57 4883EC30 8BDA 488BF9` together with the call to `0x248B550`. If that fails,
  do not release (fallback: today's leak with the cap).

### 4.2 Portrait path cache (spec §6, `0x14393D0`)
Leave it as specified: never freed. Its size is bounded by the distinct paths that are actually
resolved, which the game itself also leaks.

## 5. Change to the spec cap (§3.3, R2, O1)

- Replace "at most 1024 distinct textures loaded through catalogs per session" with a **live**
  bound: at most `2 × N + 2` textures referenced by each view (N ≤ 48 slots) plus the deferred-release
  queue. That is at most 98 per view and about 1 600 for 16 views in the worst case. In practice it is
  near 100, because pages flip one at a time.
- Keep a **safety cap of 1024 outstanding CAPIE references** (AddTexture count minus releases) only as
  a fail-closed guard. It must not be a per-session budget. Past it, show the placeholder and log once.
- If the release signature fails, fall back to the current per-session cap of 1024.
- Close O1 with this document. O2 (per-draw read) is now probably "yes" because of `reload_textures`
  (§3.3), but keep S2. Add an in-game test: page through 2 000 flags and check that the texture count
  (`texmgr+0x38`) returns to its baseline after closing the view, and that `reload_textures` with the
  view open does not crash.
