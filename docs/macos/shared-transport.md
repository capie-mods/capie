# Shared editor transport and host probe on macOS

`transport.hpp`, `public_editor.hpp` and `debug_observer.hpp` now compile together
on macOS using platform record-string types. The public editor retains the same
reserved-key filtering, epoch/readiness, identity, visibility, snapshot and payload
logic. There is no independent permissive macOS Apply path.

The shared constructor hook uses five SysV arguments and **returns void** on Apple.
It still requires an active bridge, the current scoped Origin, exact model/selector/
scope/source pointer identities and fresh matching model/selector names. It calls
the original exactly once, substituting the copied records only inside that frame.
Afterward it inspects the provided self object, because native RAX has no constructor
result contract. Debug logging labels this `inspected_self`; Windows still forwards
its original pointer result. Static ownership proof is in records-transport.md.

Country capabilities are provided by an explicitly installed
`verified_country_capabilities` callback, atomic and null by default. No dylib symbol
lookup or module-presence shortcut is used. Null yields0 and refuses name/flag/portrait
capability claims. Production must install the callback only after native core
verification; the fixture installs only its own bounded function and resets it.
This callback is not a native-fault recovery mechanism.

Debug observers use the same native paired root-array guard as window_view. They
never manufacture Windows `root_third_list` or root span offsets. Kernel-copy
`core::readable` provides bounded record-range inspection; no `__try` macro shim is
introduced.

`host_probe.hpp` now selects the proven native chain from layers-map.json:
app global RVA0x3639ad8, session+0x320, native RTTI address points, human array+0xe0,
count+0xec, stride0x90, machine id+0x88 and libc++ name+0x20. Native names are read as
bounded content bytes, preserving the probe's content-comparison semantics. The
human span is checked for overflow. Host detection remains diagnostic only: no
`capie_host` record or authorization is enabled, and no multiplayer claim follows
from these synthetic local-host fixtures.

## Offline results

`macos_transport_test.cpp` started RED on Windows model-string types and then
compiled the actual shared transport/public/debug headers after the platform changes.
**22 checks pass** on native arm64 with ASan/UBSan and on x86_64 via Rosetta:

- null/explicit capability callback and unchanged capability decoding;
- native reserved-record filtering, unrelated negative raw value, generated key,
  protocol key limit and malformed vector bounds;
- exact once-only constructor replacement, changed model-name refusal, outside-frame
  and inactive forwarding;
- native host-name decoding, complete synthetic local-host chain and foreign-session rejection.

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_transport_test.cpp -o /tmp/capie-transport-asan
/tmp/capie-transport-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_transport_test.cpp -o /tmp/capie-transport-intel
/tmp/capie-transport-intel
```

No game process, runtime detour, native UI mutation, real Apply click or multiplayer
session was used. The fixture does not prove runtime lifecycle scheduling, actual
constructor execution or native fault tolerance. Native game-call gates remain
separately default-disabled.
