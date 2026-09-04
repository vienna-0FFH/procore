#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <dir.h>
#include <file.h>
#include <error.h>
#include <unistd.h>

int
main(void) {
    char cwd[FS_MAX_FPATH_LEN + 1];
    char first, second;
    int fd, dupfd;

    assert(getcwd(cwd, sizeof(cwd)) == 0);
    assert(chdir("disk0:") == 0);
    assert(getcwd(cwd, sizeof(cwd)) == 0);
    assert(chdir("/path-that-does-not-exist") == -E_NOENT);

    fd = open("/sh", O_RDONLY);
    assert(fd >= 0);
    dupfd = dup(fd);
    assert(dupfd >= 0 && dupfd != fd);
    assert(read(fd, &first, sizeof(first)) == sizeof(first));
    assert(read(dupfd, &second, sizeof(second)) == sizeof(second));
    assert(first == second);
    assert(close(dupfd) == 0);
    assert(close(fd) == 0);

    cprintf("chdir/dup test pass.\n");
    return 0;
}
