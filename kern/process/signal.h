#ifndef __KERN_PROCESS_SIGNAL_H__
#define __KERN_PROCESS_SIGNAL_H__

#include <defs.h>
#include <spinlock.h>
#include <trap.h>
#include <unistd.h>

#define UCORE_SIGNAL_FRAME_MAGIC 0x53494746U

struct proc_struct;

struct signal_state {
    struct sigaction action[UCORE_NSIG];
    uint32_t pending;
    uint32_t blocked;
    uint32_t in_handler;
    uintptr_t frame;
    spinlock_t lock;
};

struct ucore_signal_frame {
    uint32_t magic;
    int32_t signo;
    uint32_t saved_mask;
    uintptr_t saved_frame;
    struct trapframe saved_tf;
    uintptr_t restorer;
};

void signal_state_init(struct signal_state *state);
void signal_state_fork(struct signal_state *child,
                       const struct signal_state *parent);
void signal_state_exec(struct signal_state *state);
int signal_queue(struct proc_struct *proc, int signo);
int signal_deliver(struct trapframe *tf);
int signal_sigreturn(void);
int signal_get_action(struct proc_struct *proc, int signo,
                      struct sigaction *action);
int signal_set_action(struct proc_struct *proc, int signo,
                      const struct sigaction *action);
int signal_exchange_action(struct proc_struct *proc, int signo,
                           const struct sigaction *new_action,
                           struct sigaction *old_action);
int signal_get_mask(struct proc_struct *proc, sigset_t *mask);
int signal_set_mask(struct proc_struct *proc, int how, sigset_t mask);
bool signal_should_interrupt(struct proc_struct *proc);

#endif /* !__KERN_PROCESS_SIGNAL_H__ */
