#ifndef __KERN_MM_CONFIG_H__
#define __KERN_MM_CONFIG_H__

/* Bounded reclaim policy.  Override with MM_DEFS+=-DPMM_SWAP_RETRY_LIMIT=N. */
#ifndef PMM_SWAP_RETRY_LIMIT
#define PMM_SWAP_RETRY_LIMIT          8U
#endif

#if PMM_SWAP_RETRY_LIMIT < 1
#error "PMM_SWAP_RETRY_LIMIT must be positive"
#endif

#endif /* !__KERN_MM_CONFIG_H__ */
