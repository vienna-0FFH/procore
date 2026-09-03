#ifndef __KERN_VIRT_VIRT_H__
#define __KERN_VIRT_VIRT_H__

#ifndef VIRT_SELFTEST
#define VIRT_SELFTEST                 0
#endif

void virt_init(void);
void virt_selftest(void);

#endif /* !__KERN_VIRT_VIRT_H__ */
