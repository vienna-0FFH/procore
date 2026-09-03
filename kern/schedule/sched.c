#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <smp.h>
#include <stdio.h>
#include <assert.h>
#include <default_sched.h>

extern spinlock_t proc_lock;

static struct sched_class *sched_class;
static struct run_queue sched_rq[SMP_MAX_CPUS];
static int sched_ncpu;
static list_entry_t timer_list;
static spinlock_t timer_lock;

static inline int
sched_cpu_index(void) {
    int cpu = smp_current_cpu();
    if (cpu < 0 || cpu >= sched_ncpu) {
        cpu = 0;
    }
    return cpu;
}

static inline bool
sched_is_idle(struct proc_struct *proc) {
    return proc != NULL && proc->pid == 0;
}

static inline void
sched_class_enqueue_locked(struct run_queue *rq, struct proc_struct *proc) {
    if (!sched_is_idle(proc) && proc->rq == NULL && !proc->on_rq) {
        sched_class->enqueue(rq, proc);
        proc->rq = rq;
        proc->on_rq = 1;
        proc->cpu = rq->cpu_id;
    }
}

static inline void
sched_class_dequeue_locked(struct run_queue *rq, struct proc_struct *proc) {
    sched_class->dequeue(rq, proc);
    proc->rq = NULL;
    proc->on_rq = 0;
}

static int
sched_choose_cpu(struct proc_struct *proc) {
    int i, best = 0;
    unsigned int best_load = ~0U;

    if (sched_ncpu <= 1) {
        return 0;
    }
    for (i = 0; i < sched_ncpu; i++) {
        unsigned int load;
        spin_lock(&sched_rq[i].lock);
        load = sched_rq[i].proc_num;
        spin_unlock(&sched_rq[i].lock);
        if (load < best_load || (load == best_load && proc->cpu == i)) {
            best = i;
            best_load = load;
        }
    }
    return best;
}

static void
sched_enqueue_proc(struct proc_struct *proc) {
    int cpu;
    struct run_queue *rq;
    bool queued = 0;

    if (proc == NULL || sched_ncpu <= 0 || sched_is_idle(proc)) {
        return;
    }
    spin_lock(&proc_lock);
    cpu = sched_choose_cpu(proc);
    rq = &sched_rq[cpu];
    spin_lock(&rq->lock);
    if (!proc->on_cpu && !proc->on_rq && proc->rq == NULL &&
        proc->state == PROC_RUNNABLE) {
        sched_class_enqueue_locked(rq, proc);
        queued = 1;
    }
    spin_unlock(&rq->lock);
    spin_unlock(&proc_lock);
    if (queued && cpu != smp_current_cpu()) {
        smp_send_reschedule_cpu(cpu);
    }
}

static struct proc_struct *
sched_steal(int cpu) {
    int i;
    struct proc_struct *next = NULL;

    for (i = 1; i < sched_ncpu; i++) {
        int victim = (cpu + i) % sched_ncpu;
        struct run_queue *rq = &sched_rq[victim];
        spin_lock(&rq->lock);
        if (rq->proc_num != 0) {
            next = sched_class->pick_next(rq);
            if (next != NULL) {
                sched_class_dequeue_locked(rq, next);
                next->cpu = cpu;
            }
        }
        spin_unlock(&rq->lock);
        if (next != NULL) {
            break;
        }
    }
    return next;
}

void
sched_init(void) {
    int i;

    sched_class = &default_sched_class;
    sched_ncpu = smp_cpu_count();
    if (sched_ncpu < 1) {
        sched_ncpu = 1;
    }
    if (sched_ncpu > SMP_MAX_CPUS) {
        sched_ncpu = SMP_MAX_CPUS;
    }
    list_init(&timer_list);
    spin_init(&timer_lock);
    for (i = 0; i < sched_ncpu; i++) {
        sched_rq[i].cpu_id = i;
        sched_rq[i].max_time_slice = 5;
        spin_init(&sched_rq[i].lock);
        sched_class->init(&sched_rq[i]);
    }
    cprintf("sched class: %s (%d CPU run queues)\n",
            sched_class->name, sched_ncpu);
}

void
wakeup_proc(struct proc_struct *proc) {
    bool enqueue = 0;
    bool intr_flag;

    if (proc == NULL || sched_is_idle(proc)) {
        return;
    }
    local_intr_save(intr_flag);
    spin_lock(&proc_lock);
    if (proc->state != PROC_RUNNABLE && proc->state != PROC_ZOMBIE) {
        proc->state = PROC_RUNNABLE;
        proc->wait_state = 0;
        enqueue = 1;
    }
    else if (proc->state == PROC_RUNNABLE && !proc->on_cpu &&
             !proc->on_rq && proc->rq == NULL) {
        /* A caller may have changed the state while holding proc_lock.
         * Ensure that this runnable process still reaches a run queue. */
        enqueue = 1;
    }
    spin_unlock(&proc_lock);
    if (enqueue) {
        sched_enqueue_proc(proc);
    }
    local_intr_restore(intr_flag);
}

void
schedule(void) {
    bool intr_flag;
    int cpu;
    struct run_queue *rq;
    struct proc_struct *prev, *next = NULL;

    local_intr_save(intr_flag);
    cpu = sched_cpu_index();
    rq = &sched_rq[cpu];
    prev = current;

    spin_lock(&proc_lock);
    spin_lock(&rq->lock);
    if (prev != NULL) {
        prev->need_resched = 0;
        if (prev->state == PROC_RUNNABLE && !sched_is_idle(prev) &&
            prev->rq == NULL && !prev->on_rq) {
            sched_class_enqueue_locked(rq, prev);
        }
        if (rq->proc_num != 0) {
            next = sched_class->pick_next(rq);
            if (next != NULL) {
                sched_class_dequeue_locked(rq, next);
            }
        }
    }
    spin_unlock(&rq->lock);

    if (next == NULL) {
        next = sched_steal(cpu);
    }
    if (next == NULL) {
        next = idleproc;
    }
    if (next != NULL) {
        next->cpu = cpu;
        next->runs++;
        next->on_cpu = 1;
    }
    spin_unlock(&proc_lock);

    if (next != NULL && next != prev) {
        proc_run(next);
    }
    local_intr_restore(intr_flag);
}

void
sched_tick(void) {
    struct run_queue *rq;
    bool intr_flag;

    if (sched_ncpu <= 0 || current == NULL) {
        return;
    }
    local_intr_save(intr_flag);
    rq = &sched_rq[sched_cpu_index()];
    spin_lock(&rq->lock);
    sched_class->proc_tick(rq, current);
    spin_unlock(&rq->lock);
    local_intr_restore(intr_flag);
}

void
sched_cpu_idle(void) {
    for (;;) {
        /* A switch into the idle context has no proc_run() continuation;
         * retire the previous CPU owner here before servicing work. */
        proc_switch_complete();
        if (current == NULL) {
            current = idleproc;
        }
        if (current != NULL && current->need_resched) {
            schedule();
        }
        asm volatile ("sti; hlt" ::: "memory");
    }
}

void
add_timer(timer_t *timer) {
    bool intr_flag;

    local_intr_save(intr_flag);
    assert(timer->expires > 0 && timer->proc != NULL);
    assert(list_empty(&(timer->timer_link)));
    spin_lock(&timer_lock);
    {
        list_entry_t *le = list_next(&timer_list);
        while (le != &timer_list) {
            timer_t *next = le2timer(le, timer_link);
            if (timer->expires < next->expires) {
                next->expires -= timer->expires;
                break;
            }
            timer->expires -= next->expires;
            le = list_next(le);
        }
        list_add_before(le, &(timer->timer_link));
    }
    spin_unlock(&timer_lock);
    local_intr_restore(intr_flag);
}

void
del_timer(timer_t *timer) {
    bool intr_flag;

    local_intr_save(intr_flag);
    spin_lock(&timer_lock);
    if (!list_empty(&(timer->timer_link))) {
        if (timer->expires != 0) {
            list_entry_t *le = list_next(&(timer->timer_link));
            if (le != &timer_list) {
                timer_t *next = le2timer(le, timer_link);
                next->expires += timer->expires;
            }
        }
        list_del_init(&(timer->timer_link));
    }
    spin_unlock(&timer_lock);
    local_intr_restore(intr_flag);
}

void
run_timer_list(void) {
    for (;;) {
        timer_t *timer;
        struct proc_struct *proc = NULL;
        bool expired = 0;
        bool intr_flag;

        local_intr_save(intr_flag);
        spin_lock(&timer_lock);
        if (!list_empty(&timer_list)) {
            timer = le2timer(list_next(&timer_list), timer_link);
            if (timer->expires != 0) {
                timer->expires--;
            }
            if (timer->expires == 0) {
                list_del_init(&(timer->timer_link));
                proc = timer->proc;
                expired = 1;
            }
        }
        spin_unlock(&timer_lock);
        local_intr_restore(intr_flag);

        if (!expired) {
            break;
        }
        if (proc->wait_state != 0) {
            assert(proc->wait_state & WT_INTERRUPTED);
        }
        else {
            warn("process %d's wait_state == 0.\n", proc->pid);
        }
        wakeup_proc(proc);
    }
    sched_tick();
}
