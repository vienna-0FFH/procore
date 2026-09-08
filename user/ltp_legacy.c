/*
 * Small uCore-native port of the intent behind selected LTP 20210927
 * syscall tests. Linux-only probes such as /proc, signals and namespaces are
 * deliberately removed; each check uses the uCore ABI and reports one
 * deterministic process status to the host runner.
 */
#include <ulib.h>
#include <stdio.h>
#include <file.h>
#include <string.h>
#include <unistd.h>
#include <error.h>

static int checks;
static int failures;

static void
check(int condition, const char *name) {
    checks++;
    if (condition) {
        cprintf("legacy-ltp: PASS %s\n", name);
    }
    else {
        failures++;
        cprintf("legacy-ltp: FAIL %s\n", name);
    }
}

static void
check_process_identity(void) {
    int parent = getpid();
    int child = fork();
    int status = -1;
    if (child == 0) {
        exit(getpid() > 0 && getpid() == gettid() && getppid() == parent ? 0 : 1);
    }
    check(child > 0, "fork returns child pid");
    if (child > 0) {
        check(waitpid(child, &status) == 0 && status == 0,
              "waitpid reaps identity child");
    }
}

static void
check_memory(void) {
    uintptr_t oldbrk = brk(0);
    uintptr_t newbrk = oldbrk + 2 * UCORE_PAGE_SIZE + 17;
    volatile unsigned char *heap;
    void *mapping;

    check(oldbrk != (uintptr_t)-1 && brk(newbrk) == newbrk,
          "brk grows to requested address");
    heap = (volatile unsigned char *)oldbrk;
    if (oldbrk != (uintptr_t)-1 && brk(newbrk) == newbrk) {
        heap[0] = 0x5a;
        heap[2 * UCORE_PAGE_SIZE + 16] = 0xa5;
        check(heap[0] == 0x5a && heap[2 * UCORE_PAGE_SIZE + 16] == 0xa5,
              "brk pages are writable");
    }
    check(brk(oldbrk) == oldbrk, "brk shrinks to original address");

    mapping = mmap(NULL, 2 * UCORE_PAGE_SIZE,
                   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
    check(mapping != MAP_FAILED, "anonymous mmap succeeds");
    if (mapping != MAP_FAILED) {
        ((volatile unsigned char *)mapping)[0] = 1;
        ((volatile unsigned char *)mapping)[UCORE_PAGE_SIZE] = 2;
        check(((volatile unsigned char *)mapping)[0] == 1 &&
              ((volatile unsigned char *)mapping)[UCORE_PAGE_SIZE] == 2,
              "anonymous mmap pages are writable");
        check(munmap(mapping, 2 * UCORE_PAGE_SIZE) == 0,
              "munmap removes anonymous mapping");
    }
}

static void
check_file_descriptions(void) {
    char first[8];
    int fd = open("/c4demo.c", O_RDONLY);
    int duplicate;

    check(fd >= 0, "open reads bundled source");
    if (fd < 0) {
        return;
    }
    check(read(fd, first, sizeof(first)) == (int)sizeof(first),
          "read returns requested source bytes");
    duplicate = dup(fd);
    check(duplicate >= 0, "dup creates shared descriptor");
    if (duplicate >= 0) {
        check(read(duplicate, first, 1) == 1,
              "dup descriptor shares open-file offset");
        close(duplicate);
    }
    close(fd);
}

static void
check_cpu_affinity(void) {
    int cpu = getcpu();
    uint32_t mask = 0;
    int requested = (cpu >= 0 && cpu < 32) ? (uint32_t)1 << cpu : 0;

    check(cpu >= 0, "getcpu returns a non-negative CPU");
    if (requested != 0) {
        check(setaffinity(0, requested) == 0, "setaffinity accepts current CPU");
        check(getaffinity(0, &mask) == 0 && mask == requested,
              "getaffinity returns selected CPU mask");
    }
}

int
main(void) {
    check_process_identity();
    check_memory();
    check_file_descriptions();
    check_cpu_affinity();
    cprintf("legacy-ltp: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
