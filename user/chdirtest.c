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
    int fd, dupfd;

    assert(getcwd(cwd, sizeof(cwd)) == 0);
    assert(chdir("disk0:") == 0);
    assert(getcwd(cwd, sizeof(cwd)) == 0);
    assert(chdir("/path-that-does-not-exist") == -E_NOENT);

    fd = open("/sh", O_RDONLY);
    assert(fd >= 0);
    dupfd = dup(fd);
    assert(dupfd >= 0 && dupfd != fd);
    assert(seek(fd, 0, LSEEK_SET) == 0);
    assert(read(fd, cwd, 1) == 1);
    assert(seek(dupfd, 0, LSEEK_CUR) == 1);
    assert(read(dupfd, cwd, 1) == 1);
    assert(seek(fd, 0, LSEEK_CUR) == 2);
    assert(close(dupfd) == 0);
    assert(close(fd) == 0);

    cprintf("chdir/dup test pass.\n");
    return 0;
}
