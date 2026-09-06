#include <ulib.h>
#include <unistd.h>
#include <stdio.h>
#include <file.h>

#define COW_PAGES 8

int
main(void) {
    volatile unsigned char *mapping;
    int pid, status;
    int i;

    mapping = (volatile unsigned char *)mmap(NULL,
        COW_PAGES * UCORE_PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS);
    assert(mapping != MAP_FAILED);

    for (i = 0; i < COW_PAGES; i++) {
        mapping[i * UCORE_PAGE_SIZE] = (unsigned char)(0x20 + i);
    }

    if ((pid = fork()) == 0) {
        int fd;
        for (i = 0; i < COW_PAGES - 1; i++) {
            assert(mapping[i * UCORE_PAGE_SIZE] == (unsigned char)(0x20 + i));
            mapping[i * UCORE_PAGE_SIZE] = (unsigned char)(0xA0 + i);
        }
        /* Exercise the kernel copy-to-user path while the last page is
         * still COW-protected. */
        fd = open("/sh", O_RDONLY);
        assert(fd >= 0);
        assert(read(fd, (void *)(mapping + (COW_PAGES - 1) * UCORE_PAGE_SIZE), 16) == 16);
        close(fd);
        exit(0);
    }

    assert(pid > 0);
    assert(waitpid(pid, &status) == 0 && status == 0);
    for (i = 0; i < COW_PAGES; i++) {
        assert(mapping[i * UCORE_PAGE_SIZE] == (unsigned char)(0x20 + i));
    }

    /* Once the child is gone, the parent should upgrade a sole COW page
     * in-place instead of allocating another copy. */
    mapping[3 * UCORE_PAGE_SIZE] = 0x5A;
    assert(mapping[3 * UCORE_PAGE_SIZE] == 0x5A);
    assert(munmap((void *)mapping, COW_PAGES * UCORE_PAGE_SIZE) == 0);
    cprintf("copy-on-write test pass.\n");
    return 0;
}
