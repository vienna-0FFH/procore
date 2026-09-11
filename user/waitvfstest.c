#include <error.h>
#include <file.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static int
wait_child(void *arg) {
    (void)arg;
    sleep(2);
    return 37;
}

int
main(void) {
    static unsigned char stack[4096] __attribute__((aligned(16)));
    char cwd[FS_MAX_FPATH_LEN + 1];
    char oldcwd[FS_MAX_FPATH_LEN + 1];
    int status = -1;
    int child;
    int fd;

    assert(getcwd(oldcwd, sizeof(oldcwd)) == 0);
    assert(mkdir("/__waitvfs") == 0);
    assert(mkdir("/__waitvfs/nonempty") == 0);
    fd = open("/__waitvfs/nonempty/file", O_WRONLY | O_CREAT);
    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(rmdir("/__waitvfs/nonempty") == -E_NOTEMPTY);
    assert(unlink("/__waitvfs/nonempty/file") == 0);

    fd = open("/__waitvfs/nonempty", O_RDONLY);
    assert(fd >= 0);
    assert(fchdir(fd) == 0);
    assert(getcwd(cwd, sizeof(cwd)) == 0);
    assert(strstr(cwd, "nonempty") != NULL);
    assert(fchdir(-1) == -E_INVAL);
    assert(close(fd) == 0);
    assert(chdir(oldcwd) == 0);
    assert(rmdir("/__waitvfs/nonempty") == 0);

    child = clone(wait_child, stack + sizeof(stack), CLONE_VM | CLONE_FS, NULL);
    assert(child > 0);
    assert(wait4(child, &status, WNOHANG) == 0);
    assert(wait4(child, &status, 0) == 0 && status == 37);
    assert(wait4(child, &status, WNOHANG) == -E_BAD_PROC);
    assert(rmdir("/__waitvfs") == 0);

    cprintf("wait4, fchdir and rmdir test pass.\n");
    return 0;
}
