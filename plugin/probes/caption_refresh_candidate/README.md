# Caption refresh candidate (INACTIVE)

Fixes, once integrated and runtime-accepted, the bug where the politics leader
caption and the formatted party short-name row update only after reopening the
politics panel. Nothing here is activated, injected or wired into `mod/`.

## What it does (design)

After a successful own Apply, the script side advances an ordinary success counter
(`rn_ip_lui_rev` leader, `rn_ip_pui_rev` party). The native side observes the counter
through a carrier text in the editor and launcher windows and, on an increase, asks
the game to refresh the politics view once with the existing native request
`B6E210(CGameGui*, 12)`. That only sets the view dirty byte (`view+44`); the normal
frame loop (`B68C60` -> `1236980` -> observer slot 0x48 `15863F0`) rewrites the
leader caption (`1588A60`) and the party rows (`1588370`) with native formatting.
No caption is written by us, no command is executed, no view is rebuilt.

## Guarantees in code (`caption_refresh_core.hpp`)

- Exact EXE SHA256 pin, 23 whole-instruction fingerprints unique in `.text` plus the
  3 hook-target fingerprints inherited from the render bridge, 4 vtable slot pins and
  2 token DWORDs; any mismatch refuses initialization.
- Bounded reads only (VirtualQuery + SEH in process, region-checked fake memory in tests).
- Fresh chain on every use, never cached: `window+E0` idler (VT 2968FF0, slot B8 ==
  DCCF30) -> `idler+6B8` (exact semantics of DCCF30) -> GameGui (VT 294A688,
  `gui+180 == idler`) -> `gui+C8+12*8` view (VT 29D9648, observer VT 29D96F0, slots
  28/38/48 pinned) -> `view+38 == gui`, `view+570 == idler`, `view+578 != 0` ->
  own window `+100 == view+578`, `+108 == 0`, model `+28 == 3A70`, `+2C == 3AB2`,
  `+60 == 0`, `+80 == 0`. Pointers exist only on the stack of one call.
- Revision seeded on the editor opening (prefill time), before any Apply can be
  clicked. Launcher reads never create a baseline; an existing seed is never
  overwritten (fixes the review BLOCKER: first-launcher-tick baseline lost Apply 1).
- `dispatch_allowed(field, rev)` gate for a future integrated Apply dispatch: seed
  required, no unobserved jump, fixed-point headroom.
- Exactly one native request per plan, no lock held across it, attempt recorded
  before the call; ack only after fresh relookup, identical gui/view, `dirty == 1`
  and the counter re-read. Native counters advance only on that verified result.
- Failures suspend that revision (no retry); guard rejects defer to the next
  visibility edge (no per-frame retry). Hidden keeps seed/ack/pending.
- Bind, invalid own context and counter rollback drop baseline/pending; the next
  Open seeds again. Jumps coalesce into one refresh.
- Hook bodies forward to the original exactly once, first, unconditionally.

## Not done / OPEN (enforced as refusal)

- **Authority**: no proven scalar country/world decoder (review). `authority_proven()`
  is constant `false`: even an armed build never calls `B6E210`.
- **Script side**: success counters, carrier widgets (`rn_ip_leader_rev`,
  `rn_ip_party_neutrality_rev` are proposed names) and a fixture copy do not exist.
  Carrier number formatting for large values is unverified; non-digits are rejected.
- **Dispatch integration**: the seed gate must be added to the render-bridge
  dispatch path in a separately reviewed copy; this standalone shell has no dispatch hook.
- **Hook co-existence**: Tick/Bind are also hooked by every editor bridge. The armed
  variant must not be co-loaded with any of them; intended path is integration of
  the core into one reviewed bridge copy (not focus bridge).
- Load without Bind at identical scalars, frame ordering of the dirty consume, real
  visual refresh of caption/party row/tooltip: runtime QA only.

## Build and test

`build.ps1 -MinHookDir <path>` -> `plugin/build/caption-refresh-candidate/`:
inactive DLL (`RNInitialize` verifies everything and returns 41, no hooks, no MinHook
linked), an armed compile-only `/c` check, and `caption_refresh_test.exe <hoi4.exe>`
(fake-memory suite + static fingerprint check of the EXE *file*). `/MT /W4 /WX`.
The script never inspects or touches a game process.
