#include <ulib.h>
#include <unistd.h>
#include <stdio.h>

/* Large enough to exercise the replacement queue when QEMU is started with
 * a small -m value, while remaining below the user VMA limit. */
#define COW_STRESS_PAGES 3072

int
main(void) {
    volatile unsigned char *mapping;
    int child, status, i;

    mapping = (volatile unsigned char *)mmap(NULL,
        COW_STRESS_PAGES * UCORE_PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS);
    assert(mapping != MAP_FAILED);
    for (i = 0; i < COW_STRESS_PAGES; i++) {
        mapping[i * UCORE_PAGE_SIZE] = (unsigned char)i;
    }

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        for (i = 0; i < COW_STRESS_PAGES; i += 2) {
            mapping[i * UCORE_PAGE_SIZE] ^= 0x5A;
        }
        exit(0);
    }
    assert(waitpid(child, &status) == 0 && status == 0);
    for (i = 0; i < COW_STRESS_PAGES; i++) {
        assert(mapping[i * UCORE_PAGE_SIZE] == (unsigned char)i);
    }
    assert(munmap((void *)mapping,
                  COW_STRESS_PAGES * UCORE_PAGE_SIZE) == 0);
    cprintf("copy-on-write stress test pass.\n");
    return 0;
}
