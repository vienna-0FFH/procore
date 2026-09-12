#ifndef __KERN_SYNC_FUTEX_H__
#define __KERN_SYNC_FUTEX_H__

#include <defs.h>

struct mm_struct;
struct timespec;

void futex_init(void);
int futex_wait(struct mm_struct *mm, uintptr_t address, uint32_t expected,
               const struct timespec *timeout);
int futex_wake(struct mm_struct *mm, uintptr_t address, uint32_t count);

#endif /* !__KERN_SYNC_FUTEX_H__ */
