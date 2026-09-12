#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <unistd.h>

#define FUTEX_TEST_STACK_SIZE 4096

static unsigned char futex_test_stack[FUTEX_TEST_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t futex_word;
static volatile uint32_t child_ready;
static volatile uint32_t child_release;
static volatile int child_wait_result;

static int
futex_child(void *arg) {
    (void)arg;
    child_wait_result = futex((uint32_t *)&futex_word,
                              FUTEX_WAIT_PRIVATE, 0, NULL);
    assert(child_wait_result == 0);
    child_ready = 1;
    assert(futex((uint32_t *)&child_ready, FUTEX_WAKE_PRIVATE, 1, NULL) >= 0);
    assert(futex((uint32_t *)&child_release, FUTEX_WAIT_PRIVATE, 0, NULL) == 0);
    assert(futex_word == 1);
    return 0;
}

int
main(void) {
    struct timespec timeout = { 0, 20000000 };
    int child, status, ret;

    futex_word = 0;
    child_ready = 0;
    child_release = 0;
    child_wait_result = -1;
    child = clone(futex_child,
                  futex_test_stack + sizeof(futex_test_stack),
                  CLONE_VM | CLONE_FS, NULL);
    assert(child > 0);

    /* Keep the value unchanged while retrying, so the child cannot miss the
     * compare-and-queue transition.  A positive wake count proves it slept. */
    while ((ret = futex((uint32_t *)&futex_word, FUTEX_WAKE_PRIVATE,
                         1, NULL)) == 0) {
        yield();
    }
    assert(ret == 1);

    while (child_ready == 0) {
        ret = futex((uint32_t *)&child_ready, FUTEX_WAIT_PRIVATE, 0,
                    &timeout);
        assert(ret == 0 || ret == -E_AGAIN || ret == -E_TIMEOUT);
    }

    futex_word = 1;
    child_release = 1;
    assert(futex((uint32_t *)&child_release, FUTEX_WAKE_PRIVATE, 1, NULL) == 1);
    assert(waitpid(child, &status) == 0 && status == 0);

    futex_word = 2;
    assert(futex((uint32_t *)&futex_word, FUTEX_WAIT_PRIVATE, 1, NULL) ==
           -E_AGAIN);
    assert(futex((uint32_t *)&futex_word, FUTEX_WAIT_PRIVATE, 2,
                 &timeout) == -E_TIMEOUT);
    assert(futex((uint32_t *)&futex_word, FUTEX_WAKE_PRIVATE, 1, NULL) == 0);
    assert(futex((uint32_t *)0x1000, FUTEX_WAKE, 1, NULL) == -E_INVAL);

    cprintf("futex wait/wake test pass.\n");
    return 0;
}
