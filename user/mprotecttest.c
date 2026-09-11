#include <ulib.h>
#include <stdio.h>

int
main(void) {
    volatile unsigned char *mapping;
    int pid, status;

    mapping = (volatile unsigned char *)mmap(NULL, 2 * UCORE_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
    assert(mapping != MAP_FAILED);
    mapping[0] = 0x31;
    mapping[UCORE_PAGE_SIZE] = 0x62;

    /* A read-only VMA must reject a user write in the child while leaving
     * the parent's page and contents untouched. */
    assert(mprotect((void *)mapping, UCORE_PAGE_SIZE, PROT_READ) == 0);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        mapping[0] = 0xA5;
        exit(1);
    }
    assert(waitpid(pid, &status) == 0 && status != 0);
    assert(mapping[0] == 0x31);

    /* Restoring write permission updates both the VMA and resident PTE. */
    assert(mprotect((void *)mapping, UCORE_PAGE_SIZE,
                    PROT_READ | PROT_WRITE) == 0);
    mapping[0] = 0x44;
    assert(mapping[0] == 0x44);

    /* A writable page is COW-shared by fork.  Permission changes in the
     * child must not discard that COW state before the first write. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        assert(mprotect((void *)(mapping + UCORE_PAGE_SIZE),
                        UCORE_PAGE_SIZE, PROT_READ) == 0);
        assert(mprotect((void *)(mapping + UCORE_PAGE_SIZE),
                        UCORE_PAGE_SIZE, PROT_READ | PROT_WRITE) == 0);
        mapping[UCORE_PAGE_SIZE] = 0xA3;
        exit(0);
    }
    assert(waitpid(pid, &status) == 0 && status == 0);
    assert(mapping[UCORE_PAGE_SIZE] == 0x62);

    /* PROT_NONE retains the resident page.  Re-enabling access must preserve
     * the byte written before the protection change. */
    assert(mprotect((void *)(mapping + UCORE_PAGE_SIZE), UCORE_PAGE_SIZE,
                    PROT_NONE) == 0);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        assert(mprotect((void *)(mapping + UCORE_PAGE_SIZE),
                        UCORE_PAGE_SIZE, PROT_READ | PROT_WRITE) == 0);
        mapping[UCORE_PAGE_SIZE] = 0xA6;
        exit(0);
    }
    assert(waitpid(pid, &status) == 0 && status == 0);
    assert(mprotect((void *)(mapping + UCORE_PAGE_SIZE), UCORE_PAGE_SIZE,
                    PROT_READ | PROT_WRITE) == 0);
    assert(mapping[UCORE_PAGE_SIZE] == 0x62);

    assert(munmap((void *)mapping, 2 * UCORE_PAGE_SIZE) == 0);
    cprintf("mprotect test pass.\n");
    return 0;
}
