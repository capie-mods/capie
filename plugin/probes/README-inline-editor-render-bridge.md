# Isolated opening-only render refresh candidate

Source only; no build, injection or HOI4 acceptance is claimed by this change.
The frozen predecessor is `inline_editor_bridge.cpp` (SHA256 F2EED845E7F33EF21CAC1D0C84C369A1DCF788A5FAFA648D255694921DC21E65).
Its build/output and active process are not modified.

The new `inline_editor_render_bridge.cpp` adds one native `22F4D80(CEditBox*)`
refresh after an opening-only TB SetText and readback. Immediately before that
call, it resolves a new visible View, verifies unchanged root/model, epoch and
lifecycle generation, resolves the exact edit again, and checks type/name,
visibility, inactive focus and copied text. A guarded fault or rejected check
leaves the opening not ready, with no automatic retry. The native refresh runs
without a lifecycle lock. After it returns, a second fresh View, epoch,
generation, edit identity, visibility and inactive-focus check precedes the
second TB readback and acceptance log. A returned refresh is not proof of rendered pixels.

This follows the native state-name, typed-text and key paths. The exact EXE hash
and unique 55-byte entry fingerprint gate the call. There are nine unique
fingerprints and the same five detours. No direct dirty-flag writes, Enter
dispatch, focus request, new lifecycle policy or script changes are added.
All 35/69/70-record transport, country capability, native constructor ownership
and existing failure boundaries remain copied from the predecessor.

`build-inline-editor-render-bridge.ps1` creates only
`plugin/build/inline-editor-render-bridge`. It uses /W4 /WX for this project's
C++ and an exact copy of the transport test with only its source include changed.
The DLL basename intentionally remains `rename_anything_inline_editor_bridge.dll`.
The separate injector is `inject_inline_editor_render_bridge.exe`; its shared
injector checks every loaded module by basename before loading. RNInitialize
additionally rejects a different loaded HMODULE with this basename. Never load
both output paths in one process, and never replace/reload a loaded DLL. Root
must review/build and use a fresh game process before activation.

First QA must use a reviewed log-only fixture: opening alone renders the name
before typing; no repeated SetText/refresh after draft typing or ordinary ticks;
Cancel/reopen refreshes once; both party/country fields are distinct; old
transport checks still pass. Menu/world-load without rebind remains unresolved.
The existing 64px-border background on a 36px leader root is a separate GUI
hypothesis and is not changed here. Multiplayer remains deferred.

Native evidence and limits: `docs/evidence/session-20261011/day2-inline-editor-render-refresh-contract.txt`
and `day2-inline-editor-render-refresh-raw.txt`. No visual result is promised by
the offline source/byte checks.
