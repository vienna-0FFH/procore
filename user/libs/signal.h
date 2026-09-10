#ifndef __USER_LIBS_SIGNAL_H__
#define __USER_LIBS_SIGNAL_H__

#include <unistd.h>

int kill_pid(int pid);
int kill_signal(int pid, int signo);
int raise(int signo);
int sigaction(int signo, const struct sigaction *action,
              struct sigaction *old_action);
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set);
int sigreturn(void);

/* Keep old one-argument uCore programs source-compatible while accepting the
 * POSIX-shaped kill(pid, signo) spelling for new programs. */
#define __UCORE_KILL_SELECT(_pid, _sig, name, ...) name
#define kill(...) __UCORE_KILL_SELECT(__VA_ARGS__, kill_signal, kill_pid)(__VA_ARGS__)

#endif /* !__USER_LIBS_SIGNAL_H__ */
