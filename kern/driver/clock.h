#ifndef __KERN_DRIVER_CLOCK_H__
#define __KERN_DRIVER_CLOCK_H__

#include <defs.h>
#include <clock_config.h>

extern volatile size_t ticks;

void clock_init(void);
void clock_tick(void);
uint64_t clock_ticks_read(void);
uint64_t clock_realtime_ticks_read(void);
uint64_t clock_realtime_seconds(void);
uint32_t clock_uptime_seconds(void);
uint32_t clock_tick_hz(void);

long SYSTEM_READ_TIMER( void );


#endif /* !__KERN_DRIVER_CLOCK_H__ */
