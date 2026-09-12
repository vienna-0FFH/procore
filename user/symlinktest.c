#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <file.h>
#include <stat.h>
#include <string.h>
#include <error.h>

int
main(void) {
    static const char target[] = "/__symlink_target";
    static const char link[] = "/__symlink_link";
    static const char payload[] = "target-data";
    char buffer[FS_MAX_FPATH_LEN + 1];
    struct stat st;
    int fd;

    fd = open(target, O_WRONLY | O_CREAT);
    assert(fd >= 0);
    assert(write(fd, (void *)payload, sizeof(payload) - 1) ==
           (int)(sizeof(payload) - 1));
    assert(close(fd) == 0);

    assert(symlink(target, link) == 0);
    assert(symlink(target, link) == -E_EXISTS);
    memset(buffer, 0, sizeof(buffer));
    assert(readlink(link, buffer, sizeof(buffer)) == (int)strlen(target));
    assert(memcmp(buffer, target, strlen(target)) == 0);
    assert(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
    assert(readlink(target, buffer, sizeof(buffer)) == -E_INVAL);
    assert(unlink(link) == 0);
    assert(unlink(target) == 0);

    cprintf("symlink/readlink test pass.\n");
    return 0;
}
