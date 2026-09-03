#include <defs.h>
#include <list.h>
#include <sync.h>
#include <wait.h>
#include <proc.h>

extern spinlock_t proc_lock;

void
wait_init(wait_t *wait, struct proc_struct *proc) {
    wait->proc = proc;
    wait->wakeup_flags = WT_INTERRUPTED;
    wait->wait_queue = NULL;
    list_init(&(wait->wait_link));
}

void
wait_queue_init(wait_queue_t *queue) {
    list_init(&(queue->wait_head));
    spin_init(&queue->lock);
}

void
wait_queue_add(wait_queue_t *queue, wait_t *wait) {
    spin_lock(&queue->lock);
    assert(list_empty(&(wait->wait_link)) && wait->proc != NULL);
    wait->wait_queue = queue;
    list_add_before(&(queue->wait_head), &(wait->wait_link));
    spin_unlock(&queue->lock);
}

void
wait_queue_del(wait_queue_t *queue, wait_t *wait) {
    spin_lock(&queue->lock);
    if (!list_empty(&(wait->wait_link)) && wait->wait_queue == queue) {
        list_del_init(&(wait->wait_link));
        wait->wait_queue = NULL;
    }
    spin_unlock(&queue->lock);
}

wait_t *
wait_queue_next(wait_queue_t *queue, wait_t *wait) {
    wait_t *next = NULL;
    list_entry_t *le;

    spin_lock(&queue->lock);
    if (wait != NULL && !list_empty(&(wait->wait_link)) &&
        wait->wait_queue == queue) {
        le = list_next(&(wait->wait_link));
        if (le != &(queue->wait_head)) {
            next = le2wait(le, wait_link);
        }
    }
    spin_unlock(&queue->lock);
    return next;
}

wait_t *
wait_queue_prev(wait_queue_t *queue, wait_t *wait) {
    wait_t *prev = NULL;
    list_entry_t *le;

    spin_lock(&queue->lock);
    if (wait != NULL && !list_empty(&(wait->wait_link)) &&
        wait->wait_queue == queue) {
        le = list_prev(&(wait->wait_link));
        if (le != &(queue->wait_head)) {
            prev = le2wait(le, wait_link);
        }
    }
    spin_unlock(&queue->lock);
    return prev;
}

wait_t *
wait_queue_first(wait_queue_t *queue) {
    wait_t *first = NULL;
    list_entry_t *le;

    spin_lock(&queue->lock);
    le = list_next(&(queue->wait_head));
    if (le != &(queue->wait_head)) {
        first = le2wait(le, wait_link);
    }
    spin_unlock(&queue->lock);
    return first;
}

wait_t *
wait_queue_last(wait_queue_t *queue) {
    wait_t *last = NULL;
    list_entry_t *le;

    spin_lock(&queue->lock);
    le = list_prev(&(queue->wait_head));
    if (le != &(queue->wait_head)) {
        last = le2wait(le, wait_link);
    }
    spin_unlock(&queue->lock);
    return last;
}

wait_t *
wait_queue_pop(wait_queue_t *queue) {
    wait_t *wait = NULL;
    list_entry_t *le;

    spin_lock(&queue->lock);
    le = list_next(&(queue->wait_head));
    if (le != &(queue->wait_head)) {
        wait = le2wait(le, wait_link);
        list_del_init(&(wait->wait_link));
        wait->wait_queue = NULL;
    }
    spin_unlock(&queue->lock);
    return wait;
}

bool
wait_queue_empty(wait_queue_t *queue) {
    bool empty;
    spin_lock(&queue->lock);
    empty = list_empty(&(queue->wait_head));
    spin_unlock(&queue->lock);
    return empty;
}

bool
wait_in_queue(wait_t *wait) {
    wait_queue_t *queue;
    bool in_queue = 0;

    if (wait == NULL) {
        return 0;
    }
    queue = wait->wait_queue;
    if (queue != NULL) {
        spin_lock(&queue->lock);
        in_queue = !list_empty(&(wait->wait_link)) &&
                   wait->wait_queue == queue;
        spin_unlock(&queue->lock);
    }
    return in_queue;
}

void
wakeup_wait(wait_queue_t *queue, wait_t *wait, uint32_t wakeup_flags, bool del) {
    if (queue == NULL || wait == NULL || wait->proc == NULL) {
        return;
    }
    if (del) {
        wait_queue_del(queue, wait);
    }
    wait->wakeup_flags = wakeup_flags;
    wakeup_proc(wait->proc);
}

void
wakeup_first(wait_queue_t *queue, uint32_t wakeup_flags, bool del) {
    wait_t *wait = del ? wait_queue_pop(queue) : wait_queue_first(queue);
    if (wait != NULL) {
        /* pop() already removed the entry; a non-destructive wake keeps it
         * linked for the waiter to remove after it resumes. */
        wakeup_wait(queue, wait, wakeup_flags, 0);
    }
}

void
wakeup_queue(wait_queue_t *queue, uint32_t wakeup_flags, bool del) {
    wait_t *wait;

    if (del) {
        while ((wait = wait_queue_pop(queue)) != NULL) {
            wait->wakeup_flags = wakeup_flags;
            wakeup_proc(wait->proc);
        }
        return;
    }

    /* Keep the wait entries linked, but wake every current waiter.  Save the
     * successor before waking because the waiter may remove itself as soon as
     * it runs on another CPU. */
    wait = wait_queue_first(queue);
    while (wait != NULL) {
        wait_t *next = wait_queue_next(queue, wait);
        if (wait->wait_queue != queue) {
            wait = next;
            continue;
        }
        wait->wakeup_flags = wakeup_flags;
        wakeup_proc(wait->proc);
        wait = next;
    }
}

void
wait_current_set(wait_queue_t *queue, wait_t *wait, uint32_t wait_state) {
    assert(current != NULL);
    wait_init(wait, current);
    spin_lock(&proc_lock);
    current->state = PROC_SLEEPING;
    current->wait_state = wait_state;
    spin_unlock(&proc_lock);
    wait_queue_add(queue, wait);
}
