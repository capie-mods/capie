// Offline fixture: prove the loader invokes the two local ABI exports in order.
#include <windows.h>

#ifndef FIXTURE_LABEL
#define FIXTURE_LABEL 'C'
#endif
#ifndef FIXTURE_RESULT
#define FIXTURE_RESULT 0
#endif

#ifndef FIXTURE_NO_EXPORT
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    wchar_t path[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"CAPIE_PROXY_TEST_TRACE", path, 32768);
    if (length == 0 || length >= 32768) {
        return 77;
    }
    // Crash-loop breaker: the loader must have written its sentinel before calling us.
    wchar_t sentinel[32768]{};
    const DWORD sentinel_length = GetEnvironmentVariableW(L"CAPIE_PROXY_TEST_SENTINEL", sentinel, 32768);
    if (sentinel_length > 0 && sentinel_length < 32768 && GetFileAttributesW(sentinel) == INVALID_FILE_ATTRIBUTES) {
        return 80;
    }
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 78;
    }
    const char label = FIXTURE_LABEL;
    DWORD actual = 0;
    const BOOL ok = WriteFile(file, &label, 1, &actual, nullptr);
    CloseHandle(file);
    return ok != 0 && actual == 1 ? FIXTURE_RESULT : 79;
}
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
