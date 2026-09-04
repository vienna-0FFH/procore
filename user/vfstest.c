#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <dir.h>
#include <file.h>
#include <stat.h>
#include <string.h>
#include <error.h>
#include <unistd.h>

int
main(void) {
    static const char payload[] = "vfs";
    char buffer[sizeof(payload)];
    struct stat st;
    int fd;

    assert(mkdir("disk0:/__vfs_case") == 0);
    assert(mkdir("/__vfs_case/sub") == 0);
    assert(mkdir("/__vfs_case/sub") == -E_EXISTS);

    fd = open("disk0:/__vfs_case//sub/./file", O_RDWR | O_CREAT);
    assert(fd >= 0);
    assert(write(fd, (void *)payload, sizeof(payload)) == sizeof(payload));
    assert(seek(fd, 0, LSEEK_SET) == 0);
    memset(buffer, 0, sizeof(buffer));
    assert(read(fd, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(strcmp(buffer, payload) == 0);
    assert(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlinks == 1);
    assert(close(fd) == 0);

    assert(link("/__vfs_case/sub/file", "/__vfs_case/sub/link") == 0);
    fd = open("/__vfs_case/sub/file", O_RDONLY);
    assert(fd >= 0 && fstat(fd, &st) == 0 && st.st_nlinks == 2);
    assert(close(fd) == 0);

    assert(rename("/__vfs_case/sub/file", "/__vfs_case/file") == 0);
    assert(open("/__vfs_case/sub/file", O_RDONLY) == -E_NOENT);
    fd = open("/__vfs_case/file", O_RDONLY);
    assert(fd >= 0);
    assert(close(fd) == 0);

    assert(unlink("/__vfs_case/sub") == -E_NOTEMPTY);
    assert(unlink("/__vfs_case/sub/link") == 0);
    assert(unlink("/__vfs_case/sub") == 0);
    assert(unlink("/__vfs_case/file") == 0);
    assert(unlink("/__vfs_case") == 0);
    assert(open("/__vfs_case/file", O_RDONLY) == -E_NOENT);

    cprintf("vfs namespace test pass.\n");
    return 0;
}
