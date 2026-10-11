# Caption refresh candidate (INACTIVE)

Fixes, once integrated and runtime-accepted, the bug where the politics leader
caption and the formatted party short-name row update only after reopening the
politics panel. Nothing here is activated, injected or wired into `mod/`.
Revision 2 addresses independent review 70F97F58…01D6 (MAJOR-1..3, MINOR-1..6).

## Design

After a successful own Apply, the script side advances an ordinary success counter
(`rn_ip_lui_rev` leader, `rn_ip_pui_rev` party). The native side observes it through a
carrier text in the editor and launcher windows and, on an increase, asks the game once
to refresh the politics view with the existing native request `B6E210(CGameGui*, 12)`.
That only sets the view dirty byte (`view+44`); the normal frame loop (`B68C60` ->
`1236980` -> observer slot 0x48 `15863F0`) rewrites the leader caption (`1588A60`) and
party rows (`1588370`) with native formatting. No caption write, command or rebuild.

## Contract (`caption_refresh_core.hpp`)

- **Seed only at prefill (MAJOR-1).** `Service::on_prefill(window)` is the only source of
  a baseline; it must be called by the integrated bridge's one-shot opening prefill.
  Ticks never seed; launchers never seed; an existing seed is never overwritten.
  `Service::on_dispatch(window)` is the hard Apply gate: unseeded, unparsable carrier,
  value jump, rollback suspicion or cap reached => `false`, Apply must be dropped. A
  failed prefill blocks Apply for the whole opening, even if the carrier becomes
  readable later. Without integration (this shell) no seed exists, so nothing refreshes.
- **Stale text vs rollback (MAJOR-2).** A lower value is treated as stale carrier text:
  pending is kept, no request is planned and Apply is gated while suspected. Only
  3 consecutive lower reads confirm a rollback (logged), which drops baseline and
  pending until the next Open. A rollback shorter than 3 reads is indistinguishable
  from stale text (OPEN: carrier freshness in the first post-Tick is not proven).
- **Load without Bind (MAJOR-3), explicit policy.** Bind => full reset (observed once per
  model per load in run17884/run36204). Confirmed lower counter => reset. Equal/higher
  counter after a hypothetical bindless load is accepted: worst case one extra or one
  missed `B6E210` dirty request, never a write/command/retry. World identity stays OPEN.
- **Fresh chain on every use**, never cached, all reads bounded: own window model VT and
  tokens (`+28 == 3A70`, `+2C == 3AB2`, `+60 == 0`, `+80 == 0`, window `+108 == 0`);
  `window+E0` idler (VT 2968FF0, slot B8 == DCCF30); `[window+F0]+18 == idler` (manager;
  window+F0 == manager is an inference, equality-only); `idler+6B8` gui (exact DCCF30
  semantics; VT 294A688, `gui+180 == idler`); `gui+C8+12*8` view (VT 29D9648, observer
  VT 29D96F0, slots 28/38/48 pinned, `+38 == gui`, `+570 == idler`, `+578 != 0`);
  `window+100 == view+578`. Pointers live only on the stack of one call.
- Visibility needs window `+B79` desired, root `+A5` bit 8 and not bits 4/16.
- One native request per plan; re-entrant Ticks during it cannot plan another. No lock is
  held across it. `native_calls` counts attempts (recorded before the call); `acks` and
  `failures` count only results verified after it (fresh relookup, same gui/view,
  `dirty == 1`, counter re-read). A pre-existing dirty=1 is acked but logged
  `ok-already-dirty` (non-discriminating evidence).
- Failure suspends that revision; guard/authority rejects defer to the next visibility
  edge or a new success. No per-frame retry. Hidden keeps seed/ack/pending.
- Invalid own context resets the field once per valid->invalid transition (logged), not per
  frame. Carrier rejections are logged once per ok->bad transition.
- Counter range 0..999 as plain digits (HOI4 fixed-point vars; `[?var]` formatting of
  >= 1000 unverified). Apply is gated below 999. After 999 successes per field and save,
  refresh stops (fail-safe, OPEN).
- Hook bodies forward to the original exactly once, first, unconditionally (tested order).

## Fingerprints

25 byte runs that start and end on instruction boundaries, each unique in `.text` at its
RVA and free of DIR64 relocations, plus the 3 hook-target fingerprints from the render
bridge, 4 vtable slot pins and 2 token DWORDs. Any mismatch refuses initialization.

## OPEN (enforced as refusal)

- Authority: `authority_proven()` is constant `false`; even an armed build never calls `B6E210`.
- Script side (counters, carriers `rn_ip_leader_rev` / `rn_ip_party_neutrality_rev`,
  fixture copy) does not exist. Integration of `on_prefill`/`on_dispatch` into a reviewed
  render-bridge copy (not the focus bridge) is not done.
- Co-load: refuses if any of 10 known editor/observer/bridge DLLs is already loaded;
  reverse load order is not protected (older DLLs do not know this basename) and must be
  forbidden operationally. Arming only after integration into one DLL.
- Runtime: carrier freshness, dirty consume ordering, real caption/row/tooltip refresh.

## Build, tests, mutation check

`build.ps1 -MinHookDir <path>` -> `plugin/build/caption-refresh-candidate/`: inactive DLL
(`RNInitialize` verifies everything and returns 41; no hooks, no MinHook linked), an armed
compile-only `/c` check, and `caption_refresh_test.exe <hoi4.exe>` (fake-memory suite +
static fingerprint check of the EXE *file*). `/MT /W4 /WX`. Thread tests time out with
FAIL instead of hanging. `mutation_check.ps1` holds the exact mutant definitions (find /
replace on the core) and reruns the suite per mutant with a timeout. Neither script
inspects or touches a game process.
