// Separate render-candidate injector; same basename, fresh process only.
// Shared injector rejects any existing module with this basename, regardless of path.
#define RN_EXPECTED_DLL_NAME L"rename_anything_inline_editor_bridge.dll"
#include "../src/injector.cpp"
