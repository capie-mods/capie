# Explicit country composition, without patch installation

`plugin/capie/src/dll/macos_country.hpp` composes the existing shared localisation,
NameLayer, FlagLayer and PortraitLayer. It installs no patches, has no dylib load
initializer and exports no API. A caller explicitly constructs the owner with a bounded
read/write memory policy and supplies the verified image, all 11 original hook targets,
portrait validity callback and native allocate/free callbacks.

Configure checks completeness and resolves the image base but **does not prove** file
SHA-256, live pin equality, callback ABI, allocator ownership or quiescence. Those remain
prerequisites supplied by the eventual bootstrap/lifecycle. All original functions must
be present even if the caller requests a subset of layers, so every exposed hook can
forward safely. Callback addresses are unavailable before successful configuration.
Only one owner can bind the process-wide shared localisation globals.

`enable(layers, full_contract_ready=false)` refuses admission by default. The future
complete editor+country composition must establish the full script contract before
passing true. This also prevents NameLayer from generating RN1 keys while localisation
is disabled. After admission the original shared lookup functions handle RN1/cache and
public status/API strings; no local copy of their algorithms exists. Internal capability
bits describe selected layers and are zero while inactive. Fixture admission is not
proof that the actual macOS port provides API 7.

Native wrappers preserve the SysV signatures: faction result storage comes first;
portrait result storage precedes its map receiver; atlas update returns void. UpdateNames
then updates flag/portrait caches, and set_variable refreshes them for their shared commit
keys. These mirror the Windows composition order without including Windows runtime code.

Disable clears activation but retains originals, interned localisation answers and layer
caches for residual forwarding. It does not uninstall hooks or authorize unloading.
Configure, enable, disable and destruction require caller-established quiescence. The
object and trampolines must outlive every callback; destroy only after all patches are
removed and no callback is in flight. No test here proves those game lifecycle conditions.

Standalone verification (no game):

```sh
clang++ -std=c++20 -arch arm64 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_country_test.cpp -o /tmp/capie-country-arm64
/tmp/capie-country-arm64
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_country_test.cpp -o /tmp/capie-country-x86_64
/tmp/capie-country-x86_64
```

42 checks pass per architecture: complete original binding/refusals, exclusive ownership,
all 11 inactive forwarding paths and scalar/output argument order, default full-contract
refusal, shared RN1/status after fixture admission, selected capability bits, and disable
with retained original forwarding and interned answer lifetime. The arm64 fixture ran
ASan/UBSan; x86_64 ran under Rosetta. Native game feature behavior remains untested here.
