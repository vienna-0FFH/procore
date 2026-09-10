#include <error.h>
#include <file.h>
#include <stdio.h>
#include <string.h>
#include <stat.h>
#include <ulib.h>

static void
check_stat(const char *path, size_t size) {
    struct stat first;
    struct stat second;

    assert(stat(path, &first) == 0);
    assert(lstat(path, &second) == 0);
    assert(S_ISREG(first.st_mode) && S_ISREG(second.st_mode));
    assert(first.st_size == size && second.st_size == size);
    assert(first.st_nlinks == second.st_nlinks);
}

int
main(void) {
    static const char initial[] = "0123456789";
    static const char replacement[] = "ABC";
    static const char left[] = "left-";
    static const char right[] = "right";
    char read_buffer[16];
    char vector_left[6];
    char vector_right[6];
    struct iovec write_iov[2];
    struct iovec read_iov[2];
    struct stat st;
    const char *path = "/__fileio_test";
    int fd;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    assert(fd >= 0);
    assert(write(fd, (void *)initial, sizeof(initial) - 1) ==
           (int)(sizeof(initial) - 1));
    check_stat(path, sizeof(initial) - 1);

    /* Positional operations must not change the open-file description's
     * current offset. */
    assert(pwrite(fd, replacement, sizeof(replacement) - 1, 2) ==
           (int)(sizeof(replacement) - 1));
    assert(seek(fd, 0, LSEEK_CUR) == (int)(sizeof(initial) - 1));
    memset(read_buffer, 0, sizeof(read_buffer));
    assert(pread(fd, read_buffer, 5, 0) == 5);
    assert(memcmp(read_buffer, "01ABC", 5) == 0);
    assert(seek(fd, 0, LSEEK_CUR) == (int)(sizeof(initial) - 1));

    assert(ftruncate(fd, 5) == 0);
    assert(fstat(fd, &st) == 0 && st.st_size == 5);
    check_stat(path, 5);
    assert(ftruncate(fd, -1) == -E_INVAL);
    assert(pread(fd, read_buffer, 1, -1) == -E_INVAL);

    assert(seek(fd, 0, LSEEK_SET) == 0);
    write_iov[0].iov_base = (void *)left;
    write_iov[0].iov_len = sizeof(left) - 1;
    write_iov[1].iov_base = (void *)right;
    write_iov[1].iov_len = sizeof(right) - 1;
    assert(writev(fd, write_iov, 2) ==
           (int)(sizeof(left) + sizeof(right) - 2));

    assert(seek(fd, 0, LSEEK_SET) == 0);
    memset(vector_left, 0, sizeof(vector_left));
    memset(vector_right, 0, sizeof(vector_right));
    read_iov[0].iov_base = vector_left;
    read_iov[0].iov_len = sizeof(left) - 1;
    read_iov[1].iov_base = vector_right;
    read_iov[1].iov_len = sizeof(right) - 1;
    assert(readv(fd, read_iov, 2) ==
           (int)(sizeof(left) + sizeof(right) - 2));
    assert(memcmp(vector_left, left, sizeof(left) - 1) == 0);
    assert(memcmp(vector_right, right, sizeof(right) - 1) == 0);
    assert(readv(fd, NULL, 0) == 0);

    assert(truncate(path, 3) == 0);
    check_stat(path, 3);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);
    assert(stat(path, &st) == -E_NOENT);
    cprintf("path stat, truncate, positional and vector I/O test pass.\n");
    return 0;
}
