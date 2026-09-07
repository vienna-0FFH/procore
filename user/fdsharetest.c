#include <ulib.h>
#include <unistd.h>
#include <file.h>
#include <stdio.h>
#include <error.h>

#define FDSHARE_LOOPS          96
#define FDSHARE_STACK_SIZE     4096

static unsigned char fdshare_stack[FDSHARE_STACK_SIZE]
    __attribute__((aligned(16)));
static int shared_fd;
static volatile int child_errors;

static int
fdshare_child(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < FDSHARE_LOOPS; i++) {
        char byte;
        int duplicate = dup(shared_fd);
        if (duplicate < 0) {
            child_errors++;
            continue;
        }
        if (read(duplicate, &byte, sizeof(byte)) != sizeof(byte)) {
            child_errors++;
        }
        if (close(duplicate) != 0) {
            child_errors++;
        }
    }
    return 0;
}

int
main(void) {
    int child;
    int status;
    int i;
    int replace_fd;

    shared_fd = open("/sh", O_RDONLY);
    assert(shared_fd >= 0);
    replace_fd = open("/sh", O_RDONLY);
    assert(replace_fd >= 0 && replace_fd != shared_fd);
    assert(dup2(shared_fd, replace_fd) == replace_fd);
    assert(dup2(shared_fd, shared_fd) == shared_fd);
    assert(close(replace_fd) == 0);
    child = clone(fdshare_child, fdshare_stack + sizeof(fdshare_stack),
                  CLONE_VM | CLONE_FS, NULL);
    assert(child > 0);
    for (i = 0; i < FDSHARE_LOOPS; i++) {
        char byte;
        assert(read(shared_fd, &byte, sizeof(byte)) == sizeof(byte));
        if ((i & 7) == 0) {
            yield();
        }
    }
    assert(waitpid(child, &status) == 0 && status == 0);
    assert(child_errors == 0);
    assert(seek(shared_fd, 0, LSEEK_CUR) == 2 * FDSHARE_LOOPS);

    assert(seek(shared_fd, 0, LSEEK_SET) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        char byte;
        assert(read(shared_fd, &byte, sizeof(byte)) == sizeof(byte));
        exit(0);
    }
    assert(waitpid(child, &status) == 0 && status == 0);
    assert(seek(shared_fd, 0, LSEEK_CUR) == 1);
    assert(close(shared_fd) == 0);
    cprintf("shared file descriptions and table concurrency test pass.\n");
    return 0;
}
