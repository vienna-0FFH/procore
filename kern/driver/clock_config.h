#ifndef __KERN_DRIVER_CLOCK_CONFIG_H__
#define __KERN_DRIVER_CLOCK_CONFIG_H__

/* Platform policy knobs. Override with KCFLAGS+=-DNAME=value when needed. */
#ifndef CLOCK_TICK_HZ
#define CLOCK_TICK_HZ                 100U
#endif
#define CLOCK_NSEC_PER_TICK            (1000000000U / CLOCK_TICK_HZ)
#ifndef CLOCK_RTC_CENTURY_REG
#define CLOCK_RTC_CENTURY_REG         0x32U
#endif
#ifndef CLOCK_RTC_DEFAULT_CENTURY
#define CLOCK_RTC_DEFAULT_CENTURY     20U
#endif
#ifndef CLOCK_RTC_EPOCH_YEAR
#define CLOCK_RTC_EPOCH_YEAR          1970U
#endif

#if CLOCK_TICK_HZ == 0
#error "CLOCK_TICK_HZ must be positive"
#endif
#if CLOCK_RTC_DEFAULT_CENTURY < 19 || CLOCK_RTC_DEFAULT_CENTURY > 99
#error "CLOCK_RTC_DEFAULT_CENTURY must be a two-digit century"
#endif

#endif /* !__KERN_DRIVER_CLOCK_CONFIG_H__ */
