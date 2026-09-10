#include <signal.h>
#include <syscall.h>

extern void __ucore_signal_trampoline(void);

int
kill_pid(int pid) {
    return sys_kill(pid, SIGTERM);
}

int
kill_signal(int pid, int signo) {
    return sys_kill(pid, signo);
}

int
raise(int signo) {
    return sys_raise(signo);
}

int
sigaction(int signo, const struct sigaction *action,
          struct sigaction *old_action) {
    struct sigaction local;
    if (action == NULL) {
        return sys_sigaction(signo, NULL, old_action);
    }
    local = *action;
    if (local.sa_handler != SIG_DFL && local.sa_handler != SIG_IGN &&
        local.sa_restorer == 0) {
        local.sa_restorer = (uintptr_t)__ucore_signal_trampoline;
    }
    return sys_sigaction(signo, &local, old_action);
}

int
sigprocmask(int how, const sigset_t *set, sigset_t *old_set) {
    return sys_sigprocmask(how, set, old_set);
}

int
sigreturn(void) {
    return sys_sigreturn();
}
