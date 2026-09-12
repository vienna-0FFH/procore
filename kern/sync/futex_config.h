#ifndef __KERN_SYNC_FUTEX_CONFIG_H__
#define __KERN_SYNC_FUTEX_CONFIG_H__

/* Futex policy knobs. Override with KCFLAGS+=-DNAME=value. */
#ifndef FUTEX_HASH_BITS
#define FUTEX_HASH_BITS             6
#endif

#if FUTEX_HASH_BITS < 1 || FUTEX_HASH_BITS > 12
#error "FUTEX_HASH_BITS must be between 1 and 12"
#endif

#define FUTEX_HASH_SIZE             (1U << FUTEX_HASH_BITS)

#endif /* !__KERN_SYNC_FUTEX_CONFIG_H__ */
