#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>

int
main(void) {
    volatile unsigned char *mapping;
    size_t length = 2 * UCORE_PAGE_SIZE;

    mapping = (volatile unsigned char *)mmap(NULL, length,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
    assert(mapping != MAP_FAILED);
    mapping[0] = 0x41;
    mapping[UCORE_PAGE_SIZE] = 0x52;

    assert(madvise((void *)mapping + 1, length - 1, MADV_NORMAL) == 0);
    assert(madvise((void *)mapping, length, MADV_RANDOM) == 0);
    assert(madvise((void *)mapping, length, MADV_SEQUENTIAL) == 0);
    assert(madvise((void *)mapping, length, MADV_WILLNEED) == 0);
    assert(madvise((void *)mapping, length, MADV_DONTNEED) == 0);
    cprintf("madvise values: %d %d\n", mapping[0], mapping[UCORE_PAGE_SIZE]);
    assert(mapping[0] == 0 && mapping[UCORE_PAGE_SIZE] == 0);
    assert(madvise((void *)mapping, length, 1212) == -E_INVAL);
    assert(madvise((void *)0x1000, UCORE_PAGE_SIZE, MADV_NORMAL) == -E_INVAL);

    assert(munmap((void *)mapping, length) == 0);
    cprintf("madvise test pass.\n");
    return 0;
}
