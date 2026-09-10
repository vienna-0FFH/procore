#include <assert.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <sched.h>
#include <signal.h>
#include <smp.h>
#include <string.h>
#include <vmm.h>

static bool
signal_number_valid(int signo) {
    return signo > 0 && signo < UCORE_NSIG;
}

static uint32_t
signal_bit(int signo) {
    return (uint32_t)1U << (uint32_t)signo;
}

static bool
signal_uncatchable(int signo) {
    return signo == SIGKILL || signo == SIGSTOP;
}

static bool
signal_default_stops(int signo) {
    return signo == SIGSTOP || signo == SIGTSTP;
}

static bool signal_default_ignored(int signo);

static bool
signal_user_address(struct mm_struct *mm, uintptr_t address) {
    return mm != NULL && address >= USERBASE && address < USERTOP &&
           user_mem_check(mm, address, 1, 0);
}

void
signal_state_init(struct signal_state *state) {
    int signo;
    memset(state, 0, sizeof(*state));
    spin_init(&state->lock);
    for (signo = 0; signo < UCORE_NSIG; signo++) {
        state->action[signo].sa_handler = SIG_DFL;
        state->action[signo].sa_restorer = 0;
        state->action[signo].sa_flags = 0;
        state->action[signo].sa_mask = 0;
    }
}

void
signal_state_fork(struct signal_state *child,
                  const struct signal_state *parent) {
    signal_state_init(child);
    spin_lock((spinlock_t *)&parent->lock);
    memcpy(child->action, parent->action, sizeof(child->action));
    child->blocked = parent->blocked;
    spin_unlock((spinlock_t *)&parent->lock);
}

void
signal_state_exec(struct signal_state *state) {
    int signo;
    spin_lock(&state->lock);
    for (signo = 0; signo < UCORE_NSIG; signo++) {
        if (state->action[signo].sa_handler != SIG_IGN) {
            state->action[signo].sa_handler = SIG_DFL;
        }
        state->action[signo].sa_restorer = 0;
        state->action[signo].sa_flags = 0;
        state->action[signo].sa_mask = 0;
    }
    state->pending = 0;
    state->blocked = 0;
    state->in_handler = 0;
    state->frame = 0;
    spin_unlock(&state->lock);
}

int
signal_queue(struct proc_struct *proc, int signo) {
    if (proc == NULL || !signal_number_valid(signo)) {
        return -E_INVAL;
    }
    spin_lock(&proc->signal.lock);
    if (signo == SIGCONT) {
        proc->signal.pending &= ~signal_bit(SIGSTOP);
    }
    else if (signo == SIGSTOP) {
        proc->signal.pending &= ~signal_bit(SIGCONT);
    }
    proc->signal.pending |= signal_bit(signo);
    spin_unlock(&proc->signal.lock);
    return 0;
}

int
signal_get_action(struct proc_struct *proc, int signo,
                  struct sigaction *action) {
    if (proc == NULL || action == NULL || !signal_number_valid(signo)) {
        return -E_INVAL;
    }
    spin_lock(&proc->signal.lock);
    *action = proc->signal.action[signo];
    spin_unlock(&proc->signal.lock);
    return 0;
}

int
signal_set_action(struct proc_struct *proc, int signo,
                  const struct sigaction *action) {
    if (proc == NULL || action == NULL || !signal_number_valid(signo)) {
        return -E_INVAL;
    }
    if (signal_uncatchable(signo) &&
        action->sa_handler != SIG_DFL) {
        return -E_INVAL;
    }
    spin_lock(&proc->signal.lock);
    proc->signal.action[signo] = *action;
    spin_unlock(&proc->signal.lock);
    return 0;
}

int
signal_exchange_action(struct proc_struct *proc, int signo,
                       const struct sigaction *new_action,
                       struct sigaction *old_action) {
    if (proc == NULL || !signal_number_valid(signo) ||
        (new_action == NULL && old_action == NULL)) {
        return -E_INVAL;
    }
    if (new_action != NULL && signal_uncatchable(signo) &&
        new_action->sa_handler != SIG_DFL) {
        return -E_INVAL;
    }
    spin_lock(&proc->signal.lock);
    if (old_action != NULL) {
        *old_action = proc->signal.action[signo];
    }
    if (new_action != NULL) {
        proc->signal.action[signo] = *new_action;
    }
    spin_unlock(&proc->signal.lock);
    return 0;
}

int
signal_get_mask(struct proc_struct *proc, sigset_t *mask) {
    if (proc == NULL || mask == NULL) {
        return -E_INVAL;
    }
    spin_lock(&proc->signal.lock);
    *mask = proc->signal.blocked;
    spin_unlock(&proc->signal.lock);
    return 0;
}

int
signal_set_mask(struct proc_struct *proc, int how, sigset_t mask) {
    if (proc == NULL ||
        (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)) {
        return -E_INVAL;
    }
    mask &= ~signal_bit(SIGKILL);
    mask &= ~signal_bit(SIGSTOP);
    spin_lock(&proc->signal.lock);
    if (how == SIG_BLOCK) {
        proc->signal.blocked |= mask;
    }
    else if (how == SIG_UNBLOCK) {
        proc->signal.blocked &= ~mask;
    }
    else {
        proc->signal.blocked = mask;
    }
    spin_unlock(&proc->signal.lock);
    return 0;
}

bool
signal_should_interrupt(struct proc_struct *proc) {
    uint32_t ready;
    int signo;
    bool interrupt = 0;

    if (proc == NULL) {
        return 0;
    }
    spin_lock(&proc->signal.lock);
    ready = proc->signal.pending & ~proc->signal.blocked;
    ready |= proc->signal.pending &
             (signal_bit(SIGKILL) | signal_bit(SIGSTOP));
    for (signo = 1; signo < UCORE_NSIG; signo++) {
        if ((ready & signal_bit(signo)) == 0) {
            continue;
        }
        if (signal_uncatchable(signo) ||
            (proc->signal.action[signo].sa_handler != SIG_IGN &&
             !(proc->signal.action[signo].sa_handler == SIG_DFL &&
               signal_default_ignored(signo)))) {
            interrupt = 1;
            break;
        }
    }
    spin_unlock(&proc->signal.lock);
    return interrupt;
}

static int
signal_next_locked(struct signal_state *state) {
    uint32_t ready = state->pending & ~state->blocked;
    int signo;
    ready |= state->pending & (signal_bit(SIGKILL) | signal_bit(SIGSTOP));
    for (signo = 1; signo < UCORE_NSIG; signo++) {
        if ((ready & signal_bit(signo)) != 0) {
            return signo;
        }
    }
    return 0;
}

static bool
signal_default_ignored(int signo) {
    return signo == SIGCHLD || signo == SIGCONT;
}

static bool
signal_frame_valid(struct mm_struct *mm,
                   const struct ucore_signal_frame *frame,
                   uintptr_t frame_address) {
    const struct trapframe *tf = &frame->saved_tf;
    if (frame->magic != UCORE_SIGNAL_FRAME_MAGIC ||
        frame->saved_frame != frame_address ||
        tf->tf_cs != USER_CS || tf->tf_ss != USER_DS ||
        tf->tf_ds != USER_DS || tf->tf_es != USER_DS ||
        tf->tf_fs != USER_DS || tf->tf_gs != USER_DS ||
        (tf->tf_eflags & (FL_IOPL_MASK | FL_NT | FL_VM |
                          FL_VIF | FL_VIP)) != 0 ||
        (tf->tf_eflags & 0x2U) == 0 ||
        (tf->tf_eflags & FL_IF) == 0) {
        return 0;
    }
    return signal_user_address(mm, tf->tf_eip) &&
           tf->tf_esp >= USERBASE && tf->tf_esp <= USERTOP;
}

int
signal_deliver(struct trapframe *tf) {
    struct mm_struct *mm;
    struct sigaction action;
    struct ucore_signal_frame frame;
    uint32_t stack_words[2];
    uintptr_t handler_sp, frame_address;
    uint32_t old_mask;
    int signo;

    if (current == NULL || tf == NULL || trap_in_kernel(tf) ||
        current->mm == NULL) {
        return 0;
    }
    mm = current->mm;
    for (;;) {
        spin_lock(&current->signal.lock);
        if ((current->signal.in_handler != 0 &&
             (current->signal.pending &
              (signal_bit(SIGKILL) | signal_bit(SIGSTOP))) == 0) ||
            (signo = signal_next_locked(&current->signal)) == 0) {
            spin_unlock(&current->signal.lock);
            return 0;
        }
        action = current->signal.action[signo];
        current->signal.pending &= ~signal_bit(signo);
        if (action.sa_handler == SIG_IGN ||
            (action.sa_handler == SIG_DFL && signal_default_ignored(signo))) {
            spin_unlock(&current->signal.lock);
            continue;
        }
        if (signal_default_stops(signo) && action.sa_handler == SIG_DFL) {
            bool intr_flag;
            spin_unlock(&current->signal.lock);
            local_intr_save(intr_flag);
            spin_lock(&proc_lock);
            if (current->state != PROC_ZOMBIE) {
                sched_stop_locked(current);
                current->need_resched = 1;
            }
            spin_unlock(&proc_lock);
            local_intr_restore(intr_flag);
            return 1;
        }
        if (action.sa_handler == SIG_DFL) {
            spin_unlock(&current->signal.lock);
            current->flags |= PF_EXITING;
            return 1;
        }
        old_mask = current->signal.blocked;
        current->signal.in_handler = (uint32_t)signo;
        current->signal.blocked |= action.sa_mask | signal_bit(signo);
        spin_unlock(&current->signal.lock);

        if (tf->tf_esp < USERBASE + sizeof(frame) + 8) {
            spin_lock(&current->signal.lock);
            current->signal.blocked = old_mask;
            current->signal.in_handler = 0;
            current->signal.frame = 0;
            spin_unlock(&current->signal.lock);
            current->flags |= PF_EXITING;
            return 1;
        }
        handler_sp = ROUNDDOWN(tf->tf_esp - sizeof(frame) - 8, 16);
        frame_address = handler_sp + 8;
        memset(&frame, 0, sizeof(frame));
        frame.magic = UCORE_SIGNAL_FRAME_MAGIC;
        frame.signo = signo;
        frame.saved_mask = old_mask;
        frame.saved_frame = frame_address;
        frame.saved_tf = *tf;
        frame.restorer = action.sa_restorer;
        stack_words[0] = (uint32_t)action.sa_restorer;
        stack_words[1] = (uint32_t)signo;

        lock_mm(mm);
        if (action.sa_restorer == 0 ||
            !signal_user_address(mm, action.sa_handler) ||
            !signal_user_address(mm, action.sa_restorer) ||
            !user_mem_check(mm, handler_sp, sizeof(stack_words), 1) ||
            !user_mem_check(mm, frame_address, sizeof(frame), 1) ||
            !copy_to_user(mm, (void *)handler_sp, stack_words,
                          sizeof(stack_words)) ||
            !copy_to_user(mm, (void *)frame_address, &frame,
                          sizeof(frame))) {
            unlock_mm(mm);
            spin_lock(&current->signal.lock);
            current->signal.blocked = old_mask;
            current->signal.in_handler = 0;
            current->signal.frame = 0;
            spin_unlock(&current->signal.lock);
            current->flags |= PF_EXITING;
            return 1;
        }
        unlock_mm(mm);
        spin_lock(&current->signal.lock);
        current->signal.frame = frame_address;
        spin_unlock(&current->signal.lock);
        tf->tf_esp = handler_sp;
        tf->tf_eip = action.sa_handler;
        tf->tf_regs.reg_eax = 0;
        return 1;
    }
}

int
signal_sigreturn(void) {
    struct mm_struct *mm = current->mm;
    struct ucore_signal_frame frame;
    uintptr_t frame_address;

    if (mm == NULL) {
        return -E_INVAL;
    }
    spin_lock(&current->signal.lock);
    frame_address = current->signal.frame;
    if (current->signal.in_handler == 0 || frame_address == 0) {
        spin_unlock(&current->signal.lock);
        return -E_INVAL;
    }
    spin_unlock(&current->signal.lock);
    lock_mm(mm);
    if (!user_mem_check(mm, frame_address, sizeof(frame), 0) ||
        !copy_from_user(mm, &frame, (void *)frame_address,
                        sizeof(frame), 0) ||
        !signal_frame_valid(mm, &frame, frame_address)) {
        unlock_mm(mm);
        current->flags |= PF_EXITING;
        return -E_KILLED;
    }
    unlock_mm(mm);
    spin_lock(&current->signal.lock);
    if (current->signal.frame != frame_address ||
        current->signal.in_handler == 0) {
        spin_unlock(&current->signal.lock);
        return -E_INVAL;
    }
    current->signal.blocked = frame.saved_mask &
                              ~(signal_bit(SIGKILL) | signal_bit(SIGSTOP));
    current->signal.frame = 0;
    current->signal.in_handler = 0;
    spin_unlock(&current->signal.lock);
    *current->tf = frame.saved_tf;
    return 0;
}
