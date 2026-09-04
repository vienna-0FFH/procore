#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <unistd.h>

int
main(void) {
    char *mapping;
    uintptr_t oldbrk, newbrk;

    mapping = mmap(NULL, 3 * UCORE_PAGE_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS);
    assert(mapping != MAP_FAILED &&
           ((uintptr_t)mapping % UCORE_PAGE_SIZE) == 0);
    mapping[0] = 'm';
    mapping[UCORE_PAGE_SIZE] = 'a';
    mapping[3 * UCORE_PAGE_SIZE - 1] = 'p';
    assert(mapping[0] == 'm' && mapping[UCORE_PAGE_SIZE] == 'a' &&
           mapping[3 * UCORE_PAGE_SIZE - 1] == 'p');
    assert(munmap(mapping + UCORE_PAGE_SIZE, UCORE_PAGE_SIZE) == 0);
    assert(mapping[0] == 'm' && mapping[3 * UCORE_PAGE_SIZE - 1] == 'p');
    assert(munmap(mapping, UCORE_PAGE_SIZE) == 0);
    assert(munmap(mapping + 2 * UCORE_PAGE_SIZE, UCORE_PAGE_SIZE) == 0);

    oldbrk = brk(0);
    assert(oldbrk != (uintptr_t)-1);
    newbrk = oldbrk + 2 * UCORE_PAGE_SIZE + 37;
    assert(brk(newbrk) == newbrk);
    ((volatile char *)oldbrk)[0] = 'b';
    ((volatile char *)oldbrk + 2 * UCORE_PAGE_SIZE)[36] = 'k';
    assert(brk(oldbrk) == oldbrk);

    assert(mmap(NULL, UCORE_PAGE_SIZE, PROT_READ, MAP_PRIVATE) == MAP_FAILED);
    cprintf("mmap/munmap/brk test pass.\n");
    return 0;
}
