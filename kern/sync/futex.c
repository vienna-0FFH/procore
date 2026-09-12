#include <defs.h>
#include <error.h>
#include <clock.h>
#include <futex.h>
#include <futex_config.h>
#include <list.h>
#include <mmu.h>
#include <proc.h>
#include <sched.h>
#include <stdlib.h>
#include <sync.h>
#include <unistd.h>
#include <vmm.h>
#include <wait.h>

struct futex_waiter {
    wait_t wait;
    struct mm_struct *mm;
    uintptr_t address;
};

static wait_queue_t futex_buckets[FUTEX_HASH_SIZE];
static bool futex_ready;

static unsigned int
futex_hash(struct mm_struct *mm, uintptr_t address) {
    uint32_t value = (uint32_t)(address >> 2);
    value ^= (uint32_t)(address >> 16);
    value ^= (uint32_t)(uintptr_t)mm;
    return hash32(value, FUTEX_HASH_BITS);
}

static bool
futex_address_valid(struct mm_struct *mm, uintptr_t address) {
    return mm != NULL && (address & (sizeof(uint32_t) - 1)) == 0 &&
           address >= USERBASE && address <= USERTOP - sizeof(uint32_t) &&
           user_mem_check(mm, address, sizeof(uint32_t), 0);
}

static int
futex_timeout_ticks(struct mm_struct *mm, const struct timespec *timeout,
                    uint32_t *ticks_store) {
    struct timespec request;
    uint64_t ticks;
    uint32_t fractional;

    *ticks_store = 0;
    if (timeout == NULL) {
        return 0;
    }
    if (!copy_from_user(mm, &request, timeout, sizeof(request), 0) ||
        request.tv_sec < 0 || request.tv_nsec < 0 ||
        request.tv_nsec >= 1000000000) {
        return -E_INVAL;
    }
    if ((uint32_t)request.tv_sec > 0xFFFFFFFFU / CLOCK_TICK_HZ) {
        return -E_TOO_BIG;
    }
    ticks = (uint64_t)(uint32_t)request.tv_sec * CLOCK_TICK_HZ;
    fractional = (uint32_t)request.tv_nsec;
    fractional = (fractional + CLOCK_NSEC_PER_TICK - 1U) /
                 CLOCK_NSEC_PER_TICK;
    ticks += fractional;
    if (ticks > 0xFFFFFFFFU) {
        return -E_TOO_BIG;
    }
    *ticks_store = (uint32_t)ticks;
    return 0;
}

void
futex_init(void) {
    unsigned int i;
    for (i = 0; i < FUTEX_HASH_SIZE; i++) {
        wait_queue_init(&futex_buckets[i]);
    }
    futex_ready = 1;
}

int
futex_wait(struct mm_struct *mm, uintptr_t address, uint32_t expected,
           const struct timespec *timeout) {
    wait_queue_t *bucket;
    struct futex_waiter waiter;
    uint32_t timeout_ticks;
    uint32_t observed;
    uint64_t start_ticks;
    timer_t timer;
    bool have_timer = 0;
    bool intr_flag;
    int ret;

    if (!futex_ready || mm == NULL) {
        return -E_INVAL;
    }
    lock_mm(mm);
    if (!futex_address_valid(mm, address)) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    ret = futex_timeout_ticks(mm, timeout, &timeout_ticks);
    if (ret != 0) {
        unlock_mm(mm);
        return ret;
    }
    if (signal_should_interrupt(current)) {
        unlock_mm(mm);
        return -E_INTR;
    }

    bucket = &futex_buckets[futex_hash(mm, address)];
    wait_init(&waiter.wait, current);
    waiter.mm = mm;
    waiter.address = address;

    /* Hold the address-space lock while publishing the waiter.  The
     * proc-lock then bucket-lock order matches wait_current_set(), and the
     * value is checked again after both locks are held so a wake cannot be
     * lost between the comparison and queue insertion. */
    local_intr_save(intr_flag);
    spin_lock(&proc_lock);
    spin_lock(&bucket->lock);
    if (!copy_from_user(mm, &observed, (const void *)address,
                        sizeof(observed), 0)) {
        spin_unlock(&bucket->lock);
        spin_unlock(&proc_lock);
        local_intr_restore(intr_flag);
        unlock_mm(mm);
        return -E_INVAL;
    }
    if (observed != expected) {
        spin_unlock(&bucket->lock);
        spin_unlock(&proc_lock);
        local_intr_restore(intr_flag);
        unlock_mm(mm);
        return -E_AGAIN;
    }
    if (timeout != NULL && timeout_ticks == 0) {
        spin_unlock(&bucket->lock);
        spin_unlock(&proc_lock);
        local_intr_restore(intr_flag);
        unlock_mm(mm);
        return -E_TIMEOUT;
    }
    waiter.wait.wait_queue = bucket;
    list_add_before(&bucket->wait_head, &waiter.wait.wait_link);
    current->state = PROC_SLEEPING;
    current->wait_state = WT_FUTEX;
    spin_unlock(&bucket->lock);
    spin_unlock(&proc_lock);
    local_intr_restore(intr_flag);
    unlock_mm(mm);

    start_ticks = clock_ticks_read();
    if (timeout != NULL) {
        timer_init(&timer, current, timeout_ticks);
        add_timer(&timer);
        have_timer = 1;
    }

    /* A signal can become pending after the initial check but before the
     * scheduler runs.  Turn the newly sleeping task runnable and let the
     * normal cleanup below remove its stack-local waiter. */
    if (signal_should_interrupt(current)) {
        wakeup_proc(current);
    }
    else {
        schedule();
    }

    if (have_timer) {
        del_timer(&timer);
    }
    if (wait_in_queue(&waiter.wait)) {
        wait_queue_del(bucket, &waiter.wait);
    }
    if (signal_should_interrupt(current)) {
        return -E_INTR;
    }
    if (waiter.wait.wakeup_flags == WT_FUTEX) {
        return 0;
    }
    if (timeout != NULL &&
        clock_ticks_read() - start_ticks >= timeout_ticks) {
        return -E_TIMEOUT;
    }
    return 0;
}

int
futex_wake(struct mm_struct *mm, uintptr_t address, uint32_t count) {
    wait_queue_t *bucket;
    uint32_t woken = 0;

    if (!futex_ready || mm == NULL) {
        return -E_INVAL;
    }
    lock_mm(mm);
    if (!futex_address_valid(mm, address)) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    if (count == 0) {
        return 0;
    }
    bucket = &futex_buckets[futex_hash(mm, address)];
    while (woken < count) {
        struct futex_waiter *found = NULL;
        struct proc_struct *proc = NULL;
        list_entry_t *le;
        bool intr_flag;

        local_intr_save(intr_flag);
        spin_lock(&bucket->lock);
        le = list_next(&bucket->wait_head);
        while (le != &bucket->wait_head) {
            struct futex_waiter *candidate =
                to_struct(le, struct futex_waiter, wait.wait_link);
            le = list_next(le);
            if (candidate->mm == mm && candidate->address == address) {
                found = candidate;
                list_del_init(&found->wait.wait_link);
                found->wait.wait_queue = NULL;
                found->wait.wakeup_flags = WT_FUTEX;
                proc = found->wait.proc;
                break;
            }
        }
        spin_unlock(&bucket->lock);
        local_intr_restore(intr_flag);
        if (found == NULL) {
            break;
        }
        wakeup_proc(proc);
        woken++;
    }
    return (int)woken;
}
