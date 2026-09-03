#ifndef __KERN_SYNC_SPINLOCK_H__
#define __KERN_SYNC_SPINLOCK_H__

#include <defs.h>

/* A short non-sleeping lock for data shared by CPUs. */
typedef struct {
    volatile uint32_t locked;
} spinlock_t;

static inline void
spin_init(spinlock_t *lock) {
    lock->locked = 0;
}

static inline void
spin_lock(spinlock_t *lock) {
    uint32_t old;
    for (;;) {
        old = 1;
        asm volatile ("xchgl %0, %1"
                      : "=a" (old), "+m" (lock->locked)
                      : "0" (old)
                      : "memory");
        if (old == 0) {
            return;
        }
        while (lock->locked != 0) {
            asm volatile ("pause");
        }
    }
}

static inline void
spin_unlock(spinlock_t *lock) {
    asm volatile ("movl $0, %0" : "+m" (lock->locked) :: "memory");
}

#endif /* !__KERN_SYNC_SPINLOCK_H__ */
