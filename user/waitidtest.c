#include <error.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

static int
waitid_child(void *arg) {
    (void)arg;
    sleep(2);
    return 23;
}

int
main(void) {
    static unsigned char stack[4096] __attribute__((aligned(16)));
    siginfo_t info;
    int child;

    child = clone(waitid_child, stack + sizeof(stack), CLONE_VM | CLONE_FS,
                  NULL);
    assert(child > 0);
    memset(&info, 0xA5, sizeof(info));
    assert(waitid(P_PID, child, &info, WEXITED | WNOHANG) == 0);
    assert(info.si_signo == 0 && info.si_pid == 0);

    memset(&info, 0, sizeof(info));
    assert(waitid(P_PID, child, &info, WEXITED | WNOWAIT) == 0);
    assert(info.si_signo == SIGCHLD && info.si_code == CLD_EXITED &&
           info.si_pid == child && info.si_status == 23);
    memset(&info, 0, sizeof(info));
    assert(waitid(P_PID, child, &info, WEXITED | WNOWAIT) == 0);
    assert(info.si_pid == child && info.si_status == 23);
    memset(&info, 0, sizeof(info));
    assert(waitid(P_PID, child, &info, WEXITED) == 0);
    assert(info.si_pid == child && info.si_status == 23);
    assert(waitid(P_PID, child, &info, WEXITED | WNOHANG) == -E_BAD_PROC);

    child = clone(waitid_child, stack + sizeof(stack), CLONE_VM | CLONE_FS,
                  NULL);
    assert(child > 0);
    memset(&info, 0, sizeof(info));
    assert(waitid(P_ALL, 0, &info, WEXITED) == 0);
    assert(info.si_code == CLD_EXITED && info.si_pid == child &&
           info.si_status == 23);
    assert(waitid(P_PGID, 0, &info, WEXITED) == -E_UNIMP);
    assert(waitid(P_PID, 0, &info, WEXITED) == -E_INVAL);
    assert(waitid(P_ALL, 0, NULL, WEXITED) == -E_INVAL);
    assert(waitid(P_ALL, 0, &info, 0) == -E_INVAL);
    cprintf("waitid lifecycle and status test pass.\n");
    return 0;
}
