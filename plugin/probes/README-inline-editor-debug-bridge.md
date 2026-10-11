# Development diagnostics bridge

Separate candidate based on the reviewed key logger v2. It is not activated in
the game and does not replace any accepted DLL. Exact HOI4 1.19.3.0 executable
SHA-256, eleven unique fingerprints, seven hooks, fresh process and one editor
bridge variant only. Frozen older DLLs do not know this basename, so co-loading
is forbidden in both orders, regardless of their asymmetric guards.

Build `build-inline-editor-debug-bridge.ps1 -MinHookDir <path>` into the separate
`plugin/build/inline-editor-debug-bridge` output. Own C++ uses `/MT /W4 /WX`.
The existing transport, prefill, render refresh, manual Apply and Enter diagnostic
policy are retained. This candidate adds no autofocus or automatic Apply.

The log `inline_editor_debug_bridge.log` now includes:

- A sequence, monotonic milliseconds, current thread and truncation flag on
  every captured line. These identify observation order, not simulation time.
- Recognized Open, Cancel, Apply and QA-guards selectors from exactly seven own
  scripted GUI models, ten model/selector pairs. An invalid fresh context is
  reported; this observation does not authorize or block an action.
- Existing explicit prefill, epoch, readiness, codec, capability and command
  guard reasons. Wrapper/TB events retain separate meaningful32/empty2/readfailed2
  file budgets. Empty polls are not labeled failed keys.
- A copied rolling history of the latest 256 records. Meaningful raw events and
  semantic status transitions continue entering history after their file quota.
  Empty/readfailed polls stop at their separate file quotas. Overwrite count and
  any truncation are explicit; this is a bounded history, not a complete replay.
- A distinct pre-Execute observation of `CExecuteScriptedWindowEffect`, exact
  vtable2996ED0 / RVA1156950. Only the three own Apply model/selector pairs are
  recorded. Copied header values remain native fixed-point integers. A void
  return is labeled `effect_success=unverified`. No command pointer is retained
  or dereferenced after original Execute, which always runs exactly once.

`RNDebugRequest(void*)` accepts fixed scalar operations: 1 snapshot, 2 dump,
3 pause history, 4 resume history. It returns 0 **queued**, 41 inactive,
42 invalid operation, or 43 serial exhausted. It reads no game object and performs
no file I/O, focus, setter, dispatch or engine call from the remote thread.
The companion controller only calls this already-loaded export; see
`README-inline-debug-control.md`.

Work runs after original scripted-window Tick. A snapshot requests all three
editor models independently; hidden/invalid models report a reason without text
lookup. Visible models are checked before and after copying only their named
fields, up to 128 UTF32 codepoints per field. Logs include Unicode codepoints,
strict codec result, visibility, focus-manager relations and opening lifecycle.
Snapshot calls no setter, focus or Apply. A changed View, epoch, lifecycle or
text suppresses the copied text rather than publishing a coherent snapshot.

Requests coalesce into the latest serial and model bitset. Each bit is consumed
once, then history is dumped. After ten seconds, the next UI service point
reports remaining model bits as expired and dumps history. Without a Tick,
requests remain queued; controller success alone never proves completion.
Pause cancels pending model bits and requests a final copied dump. Already-taken
UI work can finish. It stops history capture only: baseline file diagnostics
continue with `trace_paused=1`, and explicit snapshots can still write their
observations to the file. Resume captures subsequent history; it does not replay
missed records or synthesize snapshots.

History contains only copied scalars and bounded text from our own fields or
fixed whitelisted actions. File I/O happens outside history/lifecycle locks.
No locks cross original native callbacks or native widget lookup. Debug dump
does not recurse into history capture. No engine address is cached for later use.

Validation is offline: inherited actual builder/key-forwarding tests plus own
snapshot/action/Execute tests, including a command made PAGE_NOACCESS by the
fake native before returning, and ring/queue wrap, TTL and concurrency tests.
Wrong-host initialization is tested only in the disposable test executable.
Actual HOI4 parsing, live Execute observation, request servicing, Unicode input,
focus, Enter, multiplayer and release integration remain separate runtime work.
Fresh-root/epoch checks do not establish world identity across every save load.
Log write failure can lose evidence and must never be interpreted as absence of
an event or successful effect. No game-install file is changed by this candidate.
