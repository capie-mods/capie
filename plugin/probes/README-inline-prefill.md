# Isolated inline prefill prototype

This is a **built, unactivated QA prototype**, separate from the country DLL,
editor observer and command bridge. The prior static proof is
[day2-native-inline-lifecycle-contract.txt](../../docs/evidence/session-20261011/day2-native-inline-lifecycle-contract.txt).

It supports only the unloaded probes/rn_in_place_ui manifest:

| Model | Native field | Prefill carrier |
| --- | --- | --- |
| rn_ip_leader_gui | rn_ip_leader_edit | rn_ip_leader_prefill |
| rn_ip_party_neutrality_gui | rn_ip_party_neutrality_edit | rn_ip_party_neutrality_prefill |
| rn_ip_party_neutrality_gui | rn_ip_party_neutrality_long_edit | rn_ip_party_neutrality_long_prefill |

Epoch carriers are rn_ip_leader_epoch and rn_ip_party_neutrality_epoch.
They must render the plain positive decimal ROOT.rn_ip_epoch; the fixture uses
`[?ROOT.rn_ip_epoch|0]`, whose exact runtime rendering remains to be checked. Open/Cancel or
target changes must advance that script epoch. Only these exact model/control
names are accepted. Carrier expressions and target pins belong to the fixture,
not this DLL.

The prototype detours only:

- B88950: forwards native ScriptedWindow tick, then validates the current
  model, context, root, desired visibility, actual visibility and epoch.
- 22F1030: forwards every native key exactly once, preserving the full
  original return value. A recognized Enter copies bounded text for a later
  diagnostic. No callback or native observer object is attached.

On each opening/epoch it resolves all fields and hidden instant-text carriers
fresh, copies valid UTF8, calls native SetText once, and verifies the resulting
UTF32 text. It prepares both party fields before any write. A partial/rejected
attempt requires Cancel/reopen; it never retries repeatedly over a draft.
Unreadable epoch data suspends that opening and invalidates pending diagnostics.
Subsequent GUI refreshes preserve the draft.

Prefill rejects malformed/empty/oversized Unicode, controls and unresolved
bracket/render expressions. It never trims, replaces or truncates a name.
Existing names up to 128 Unicode scalars can be shown; the strict 32-character
CHARS84 converter is logged independently and never applied here.

Native focus remains **click-to-focus**. Prefill refuses a registered active
receiver, including a failure to read its state. Auto-focus and Escape are
unimplemented. The fixture's ordinary Cancel button remains available.

Enter diagnostics contain no engine pointer. They carry only field index,
opening generation, epoch, creation time, producer thread and copied UTF32.
A fresh later tick consumes them once, verifies the epoch/generation, current
receiver and unchanged text, and logs either acceptance or rejection. A queued
diagnostic expires after two seconds. SRWLOCK protects copied data across threads.
This observes an Enter **key**, without claiming a native NotifyEnter callback
was executed. The active receiver is checked before native key handling; a
post-call copy is still allowed if native handling then releases focus, but
later consumption rejects it unless the freshly resolved field is active.
No widget is closed inside a key callback.

**There is no command dispatch, constructor hook, payload injection or Apply.**
Strict invalid input is logged and cannot rename anything. Fixture Apply stays
disabled until the separate command transport, typed target pin, ownership and
execution-time epoch checks have been reviewed and tested. Party two-field
payload transport is a later step.

The DLL accepts only the pinned EXE SHA256 and five unique raw entry fingerprints:
tick45B, key42B, edit lookup43B, instant lookup43B and SetText45B. The SetText
fingerprint covers its complete 15-byte thunk, alignment byte and the start of
the immediately adjacent UTF8 converter. Exact type tables and control-name
layout are checked again before field access. Every rejected native guard
forwards unrelated native behavior unchanged.

Build only after root integration review, using the separate
build-inline-prefill.ps1 -MinHookDir <source> script. It produces:

- plugin/build/inline-prefill/rename_anything_inline_prefill.dll
- plugin/build/inline-prefill/inject_inline_prefill.exe
- inline_prefill.log beside the DLL after explicit initialization

The script refuses to overwrite its DLL if it is already loaded. It does not
build or replace any other plugin output. Its injector uses the existing
exact-image and duplicate guards with a fixed DLL-name whitelist.

Use a **fresh game process**. Never combine this candidate with the observer or
bridge DLL in the same process. Initialization rejects either already loaded
module. The inverse load order is also prohibited operationally. The country
rename DLL uses other hooks and remains separately owned by root.

A successful compile is not runtime acceptance. Root/QA must first verify
correct prefill in all three fields, ordinary typing, draft preservation during
refresh, Cancel/reopen, field separation and the two Enter diagnostic records.
Initialization/error/length/codepoint diagnostics are written beside this DLL.
No MP, native auto-focus, Escape or actual rename acceptance is claimed.
