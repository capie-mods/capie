# Design: SetVariable through its vtable slot (findings B2 / N11)

Status: design only, not implemented. Build: HOI4 1.19.4.0 (`hoi4.exe` SHA-256 `B6650C85…D888`).
Sources: `ghidra-review-2026-10-28.md` (B2), `port-1.19.4.0.md` (Ghidra verification), and a
capstone/pefile re-check with `tools/port/locate.py` on 2026-10-28. The Ghidra session crashed
(Java OutOfMemoryError) before this note was written, so the facts below were re-read with
capstone. They agree with the review, which used Ghidra; nothing here is "pending Ghidra".

## Facts

| Fact | Evidence |
|---|---|
| `CSetVariableEffect` primary vtable is `27F2F88` | RTTI `.?AVCSetVariableEffect@@`, COL offset 0 (`anchors.json`: `vt_set_variable_effect`) |
| slot `+0x68` (`27F2FF0`) holds `image + 0x37D1C0` | `locate.py`: `set_variable_execute` = slot `0x68` |
| no other `.rdata` qword holds `image + 0x37D1C0` | qword scan of `.rdata`: one hit, `27F2FF0`; no other vtable shares the function |
| no direct `call`/`jmp` to `37D1C0` in `.text` | rel32 scan: 0 references (only the slot and `.pdata`) |
| `27F2FF0` is 8-byte aligned | `0x…FF0` |
| the body is 35 instructions: `FindOrCreate(effect+0x58, ctx, 0)`, `Eval(effect+0x128)`, store; an invalid variant calls `3D2FC0` (throws) | capstone of `37D1C0`, review G7 |
| it runs on the logic thread, possibly nested (effects inside effects) | review §1 |

Today `capie_core.dll` hooks `37D1C0` inline with MinHook (`name.set_variable_execute` in
`dll/capie_core.cpp`, `install_all_at_once` in `core/hook_set.hpp`), and the body
`set_variable_hook` runs the name, faction, flag and portrait commits after the original.

## Slot patch versus inline hook

| | Inline (today) | Slot patch |
|---|---|---|
| What changes | first bytes of `37D1C0` (5-byte jmp + trampoline) | one aligned qword in `.rdata` |
| Atomicity | MinHook freezes all threads while it writes | one `InterlockedExchange64`; a thread either calls the old or the new pointer |
| Coverage | every caller, direct or virtual | every virtual call through this vtable; equal today (no direct caller) |
| Byte signature needed | yes (`country.set_variable_execute` pin) | no: the slot value itself is the check |
| Breaks when | the prologue changes (every rebuild) | the slot index changes or the function gets a direct caller |
| Original call | trampoline | the saved slot value (the real function, no trampoline) |
| Interaction with other mods / tools hooking `37D1C0` inline | chained by MinHook | still works: CAPIE calls the saved pointer, which runs their detour |

Decision: patch the slot. It removes one byte pin and one MinHook trampoline, and the
condition it relies on ("no direct caller, no second vtable") is checked at every start, not
assumed. Keep the inline hook as the fallback only if the start check fails (see below); do not
install both.

## RTTI and start checks (all before any write; any failure = layer off, logged)

1. `core::vtable_is_class(mem, base, {"set_variable_vtable", rva::vt_set_variable_effect,
   ".?AVCSetVariableEffect@@", 0})` (`core/rtti.hpp`); add the entry to `core_vtable_classes`.
   New `game_layout.hpp` constants: `vt_set_variable_effect = 0x27F2F88`,
   `off::slot_set_variable_execute = 0x68`, both with an `anchors.json` entry (the vtable entry
   exists already as a helper; flip `layout` to true).
2. The slot holds exactly `base + rva::set_variable_execute`. If it holds anything else (another
   tool patched it, or the layout is wrong) refuse; never chain onto an unknown pointer.
3. The target bytes still match the existing `country.set_variable_execute` signature (the pin
   stays in the table as identity of the function, but it is no longer hooked inline).
4. The slot address lies in `.rdata` and is 8-byte aligned (`VirtualQuery`: committed,
   `PAGE_READONLY`).

## Install

```text
saved = base + rva::set_variable_execute      // value checked in step 2
VirtualProtect(slot, 8, PAGE_READWRITE, &old)  // one page; other vtables share it, nothing else changes
prev = InterlockedCompareExchange64(slot, &set_variable_slot_hook, saved)
VirtualProtect(slot, 8, old, &ignored)
FlushInstructionCache is not needed (data, not code)
if prev != saved: refuse (someone changed it between the check and the write); slot untouched
```

- `name_layer.originals().set_variable = saved` must be published (release store) **before**
  the exchange, because the hook can run on another thread immediately after it.
- Order inside `capie_core.dll` init: pins and RTTI checks, then MinHook for the other hooks
  (`install_all_at_once`), then the slot patch last. A failed slot patch leaves the inline hooks
  working and turns only the layers that depend on the commit off (name, faction, flag,
  portrait commits; the per-frame localisation hooks stay on). Same `InitCode` style: a new code
  for "slot check failed" next to 30/31/32.
- The hook keeps the calling convention `void(effect*, context*)` and the existing body
  (`set_variable_hook`); it calls `saved` exactly once and never under a lock.

## Uninstall

CAPIE has no unload today (`hook_set.hpp`: no unload or retry; the module is pinned). The
slot patch keeps that rule. For the failure paths of a later init step:

```text
InterlockedCompareExchange64(slot, saved, &set_variable_slot_hook)   // only if it is still ours
```

with the same `VirtualProtect` bracket. If it is not ours any more, leave it: someone chained
onto CAPIE and restoring would cut them off. The module stays pinned in both cases, because a
game thread may be inside the hook while the slot is written back.

## Tests

Offline (`plugin/capie/tests`, no game):

- `set_variable_slot_test.cpp`: a fake `.rdata` page with a fake COL/type descriptor
  (`.?AVCSetVariableEffect@@`) and a slot holding a fake function; check refusal on a wrong
  class name, a wrong offset, a foreign slot value, an unaligned or writable slot, and that a
  successful install publishes the original before the exchange (call through the slot from a
  second thread in a loop while installing: every call reaches the fake original exactly once).
- Restore: only when the slot still holds the hook; a foreign value is left alone.
- Mutation check entries for the three refusals (`mutants.json`).

Against the real exe (`*_exe_test`, like `rtti_exe_test.cpp`):

- the slot of `vt_set_variable_effect` holds `image + rva::set_variable_execute`;
- no other qword in `.rdata` and no rel32 in `.text` references that function (the condition
  the design relies on; on a new build this test is the first to say "slot patch not safe");
- `port.py locate --check` (already covers `set_variable_execute` through the slot).

In game (`in-game-test-record.md`): rename a country, a faction, a flag and a portrait in one
session (all four commits pass through the hook), save and load, and a multiplayer session
with the host and a client both running CAPIE.

## Open

- Whether any other build of the game (Linux/macOS) has a direct caller is irrelevant here (the
  DLL is Windows only), but a future Windows build could inline `Execute` into a caller; the
  exe test above catches it.
- `3D2FC0` throws through the hook on an invalid variant (review G7). Unchanged by this design;
  the hook entry must not be `noexcept` once G7's change lands.
