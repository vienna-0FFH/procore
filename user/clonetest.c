#include <ulib.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <unistd.h>

#define CLONE_TEST_STACK_SIZE 4096

static unsigned char clone_test_stack[CLONE_TEST_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile int clone_test_value;

static int
clone_child(void *arg) {
    assert(arg == NULL);
    assert(getppid() > 0);
    assert(gettid() == getpid());
    clone_test_value = 0x5a5a;
    cprintf("clone child: pid=%d ppid=%d tid=%d cpu=%d\n",
            getpid(), getppid(), gettid(), getcpu());
    return 0;
}

int
main(void) {
    int parent, child, status;

    parent = getpid();
    assert(parent > 0);
    assert(gettid() == parent);
    assert(getppid() > 0);
    assert(getcpu() >= 0);

    child = clone(clone_child,
                  clone_test_stack + sizeof(clone_test_stack),
                  CLONE_VM | CLONE_FS, NULL);

    assert(child > 0);
    assert(waitpid(child, &status) == 0);
    assert(status == 0);
    assert(clone_test_value == 0x5a5a);
    assert(clone(clone_child,
                 clone_test_stack + sizeof(clone_test_stack),
                 CLONE_THREAD, NULL) == -E_INVAL);

    cprintf("clone/gettid/getppid/getcpu test pass.\n");
    return 0;
}
