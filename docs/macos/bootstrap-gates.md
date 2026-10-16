# Minimal first-inserted bootstrap gates

Implementation: `plugin/capie/src/bootstrap/macos_bootstrap.cpp`; stable C interface:
`plugin/capie/src/bootstrap/macos_bootstrap_api.h`. This code has **not been injected
into or used to patch HOI4**. The default build has no configured lifecycle policy
and refuses activation. Windows sources are unchanged.

## Contract and ordering

The configured initializer verifies exact inserted-library order and paths, rejects
all other `DYLD_*` overrides, requires its own image at dyld index zero, hashes each
allowed later inserted dylib and compares its loaded header/load commands to that
file. It finds the unique `MH_EXECUTE` image (which is not necessarily image zero).
It checks the exact HOI4 file SHA, slid loaded header identity, all **240 static
profile checks**, and four supplemental allocator/stub byte checks. It then
preflights **all 15** complete relocation-free hook prefixes and RX source/destination
pages before the first mutation. Runtime initialization-token values are intentionally
left to the late runtime.

The initializer uses the existing `macos_patch.hpp` local copy/write/cache/restore
backend. On a later installation failure it removes earlier installed hooks in
reverse order. Normal refusals and successful rollback leave `DISABLED`. A permanent
OS rollback failure leaves explicit `POISONED`, never mislabeled as disabled. This
catastrophic state is a remaining failure mode; the code does not terminate the game
or claim safe recovery from a partially restored executable page.

Each hook target jumps to an assembly stub that tail-jumps through an aligned
lock-free pointer. The original trampoline is installed in that slot before any
later initializer can run. The stub changes no argument register, stack slot, XMM
register or sret pointer. Release stores publish replacement pointers; an aligned
x86-64 memory load has acquire ordering. This is deliberately x86-64-specific and
is not a portable C++ concurrent-reader implementation. The backend and stubs must
remain loaded for process lifetime, as must all published callbacks.

`FORWARDING=1` means all gates installed after the full static preflight, even when
some dispatch slots have subsequently been armed. `set_dispatch(index,NULL)` restores
original forwarding. Public accessors return no original/base when disabled or
poisoned. Indices match `macos_hook_prefixes.h` exactly; `effect_ctor` patches the
proven body RVA `0x20b2b0`, not the original tail-jump thunk.

## Immutable build policy

Generate `macos_bootstrap_policy.h` in a build include directory only after the late
runtime has been built. Its expected data format is:

```cpp
#define CAPIE_BOOTSTRAP_POLICY_READY 1
static constexpr char capie_bootstrap_expected_inserted[] =
    "/absolute/bootstrap.dylib:/absolute/runtime.dylib";
static constexpr const char* capie_bootstrap_inserted_paths[] = {
    "/absolute/bootstrap.dylib", "/absolute/runtime.dylib"
};
static constexpr const char* capie_bootstrap_inserted_hashes[] = {
    "", "<64 lowercase hex SHA-256 of runtime.dylib>"
};
```

Paths must match dyld's actual canonical names; `/var` versus `/private/var` aliases
are not silently accepted. Up to eight inserted images are allowed. The bootstrap's
own hash is checked by the launcher rather than embedded circularly in itself.
Unknown inserted images fail closed. Merely generating this header does **not**
audit transitive dependency initializers, Objective-C callbacks, resolver behavior
or already-running Rosetta executors. Setting READY requires the separately reviewed
exact target/known-library lifecycle closure from `early-init-evidence.md` and
`lifecycle-gates.md`. No generic thread-count claim is made. This closure authorization
must be controlled by the build/install workflow; it is not proven by the path list.

Build (x86-64 only):

```sh
xcrun clang++ -std=c++20 -arch x86_64 -O2 -dynamiclib \
  -nostdlib++ -fno-exceptions -fno-rtti -Wno-deprecated-declarations \
  -I/path/to/generated-policy \
  plugin/capie/src/bootstrap/macos_bootstrap.cpp -o capie_bootstrap.dylib
```

The production source compiled with an enabled immutable refusal policy links only
`/usr/lib/libSystem.B.dylib`, with no libc++, allocation constructors, exception or
RTTI runtime imports. Actual Mach-O analysis finds exactly **one initializer** in
`__TEXT,__init_offsets` (four-byte offset, modern linker), rather than assuming an
older `__mod_init_func` section. All persistent hook/atomic state uses `constinit`.

## Reproducible owned test

```sh
python3 plugin/capie/tests/macos_bootstrap/run.py
```

Results: `bootstrap-gates-results.json`. The runner compiles fifteen owned assembly
functions and a dedicated fixture harness, replacing the target table and main
identity test only for that harness. It executes the actual production installation,
rollback, public API, assembly stubs and policy gate. The production main verifier
is separately compiled and its foreign-image refusal is executed. The fixture does
not claim its synthetic main passed the HOI4 240-pin verifier.

Observed under x86-64 Rosetta:

1. All 15 original functions forward before the later dylib initializer; publication
   changes one to a replacement calling its original, then NULL restores forwarding.
   Seven integer arguments (including stack), an XMM double, and a 24-byte sret return
   survive the stubs.
2. Wrong inserted order, unknown inserted dylib and unknown DYLD override refuse;
   all originals remain usable.
3. The **last** pinned prefix mismatch refuses before modifying any of the other 14.
   A changed owned executable SHA also refuses.
4. The actual production bootstrap refuses a foreign executable. Its binary has one
   initializer and only libSystem linkage. Python Ruff and py_compile pass.

Synthetic backend tests cover write/protection failures and poisoned rollback
separately. This fixture does not inject kernel failures, establish game callback ABI,
prove every game target is unreachable before the initializer, or test feature behavior.
No game process was started, stopped or patched by this test.
