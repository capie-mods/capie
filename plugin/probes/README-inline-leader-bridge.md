# Isolated inline leader transport prototype

This is an **unbuilt, unactivated QA source candidate**. It combines the reviewed
inline prefill and command bridge in a new DLL/output. It does not replace the
country DLL or any earlier probe. Root owns integration review, build, fixture
configuration and fresh-process activation.

The four detours are B88950 (post native tick), 22F1030 (forward native key),
B867D0 (ordinary scripted-GUI button dispatch), and 1144840 (command constructor).
The exact EXE hash and seven unique entry fingerprints must match before any
hook is enabled. Initialization rejects loaded observer, editor bridge or
inline prefill DLLs and pins itself. Never load these modules in the reverse
order either. An initialization failure requires a fresh process.

Prefill retains the current split-reason implementation for all three fields:

| Model | Native field | Carrier |
| --- | --- | --- |
| rn_ip_leader_gui | rn_ip_leader_edit | rn_ip_leader_prefill |
| rn_ip_party_neutrality_gui | rn_ip_party_neutrality_edit | rn_ip_party_neutrality_prefill |
| rn_ip_party_neutrality_gui | rn_ip_party_neutrality_long_edit | rn_ip_party_neutrality_long_prefill |

Epoch carriers remain rn_ip_leader_epoch and rn_ip_party_neutrality_epoch with
plain positive decimal `[?ROOT.rn_ip_epoch|0]`. All fields are freshly resolved;
prefill happens once per opening/epoch and verifies native UTF32 readback.
Subsequent refreshes preserve drafts. Partial/rejected prefill requires reopen.
Focus is manual. No auto-focus or Escape behavior is added.

Enter **only copies a diagnostic** for later tick validation. Every key forwards
exactly once, preserving the native return value. Enter never calls dispatch,
constructs a command, enables Apply or closes a widget. Its later diagnostic
still requires the freshly resolved active receiver and unchanged text.

The only transport origin is the ordinary already-enabled native button path:

- model rn_ip_leader_gui;
- selector rn_ip_leader_apply_click;
- field rn_ip_leader_edit.

Party and country have no payload transport. The leader button need not retain
keyboard focus after being clicked. It must have a fresh valid visible View,
matching window/context backreferences, a completed prefill, current epoch and
unchanged lifecycle generation. The bridge copies bounded text and epoch during
that preflight, then rechecks View/epoch/generation and text immediately before
calling the original dispatch. Any recognized own-action preflight failure drops
that action before native dispatch. Unrelated actions forward unchanged.

The source temp-vector is bounded to 256 original records. Only these **35 exact
keys** are sanitized, including every duplicate; other keys/order/values remain:
rn_native_ok, rn_native_len, rn_native_0 through rn_native_31, rn_ip_cmd_epoch.
There is no prefix-based namespace removal. The last key is exactly 15 ASCII
bytes and fits native SSO. Every newly appended logical integer is checked
before signed fixed-point multiplication by 100000. Epoch must be positive and
at most 92233720368547. No epoch string is sent in the command.

The unchanged strict converter accepts at most 32 CHARS84 characters. Invalid
input within the 128-scalar snapshot bound produces ok0, len0 and all 32 zero
codes; no accepted prefix survives. Larger/unreadable native snapshots are
guard failures and drop the own action. Epoch is still carried for invalid-text
diagnostics. Input is never trimmed, truncated, normalized or replaced.

Stack-bound POD records borrow unrelated native string storage only during the
synchronous dispatch. A scoped TLS origin matches exact model-name, selector,
scope and source-vector addresses and exact names at the constructor. The
native constructor deep-copies the const view using its own allocator. TLS is
restored before dispatch returns; lifecycle/Enter state stores no engine
pointer. No plugin allocator crosses ownership boundaries.

A constructor origin mismatch forwards the original arguments. Once a native
constructor starts, its result returns unchanged: no cancellation, retry or
second command is attempted even if post-copy diagnostics fail. The expected
single ctor call and independent record storage are logged.

**Use only a disposable log-only fixture prepared by root.** Existing leader
Apply stays disabled; this DLL does not change enabled triggers or target/name
variables. Do not enable the existing rename helper for this diagnostic run.
The normal command can execute the effect bound by its fixture, so log-only
effects are an activation requirement. Typed target ownership and execution-time
epoch checks must be independently accepted before any real rename stage.

After review, build-inline-leader-bridge.ps1 -MinHookDir <source> produces only:

- plugin/build/inline-leader-bridge/rename_anything_inline_leader_bridge.dll;
- plugin/build/inline-leader-bridge/inject_inline_leader_bridge.exe;
- LICENSE.MinHook.txt in the same output directory.

The script refuses to overwrite its loaded target. The injector uses the normal
exact-image, duplicate-module and fixed-name guards through RN_EXPECTED_DLL_NAME.
Explicit initialization writes inline_leader_bridge.log beside this DLL:
RN_INLINE lines retain prefill/Enter diagnostics; RN_ILBR lines describe transport,
35-record preparation, epoch/raw epoch, ctor count/storage or dropped actions.

No compiler, injector, game UI or runtime test was run by the source author.
Source/PE checks do not establish runtime acceptance. Root/QA must first prove
the prior bridge, then prefill/typing/draft/reopen and leader log-only payload
length/codes/epoch, invalid full-zero payload, unrelated-record preservation and
stale-opening rejection. Multiplayer and two-field transport remain deferred.

Root built the candidate on 2026-10-11 with own C++ `/W4 /WX`, no warnings.
DLL SHA256 `9EB47336166074323C71459180D907E087B4EE978B6212A926E91403B120D329`;
injector `4655389573E00167D779F1C71601C5F247FEFCF7750EF123071EE0330A2C3B1A`.
This build has not yet been injected. Metadata and independent source review
are retained in `docs/evidence/session-20261011/day2-inline-leader-bridge-*`.
