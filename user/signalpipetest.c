#include <error.h>
#include <file.h>
#include <signal.h>
#include <stdio.h>
#include <ulib.h>

int
main(void) {
    int fds[2];
    int child;
    int status;
    char value = 'x';

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(pipe(fds) == 0);
        assert(close(fds[0]) == 0);
        /* Default SIGPIPE terminates the writer before this assertion can
         * run; the parent verifies the resulting exit status. */
        (void)write(fds[1], &value, 1);
        exit(0);
    }
    assert(waitpid(child, &status) == 0);
    assert(status == -E_KILLED);
    cprintf("SIGPIPE default action test pass.\n");
    return 0;
}
