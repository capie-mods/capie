# Establishing the native GUI owner

The first recognized Tick is **not** independently a verified GUI owner. The
pinned binary establishes the following narrower facts (SHA256
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`):

- `0x100b43f00` is `CScriptedWindowManager` vtable slot 8. RTTI is at
  `0x1033269c8`, address point `0x1033268e0`, slot pointer `0x103326920`.
  The function uses the profiling label `scripted_gui.window`.
- Its loop reads manager+`0x20` (window pointers), captures count at +`0x2c`,
  passes the current pointer in RDI and a derived force byte in ESI, and calls
  Tick `0x100b44020` at `0x100b43f81`. The full saved disassembly contains one
  direct call to Tick and no pointer reference to Tick in the binary pointer
  scan. This is not a proof that computed indirect references cannot exist.
- After the loop, `0x100b43f91` calls manager work `0x100b44480`. That function
  traverses a separate manager+`0x50` array/count+`0x5c`; its complete semantics
  are not established here. It must not be called a proven deferred deletion queue.
- Tick synchronously updates its embedded context (window+`0x110`) at
  `0x100b443d5`. Click `0x100b480f7` and right-click `0x100b481ab` call
  Dispatch `0x100b4b7e0` directly with stack-owned selectors, destroyed after
  return. Thus the observed Dispatch argument lifetime spans the original call.

Fresh byte evidence is in [editor-owner-evidence.json](re/editor-owner-evidence.json).
The caller's collection owns/references the window during iteration, but the
shown loop does not increment a reference count across Tick. A readable address
alone cannot prove that its identity survived an original call or nested GUI work.
The remaining evidence needed is a bounded real execution of this path, not a
universal proof about every engine thread and allocation.

## Forward-only recorder

`src/core/macos_gui_probe.hpp` is an optional, disconnected helper. It installs
nothing, does not modify admission, and does not call lookup, setter, focus,
caption or any game function except the supplied original exactly once. It records
only thread ID/main-thread flag and the window/model/root/context identities and
vtable values before and after that call. Selector/text/auth data is never captured.
Read failures leave an incomplete observation and **never** suppress forwarding.
The memory policy must be the kernel-copy policy in a real probe.

A future explicitly selected probe bridge can supply:

```cpp
core::macos::gui_probe::Thread thread{
    core::platform::thread_id(), pthread_main_np() != 0};
recorder.tick(memory, reinterpret_cast<std::uintptr_t>(window), force,
              thread, original_tick);
recorder.dispatch(memory, reinterpret_cast<std::uintptr_t>(context), selector,
                  thread, original_dispatch);
```

Use separate wrappers for those two examples, not both for one hook. Keep recorder
storage alive for the process lifetime. Export/snapshot its immutable records through
`read(index, event)`; it does not write files. Producers reserve distinct slots and
publish with release/acquire. Pair enter/leave by `event.call`; nested and concurrent
calls need not occupy adjacent slots. The default 1024 events never wrap: saturation
increments a dropped-event counter while forwarding continues. Start a bounded
observation near the relevant UI action or use a larger fixed capacity. No reset
may race a live hook. A missing leave event can also mean an exception propagated.

## Concrete activation evidence

1. In forward-only mode, capture opening a CAPIE window, multiple visible Tick
   passes, one click/Apply Dispatch, closing, and reopening it. Do not enable editor
   writes during this observation. Check the recorder did not saturate for this span.
2. Require relevant Tick and Dispatch observations to agree on one OS thread ID;
   record `pthread_main_np()` independently. If they are main-thread events, use
   that explicit check in the owner policy. If not, investigate the actual thread
   instead of replacing its identity with the initializer's or first Tick's ID.
3. Check each relevant Dispatch has its context equal to window+`0x110`, its
   context back-pointer equal to the window, and the expected model vtable.
   Check post-Tick identity afresh. A changed/null root/model is an ordinary
   lifecycle transition: skip observers for that pass and reacquire a valid chain.
4. Combine these observations with existing synchronous caller evidence and shared
   fresh identity/epoch/text rechecks. Then exercise the scoped observer path in a
   controlled in-game test, including close/reopen and Apply. Record the tested
   build/scenarios and remaining limitation: an unexpected native callback fault
   is not recoverable by macOS admission, which is not Windows SEH.

This replaces the first-Tick assumption with an observed owner, a concrete caller
boundary and checked object identity. It does not require claiming that all engine
objects are immortal or that a sample proves absence of every future race. The
first forward-only observation alone cannot certify native setter/focus behavior;
that requires the separately recorded scoped observer test.

## Offline checks

`macos_gui_probe_test.cpp` checks exact argument forwarding, readable identity,
changed post-call root, unreadable input, bounded saturation, concurrent producers,
nested call pairing and unchanged exception propagation. This helper is not wired
into `macos_runtime.cpp` or the production bootstrap.
