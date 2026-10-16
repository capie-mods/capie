// Fixed metadata only: no argv, authentication data, or environment values.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    const char* path = getenv("CAPIE_FIXTURE_RECORD_FILE");
    if (!path) return 64;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return 73;
    char cwd[4096];
    const char* wd = getcwd(cwd, sizeof(cwd));
    int result = dprintf(fd, "pid=%ld\nparent_pid=%ld\ncwd=%s\ndyld_present=%d\n",
                        (long)getpid(), (long)getppid(), wd ? wd : "unavailable",
                        getenv("DYLD_INSERT_LIBRARIES") != NULL);
    return close(fd) != 0 || result < 0;
}
