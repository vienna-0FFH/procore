#include <defs.h>
#include <error.h>
#include <wait.h>
#include <atomic.h>
#include <kmalloc.h>
#include <sem.h>
#include <proc.h>
#include <sync.h>
#include <assert.h>

void
sem_init(semaphore_t *sem, int value) {
    sem->value = value;
    wait_queue_init(&(sem->wait_queue));
    spin_init(&sem->lock);
}

static __noinline void __up(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    wait_t *wait;

    local_intr_save(intr_flag);
    spin_lock(&sem->lock);
    for (;;) {
        wait = wait_queue_pop(&(sem->wait_queue));
        if (wait == NULL) {
            sem->value++;
            break;
        }
        if (wait->proc->wait_state == wait_state ||
            wait->proc->wait_state == (wait_state | WT_INTERRUPTED)) {
            wait->wakeup_flags = wait->proc->wait_state;
            break;
        }
        /* A signal woke this waiter before it had removed its stack-local
         * wait node. Discard that stale queue node and continue so this
         * resource event reaches another valid waiter (or increments value). */
    }
    spin_unlock(&sem->lock);
    if (wait != NULL) {
        wakeup_proc(wait->proc);
    }
    local_intr_restore(intr_flag);
}

static __noinline uint32_t __down(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);
    spin_lock(&sem->lock);
    if (sem->value > 0) {
        sem->value --;
        spin_unlock(&sem->lock);
        local_intr_restore(intr_flag);
        return 0;
    }
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(sem->wait_queue), wait, wait_state);
    spin_unlock(&sem->lock);
    local_intr_restore(intr_flag);

    /* prepare_to_wait-style ordering: a signal can arrive after the caller
     * observed its condition but before this task became a visible waiter.
     * Once the wait state is published, check again before scheduling.  A
     * later signal sees WT_INTERRUPTED and wakes us through the normal path. */
    if ((wait_state & WT_INTERRUPTED) != 0 &&
        signal_should_interrupt(current)) {
        wakeup_proc(current);
        local_intr_save(intr_flag);
        wait_current_del(&(sem->wait_queue), wait);
        local_intr_restore(intr_flag);
        return WT_INTERRUPTED;
    }

    schedule();

    local_intr_save(intr_flag);
    wait_current_del(&(sem->wait_queue), wait);
    local_intr_restore(intr_flag);

    if (wait->wakeup_flags != wait_state) {
        return wait->wakeup_flags;
    }
    return 0;
}

void
up(semaphore_t *sem) {
    __up(sem, WT_KSEM);
}

/* Signal one condition change without accumulating an unbounded number of
 * tokens when no task is waiting.  The semaphore lock closes the race with
 * __down(): either the waiter is already visible and is woken, or it observes
 * the one event token before it can sleep. */
void
sem_wake_event(semaphore_t *sem) {
    bool intr_flag;
    wait_t *wait;

    if (sem == NULL) {
        return;
    }
    local_intr_save(intr_flag);
    spin_lock(&sem->lock);
    for (;;) {
        wait = wait_queue_pop(&sem->wait_queue);
        if (wait == NULL) {
            if (sem->value == 0) {
                sem->value = 1;
            }
            break;
        }
        if (wait->proc->wait_state == WT_KSEM ||
            wait->proc->wait_state == (WT_KSEM | WT_INTERRUPTED)) {
            wait->wakeup_flags = wait->proc->wait_state;
            break;
        }
    }
    spin_unlock(&sem->lock);
    local_intr_restore(intr_flag);
    if (wait != NULL) {
        wakeup_proc(wait->proc);
    }
}

/* Wake all tasks currently blocked on an event semaphore.  If the condition
 * changed just before a waiter became visible, preserve one bounded token so
 * that waiter rechecks the condition instead of sleeping through the event. */
void
sem_wake_all(semaphore_t *sem) {
    bool intr_flag;
    wait_t *wait;
    bool woke = 0;

    if (sem == NULL) {
        return;
    }
    for (;;) {
        local_intr_save(intr_flag);
        spin_lock(&sem->lock);
        wait = wait_queue_pop(&(sem->wait_queue));
        while (wait != NULL &&
               wait->proc->wait_state != WT_KSEM &&
               wait->proc->wait_state != (WT_KSEM | WT_INTERRUPTED)) {
            /* A signal woke this task before its stack-local wait node was
             * removed.  It cannot consume this event. */
            wait = wait_queue_pop(&(sem->wait_queue));
        }
        if (wait != NULL) {
            wait->wakeup_flags = wait->proc->wait_state;
        }
        else if (!woke && sem->value == 0) {
            /* Preserve the state transition for a waiter that starts after
             * the broadcast.  Condition users re-check their state after
             * consuming this bounded token. */
            sem->value = 1;
        }
        spin_unlock(&sem->lock);
        local_intr_restore(intr_flag);
        if (wait == NULL) {
            return;
        }
        wakeup_proc(wait->proc);
        woke = 1;
    }
}

void
down(semaphore_t *sem) {
    uint32_t flags = __down(sem, WT_KSEM);
    assert(flags == 0);
}

int
down_interruptible(semaphore_t *sem) {
    uint32_t flags = __down(sem, WT_KSEM | WT_INTERRUPTED);
    return flags == 0 ? 0 : -E_INTR;
}

bool
try_down(semaphore_t *sem) {
    bool intr_flag, ret = 0;
    local_intr_save(intr_flag);
    spin_lock(&sem->lock);
    if (sem->value > 0) {
        sem->value --, ret = 1;
    }
    spin_unlock(&sem->lock);
    local_intr_restore(intr_flag);
    return ret;
}
