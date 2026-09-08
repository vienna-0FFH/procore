#include <error.h>
#include <file.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#define PIPE_STRESS_BYTES 9000

int
main(void) {
    int fds[2];
    int child;
    int status;
    char small[] = "pipe round trip";
    char buffer[128];
    static char payload[PIPE_STRESS_BYTES];
    int i;

    assert(pipe(fds) == 0 && fds[0] >= 0 && fds[1] >= 0);
    assert(write(fds[1], small, sizeof(small)) == sizeof(small));
    memset(buffer, 0, sizeof(buffer));
    assert(read(fds[0], buffer, sizeof(buffer)) == sizeof(small));
    assert(strcmp(buffer, small) == 0);

    for (i = 0; i < PIPE_STRESS_BYTES; i++) {
        payload[i] = (char)('A' + (i % 23));
    }
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        int received = 0;
        close(fds[1]);
        while (received < PIPE_STRESS_BYTES) {
            int ret = read(fds[0], buffer, sizeof(buffer));
            assert(ret > 0);
            for (i = 0; i < ret; i++) {
                assert(buffer[i] == payload[received + i]);
            }
            received += ret;
        }
        assert(read(fds[0], buffer, sizeof(buffer)) == 0);
        close(fds[0]);
        exit(0);
    }
    close(fds[0]);
    assert(write(fds[1], payload, sizeof(payload)) == sizeof(payload));
    close(fds[1]);
    assert(waitpid(child, &status) == 0 && status == 0);

    assert(pipe(fds) == 0);
    assert(close(fds[0]) == 0);
    assert(write(fds[1], small, sizeof(small)) == -E_PIPE);
    assert(close(fds[1]) == 0);
    assert(pipe2(fds, 1) == -E_INVAL);
    assert(pipe2(fds, 0) == 0);
    assert(close(fds[0]) == 0 && close(fds[1]) == 0);
    cprintf("pipe blocking, EOF, fork, and close semantics test pass.\n");
    return 0;
}
