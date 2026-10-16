# Explicit native editor composition

`src/dll/macos_editor.hpp` composes the shared release editor pipeline. It performs
no byte patching, exports no autonomous startup constructor, does not launch the
game and leaves both composition and native-call admission off by default.

`Configuration` accepts the verified image, owning UI thread, four original
trampolines, six native callees, optional verified country-capability callback and
caption/focus-specific verification flags. Configuration must occur while hook
entry points are quiescent. Missing bindings, an active composition or an already
open native-call gate refuse configuration. Configuration alone does not activate
anything. `activate(true)` is an explicit bootstrap attestation that object-lifetime
and native-call fault policy were verified; the boolean is not such verification
by itself. No production bootstrap currently supplies that attestation.

## Shared behavior and lexical admission

Tick calls its original exactly once, then checks activation, UI-thread identity,
reentrancy and a fresh model prefilter. Only its release observer scope grants
native-call admission. It invokes the existing private prefill/lifecycle, public
editor, caption and focus observers, then closes admission. Nested Tick calls run
the original but do not recursively observe. Other threads forward without observers.

Bind calls the original exactly once, then invokes the shared reset observers on
the owning thread. It does not grant admission. The release Bind reset and Dispatch
orchestration were extracted into `editor/release_pipeline.hpp`; the Windows wrappers
now delegate to them. After removing comments/whitespace, platform string alias names
and explicit namespace qualifications, both extracted bodies compare identically to
the original HEAD bodies. Windows debug pre-dispatch logging stays in its wrapper.
This source comparison is not a substitute for Windows build/test execution.

Dispatch first requires the same owning UI thread, a fresh recognized model, exact
embedded context identity and a visible valid window/context/root chain before it
grants lookup admission. It then uses the same release Apply pipeline as Windows:
caption gate, fresh epoch/readiness, input snapshots, record preparation, second
fresh view/epoch/text checks, scoped Origin and exactly one original dispatch. An
invalid own window retains the existing refusal/sentinel decisions; no permissive
fallback bypass was added. Admission closes when Dispatch returns.

Every forwarded original Tick, Bind, Dispatch and constructor temporarily suspends
plugin-initiated native-call admission. A nested scope restores an existing same-thread
grant; owning scopes close their grant with compare/exchange. Thus the constructor
inside original Dispatch still uses shared identity/payload checks, without granting
arbitrary native calls to the original. The native constructor returns void and is
inspected through self, as established in records-transport.md.

## Dispatch synchronicity and limits

For pinned SHA256 `09419b08...b1fb`, the native click caller constructs the selector
on its stack, appends `_click`, then at `0x100b480f0/0xf4` sets RSI to that stack
selector and RDI to the GUI context before directly calling `0x100b4b7e0` at
`0x100b480f7`. It destroys the selector only after return at `0x100b48102/106`.
Dispatch preserves context and selector (`0x100b4b7f1/7f5`), derives the scope from
context+0x3c0 and synchronously constructs the command at `0x100b4bae5`. The caller
uses its saved command pointer, then submits it at `0x100b4baf2`. This proves a
synchronous constructor/selector boundary, not a deferred selector lifetime.

Fresh bytes:

- `0x100b480f0`: `488d75a04889dfe8e4360000`
- `0x100b48102`: `488b7db0e8057bf301`
- `0x100b4b7f1`: `488975c04889fb4c8db7c0030000`
- `0x100b4bae5`: `e836fd6bff4c89e74c89ee31d2e8a99cb801`

Before original Dispatch, shared capture/recheck performs lookups and copied reads
on that same verified UI thread. Each lookup uses its current root and native
three-argument lookup ABI. The composition never retains those engine pointers
beyond the hook call. These static facts and fresh guards do **not** establish
universal protection against reentrant destruction, cross-thread engine mutation,
native allocation failure or a fault inside a game callback. macOS admission is
not Windows SEH. Those remain explicit prerequisites for production activation.

## Offline verification

`macos_editor_composition_test.cpp` started RED with the absent composition header.
It now passes **37 checks**, native arm64 with ASan/UBSan and x86_64 under Rosetta.
Owned native objects and fake callbacks exercise inactive exact forwarding,
configuration refusal, explicit activation, post-original observer admission,
nested Tick suspension/restoration, other-thread forwarding, missing epoch keeping
lifecycle unready, and deactivation. A complete synthetic own Apply passes the
shared snapshots/readiness/transport pipeline, forwards once, constructs once and
carries `rn_native_ok=100000`. Hidden-window Apply refuses before lookup admission.
All scopes finish with the native-call gate closed.

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_editor_composition_test.cpp -o /tmp/capie-composition-asan
/tmp/capie-composition-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_editor_composition_test.cpp -o /tmp/capie-composition-intel
/tmp/capie-composition-intel
```

The fixture's ready lifecycle is explicitly seeded synthetic state; it does not
prove a real opening/prefill cycle. No real game hook, GUI Apply, caption request,
focus release or lifecycle test was performed by this change.
