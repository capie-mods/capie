// Writes only the loaded process PID, never arguments or environment values.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
__attribute__((constructor)) static void loaded(void) {
    const char* path = getenv("CAPIE_FIXTURE_MARKER_FILE");
    if (!path) return;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    char line[32];
    int n = snprintf(line, sizeof(line), "%ld\n", (long)getpid());
    if (n > 0 && n < (int)sizeof(line)) (void)write(fd, line, (size_t)n);
    (void)close(fd);
}
