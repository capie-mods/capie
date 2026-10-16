# Native shared-code integration

The native numeric layout selects all 157 numeric mappings from
`address-inventory.json`. It does not substitute Windows values for the five
adapter entries or two policy replacements. The independent inventory test
checks exact values and rejects those aliases.

Name-layer hooks use SysV output-first faction arguments, owned libc++ strings
and the game's verified allocator/free boundary. Temporary BuildNameKey inputs
are released after the synchronous original returns. Allocation failure forwards
the original tag and invalidates the displayed-name cache so a later refresh can
retry. The faction result transfers ownership to the caller. The fixture covers
both behaviors and keys longer than the ordinary 128-byte name limit.

Shared diagnostics use native monotonic clocks, thread identifiers and locks;
Windows continues through SRW locks and its original APIs. The native log writer
handles partial writes and EINTR. No production log location is selected here.

The native engine-call boundary is inactive by default. Readability is a bounded
kernel-copy probe, not Windows SEH and not a lifetime guarantee. A verified owner
thread and valid object lifetime are required before any game call. The fixture
rejects calls from a different thread, even with readable objects.

## Release hook prefixes

`re/hook-prefix-evidence.json` records 15 complete instruction prefixes from the
exact pinned binary. `core/macos_hook_prefixes.h` is a C-compatible copy for the
minimal bootstrap. Every prefix fits the restricted relocation-free backend.
The constructor's entry at `0x10020b820` is a tail thunk; the proposed patch target
is its body at `0x10020b2b0`, preserving the proven five-argument void interface.
The other entries use their inventory RVAs. This is not authorization to install
patches: the early-init lifecycle, loaded-image identity and rollback gates still
have to pass.

## Checked

The integrated 23-suite baseline passed on arm64 and x86_64 (Rosetta). Subsequent
transport, prefix and foreign-thread tests passed separately; the runner includes
them for the next complete run. Python evidence tests: 24 passed. Targeted native
ASan/UBSan fixtures passed for the new adapters.

No full Windows build was available in this macOS environment. No production
macOS API was activated, no campaign/save/load/language parity test was performed,
and no release was published. See `port-plan.md` for the remaining acceptance gates.
