# Shared macOS widget/window readers

`engine/widgets.hpp` and `editor/window_view.hpp` now select the independent macOS
layout. The Windows branches retain their existing string and conversion semantics.
No game hook or game process is started by these readers or the offline fixture.

Native edit snapshots verify all three object/definition vtables and the strict
native definition name, decode the tagged UTF-32 object at edit+0xa0, bound usable
capacity to 65536 elements and content to128, then copy a complete terminated value.
The existing `macos_records`/`macos_string` adapters provide native storage decoding.
Invalid text is not truncated or normalized. Codepoint alphabet checks remain the
higher-level editor protocol's responsibility.

Carrier UTF-8 decoding on Apple accepts canonical sequences only and rejects
surrogates, values above U+10FFFF, truncated/overlong sequences and the same controls
and markup characters as Windows. The copied decoded result is published only after
a successful complete decode. Carrier lookup remains a supplied `FindWidget`
callback, so fixtures can use owned objects rather than call the game.

The shared window chain applies `macos::root_guard` in place of the Windows anonymous
third-list heuristic. It validates the native root prefix through0xeb0 and the paired
instant pointers/names at0xe20/0xe38 and edit pointers/names at0xe80/0xe98, including
counts, names and child vtables. This native policy is proven in editor-map.md;
there is no invented macOS `root_third_list` constant.

The fast model prefilter decodes one native header and compares content without
requiring the terminator, preserving its deliberately looser acceptance rule.
Full `inspect_window` still performs strict native name checks and fresh root/context
identity and visibility checks. Both explicit editor model names and the public
editor grammar prefix remain supported.

`macos_shared_widgets_test.cpp` includes the actual shared window/widget headers and
uses native object fixtures and an injected carrier lookup. Initial compilation was
RED on the Windows-only game profile/calling conventions. After integration,
23 checks pass on native arm64 with ASan/UBSan and x86_64 under Rosetta. The fixture
explicitly grants its current thread the native-call test gate; production leaves
that gate disabled. No general SEH-equivalent native fault recovery is claimed. This fixture is not a real GUI lifecycle,
focus, prefill or Apply test. Such integration remains unverified in the game.


```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  plugin/capie/tests/macos_shared_widgets_test.cpp -o /tmp/capie-shared-widgets-asan
/tmp/capie-shared-widgets-asan
clang++ -std=c++20 -arch x86_64 -Wall -Wextra -Werror \
  plugin/capie/tests/macos_shared_widgets_test.cpp -o /tmp/capie-shared-widgets-intel
/tmp/capie-shared-widgets-intel
```
