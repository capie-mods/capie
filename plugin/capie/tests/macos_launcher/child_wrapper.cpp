// Fixture only. Explicit gate switch tests loading; it does not parse dlc_load.json.
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
int main(int argc, char** argv) {
    if (argc != 5 || argv[2][0] != '/' || argv[3][0] != '/' || argv[4][0] != '/') return 64;
    const bool enabled = std::strcmp(argv[1], "--enabled") == 0;
    // Disabled and malformed gate inputs both run the clean recorder.
    unsetenv("DYLD_INSERT_LIBRARIES");
    unsetenv("DYLD_FORCE_FLAT_NAMESPACE");
    const std::string marker = std::string(argv[4]) + "/loaded-pid.txt";
    const std::string record = std::string(argv[4]) + "/child-record.txt";
    if (setenv("CAPIE_FIXTURE_MARKER_FILE", marker.c_str(), 1) != 0 ||
        setenv("CAPIE_FIXTURE_RECORD_FILE", record.c_str(), 1) != 0) return 71;
    if (enabled && setenv("DYLD_INSERT_LIBRARIES", argv[3], 1) != 0) return 71;
    char* child_args[] = {argv[2], nullptr};
    execv(argv[2], child_args);
    return 72;
}
