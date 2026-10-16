# Concrete first-bootstrap closure audit

Date: 2026-10-16. Host: macOS 27.0.1 (26A434), dyld program version 27062.
No HOI4 process was launched or patched. Exact binary evidence is recorded in
`closure-evidence.json`, including file/slice SHA-256 and dependency commands.

## Decision

**The first-bootstrap design is viable for the known pair and known Steam overlay;
the closure gate now covers the three bundled dependencies and rejects unknown
non-system images.** The remaining work is specific: generate and review the audited
record for the final built runtime/bootstrap pair. This is not
a requirement to prove arbitrary Apple platform code or all possible executors safe.
Apple's system loader/runtime and the standard Rosetta execution contract are trusted
platform prerequisites. A compromised/modified OS is outside this port's threat model.

The causal argument for this exact profile is:

1. Bootstrap depends only on libSystem and has one initializer. It has no dependency
   path that initializes the late CAPIE runtime, Steam overlay, PDXSDK, TBB or HOI4 first.
   The runtime itself depends only on libc++ and libSystem. The three bundled game
   libraries depend only on Apple libraries/frameworks, not on bootstrap or the main.
2. The published dyld initializer order runs libSystem/below-libSystem first, then
   inserted images recursively, then main. Known main/bundled/runtime images have no
   interpose section, export resolver, nonlazy ObjC list, LC_ROUTINES or upward load.
   Their ordinary constructors are therefore later, not unexplained early game callers.
3. Steam overlay has 15 interpositions, all graphics/cursor/CFRunLoop functions. None
   matches the minimal bootstrap's imported functions. Its ordinary constructors and
   framework dependencies remain later in the first-inserted sequence. The libSystem
   initializer uses its own subsystem initializers, not those graphical/run-loop entry
   points. Thus the existence of interpose metadata is not itself a veto on this overlay.
4. Before those later constructors, no audited non-system code has supplied a game
   callback or run game initialization. Extra Rosetta/system workers are not thereby
   game executors. This causal exclusion applies to whole game code pages, not only
   hook entries, and avoids reliance on thread-count snapshots or unavailable guest RIP.

This reasoning assumes ordinary Apple initialization semantics. It is a **source- and
artifact-supported inference**, not a measurement of every thread in the real game.
The exact dyld27062 binary has not been matched to the older published OSS commit.
Existing owned tests establish local patch/cache visibility under this Rosetta host;
they do not establish gameplay ABI or all future OS behavior. An OS update requires
review of this platform assumption and the owned lifecycle/cache regression tests.

## Actual binary observations

| Image | Ordinary initializer entries | Other early application-code paths |
|---|---:|---|
| HOI4, exact recorded SHA | 5721 | No export resolvers, interpose, nonlazy ObjC or upward/routines commands |
| bundled libsteam_api | 0 | Same absence |
| bundled libPDXSDK | 9 | Same absence |
| bundled libtbb | 5 | Same absence |
| tested CAPIE runtime | 11 offset entries | Same absence; depends on libc++ and libSystem |
| bootstrap enabled-refusal build | 1 offset entry | Same absence; depends only on libSystem |
| Steam gameoverlayrenderer x86 slice | 14 | 15 interpose tuples; no export resolvers/nonlazy ObjC/upward/routines |

The runtime/bootstrap hashes identify the **examined artifacts**, not permanent release
identities: regenerate evidence after rebuilding. Steam overlay whole-file SHA is
`c8a2b06c1daf1bc03dc1d521c967eb95e93a46660aa320979288e42df3b4db10`.
Its `__DATA,__interpose` is VA `0x29788`, size `0xf0`, with bind slots from `0x29790`
through `0x29870`. Bindings are preserved individually in the JSON evidence. Names
cover CGLFlushDrawable/glSwapAPPLE, cursor functions, CGLChoosePixelFormat and two
CFRunLoop entry points. No guessed resolver names were derived from C++ symbols:
the actual export-trie terminal flag `0x10` was decoded and absent in all seven files.

The three previously unpinned bundled identities are:

- libsteam_api: `dd19abd471149617d5b85e390d707806f689b96ee67e014f32769b4cf18e2a1c`
- libPDXSDK: `63b84999312aa029e9aeb3f8288d09b2297b78c3c2bb6822a1e5541dbe046f31`
- libtbb: `65a10dc5463a0fa78519785634abe0c06f83ddaa528b8bf95a103577a3c8804a`

A late hash check cannot undo arbitrary code already executed by a malicious added
resolver/interposer. The meaningful model is that the builder/launcher authorizes an
unchanged known installation before launch, then bootstrap rechecks loaded identities
before its own writes. It must not advertise containment of hostile preloaded code.
This is the same practical boundary as trusting the executable being launched.

## Concrete legacy-index bug found and fixed

HOI4's LC_BUILD_VERSION minimum is **10.14**, whereas our original fixture used the
current SDK minimum. dyld's x86 compatibility permutation exposes the main as index
zero for applications targeting before macOS 12, even though inserted initializers
still run first internally. A new read-only owned fixture with minOS10.14 confirmed
main index0, inserted index1, libSystem index2 on this host.

The previous bootstrap self==image0 gate therefore incorrectly refused the exact
game. The fix skips **only the unique MH_EXECUTE** when matching the first N inserted
images in order. It never skips an unknown dylib. Owned 15-hook regression:
RED exit20 before fix; all legacy and modern scenarios GREEN afterward. See
`bootstrap-gates-legacy-results.json` and `bootstrap-gates-results.json`.

## Minimal remaining implementation and validation

1. Implemented: bootstrap pins the three local bundled libraries plus selected
   overlay/runtime and rejects unexpected non-system images. Universal carriers are
   hashed whole, then bounded x86 slices compared to loaded headers. The builder must
   supply these exact identities; no allowlist exists for arbitrary `/usr/local` libraries.
2. Generate READY only for that reviewed closure and the accepted Apple platform
   premise. Unknown overlay versions, startup libraries and environment overrides
   refuse before CAPIE patching. Do not demand absence of normal Rosetta threads.
3. Perform the authorized actual-game first-inserted **read-only** identity/order
   capture before the first game-hook launch: expected image set, bootstrap index,
   main SHA/header, profile checks. This catches integration differences and does not
   pretend that a snapshot itself proves quiescence.

No additional suspend/guest-PC experiment is indicated by this audit. Neither a full
5721-constructor call graph nor analysis of all later AppKit/Steam worker callbacks
is required when those constructors have not yet run.

## Primary sources and scope

[Apple dyld runAllInitializersForMain and normalizeImageIndex](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/DyldAPIs.cpp)
provides the initializer order and the legacy permutation. The installed binary reports
27062; exact source parity is unverified.

[Apple libSystem initializer](https://github.com/apple-oss-distributions/Libsystem/blob/main/init.c)
shows subsystem initialization and internal helper tables. This supports trusting the
normal system startup boundary; it is not an exhaustive proof over private Rosetta
implementation internals. [Apple Objective-C runtime](https://github.com/apple-oss-distributions/objc4/blob/main/runtime/objc-os.mm)
is the supporting distinction between image registration and per-image initialization.

## Audited builder record and fingerprint boundary

The builder consumes `target_sha256`, `lifecycle_ready`,
`bootstrap_source_sha256`, `runtime_source_sha256`, `external_libraries`
(an optional overlay list of `{path,sha256}`) and `dependencies` (the three
`{path,sha256}` entries). Do not issue `lifecycle_ready:true` for a stale intermediate
runtime: the root task is currently rebuilding its diagnostic composition.

A SHA of each top-level `.cpp` is **not** a transitive source fingerprint. Changes
to included `macos_patch.hpp`, `macos_memory.hpp`, generated pin tables, activation
checks, feature bodies, API headers or compiler/linker flags can change the result
without changing that source hash. The final audit therefore needs actual binary
SHA identities and a post-build linkage/initializer check. Source hashes identify
reviewed entry files; they must not be presented as proving every included file is
unchanged. The builder must regenerate the runtime hash before compiling the policy
into bootstrap. Bootstrap can then have its own artifact hash checked by the launcher
without a circular embedded self-hash.

The developer builder now also requires `source_closure_sha256`, covering the
sorted relative names and SHA-256 values of all native src `.h/.hpp/.cpp/.mm`
files plus the three build scripts. Header changes therefore invalidate a prior
audit. The final bootstrap is checked as a thin x86-64 dylib linking only libSystem;
its expected initializer count is one for READY, zero for the optimized inert build.
A successfully built inert bundle does not authorize the READY profile.
