#include <x86.h>
#include <trap.h>
#include <stdio.h>
#include <picirq.h>
#include <clock_config.h>

/* *
 * Support for time-related hardware gadgets - the 8253 timer,
 * which generates interruptes on IRQ-0.
 * */

#define IO_TIMER1           0x040               // 8253 Timer #1

/* *
 * Frequency of all three count-down timers; (TIMER_FREQ/freq)
 * is the appropriate count to generate a frequency of freq Hz.
 * */

#define TIMER_FREQ      1193182
#define TIMER_DIV(x)    ((TIMER_FREQ + (x) / 2) / (x))

#define TIMER_MODE      (IO_TIMER1 + 3)         // timer mode port
#define TIMER_SEL0      0x00                    // select counter 0
#define TIMER_RATEGEN   0x04                    // mode 2, rate generator
#define TIMER_16BIT     0x30                    // r/w counter 16 bits, LSB first

volatile size_t ticks;
static volatile uint64_t clock_ticks;
static volatile uint32_t clock_sequence;
static uint64_t clock_epoch;
static bool clock_epoch_valid;

long SYSTEM_READ_TIMER( void ){
    return ticks;
}

static uint8_t
clock_cmos_read(uint8_t reg) {
    outb(0x70, (uint8_t)(0x80U | reg));
    return inb(0x71);
}

static uint8_t
clock_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0FU) + ((value >> 4) * 10U));
}

static bool
clock_leap(uint32_t year) {
    return (year % 4U == 0U && year % 100U != 0U) ||
           (year % 400U == 0U);
}

static uint64_t
clock_days_before_year(uint32_t year) {
    uint32_t y = year - 1U;
    return (uint64_t)y * 365U + y / 4U - y / 100U + y / 400U;
}

static uint32_t
clock_days_before_month(uint32_t year, uint32_t month) {
    static const uint16_t days[] =
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint32_t result = days[month - 1U];
    if (month > 2U && clock_leap(year)) {
        result++;
    }
    return result;
}

static uint64_t
clock_calendar_to_epoch(uint32_t year, uint32_t month, uint32_t day,
                        uint32_t hour, uint32_t minute, uint32_t second) {
    uint64_t epoch_days;
    uint64_t date_days;

    if (year < CLOCK_RTC_EPOCH_YEAR || month < 1U || month > 12U ||
        day < 1U || day > 31U || hour > 23U || minute > 59U ||
        second > 59U) {
        return 0;
    }
    epoch_days = clock_days_before_year(CLOCK_RTC_EPOCH_YEAR);
    date_days = clock_days_before_year(year) +
                clock_days_before_month(year, month) + (uint64_t)day - 1U;
    if (date_days < epoch_days) {
        return 0;
    }
    return (date_days - epoch_days) * 86400U +
           (uint64_t)hour * 3600U + (uint64_t)minute * 60U + second;
}

static bool
clock_read_rtc(uint64_t *seconds_store) {
    uint8_t status_a, status_b;
    uint8_t sec, minute, hour, day, month, year, century;
    uint8_t sec2, minute2, hour2, day2, month2, year2, century2;
    uint32_t full_year;
    uint32_t tries;
    bool pm;

    if (seconds_store == NULL) {
        return 0;
    }
    for (tries = 0; tries < 8U; tries++) {
        status_a = clock_cmos_read(0x0AU);
        if ((status_a & 0x80U) != 0) {
            continue;
        }
        sec = clock_cmos_read(0x00U);
        minute = clock_cmos_read(0x02U);
        hour = clock_cmos_read(0x04U);
        day = clock_cmos_read(0x07U);
        month = clock_cmos_read(0x08U);
        year = clock_cmos_read(0x09U);
        century = clock_cmos_read((uint8_t)CLOCK_RTC_CENTURY_REG);
        status_b = clock_cmos_read(0x0BU);
        sec2 = clock_cmos_read(0x00U);
        minute2 = clock_cmos_read(0x02U);
        hour2 = clock_cmos_read(0x04U);
        day2 = clock_cmos_read(0x07U);
        month2 = clock_cmos_read(0x08U);
        year2 = clock_cmos_read(0x09U);
        century2 = clock_cmos_read((uint8_t)CLOCK_RTC_CENTURY_REG);
        if (sec != sec2 || minute != minute2 || hour != hour2 ||
            day != day2 || month != month2 || year != year2 ||
            century != century2) {
            continue;
        }
        pm = (hour & 0x80U) != 0;
        hour = (uint8_t)(hour & 0x7FU);
        if ((status_b & 0x04U) == 0) {
            sec = clock_bcd(sec);
            minute = clock_bcd(minute);
            hour = clock_bcd(hour);
            day = clock_bcd(day);
            month = clock_bcd(month);
            year = clock_bcd(year);
            century = clock_bcd(century);
        }
        if ((status_b & 0x02U) == 0) {
            if (hour == 12U) {
                hour = 0;
            }
            if (pm) {
                hour = (uint8_t)(hour + 12U);
            }
        }
        if (century < 19U || century > 99U) {
            century = CLOCK_RTC_DEFAULT_CENTURY;
        }
        full_year = (uint32_t)century * 100U + year;
        *seconds_store = clock_calendar_to_epoch(full_year, month, day,
                                                 hour, minute, sec);
        return *seconds_store != 0;
    }
    return 0;
}

void
clock_tick(void) {
    clock_sequence++;
    barrier();
    clock_ticks++;
    ticks = (size_t)clock_ticks;
    barrier();
    clock_sequence++;
}

uint64_t
clock_ticks_read(void) {
    uint32_t before, after;
    uint64_t value;
    do {
        before = clock_sequence;
        barrier();
        value = clock_ticks;
        barrier();
        after = clock_sequence;
    } while ((before & 1U) != 0 || before != after);
    return value;
}

uint64_t
clock_realtime_ticks_read(void) {
    uint64_t epoch = clock_epoch_valid ? clock_epoch : 0;
    return epoch * CLOCK_TICK_HZ + clock_ticks_read();
}

uint64_t
clock_realtime_seconds(void) {
    uint64_t epoch = clock_epoch_valid ? clock_epoch : 0;
    uint32_t uptime_ticks = (uint32_t)clock_ticks_read();
    if ((uint32_t)(clock_ticks_read() >> 32) != 0 ||
        epoch > 0x7FFFFFFFULL) {
        return 0x7FFFFFFFULL;
    }
    return epoch + uptime_ticks / CLOCK_TICK_HZ;
}

uint32_t
clock_tick_hz(void) {
    return CLOCK_TICK_HZ;
}

/* *
 * clock_init - initialize 8253 clock to interrupt 100 times per second,
 * and then enable IRQ_TIMER.
 * */
void
clock_init(void) {
    // set 8253 timer-chip
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_RATEGEN | TIMER_16BIT);
    outb(IO_TIMER1, TIMER_DIV(CLOCK_TICK_HZ) % 256);
    outb(IO_TIMER1, TIMER_DIV(CLOCK_TICK_HZ) / 256);

    // initialize time counter 'ticks' to zero
    ticks = 0;
    clock_ticks = 0;
    clock_sequence = 0;
    clock_epoch_valid = clock_read_rtc(&clock_epoch);
    if (clock_epoch_valid) {
        cprintf("clock: RTC epoch %llu seconds\n",
                (unsigned long long)clock_epoch);
    }
    else {
        cprintf("clock: RTC unavailable, realtime starts at boot\n");
    }

    cprintf("++ setup timer interrupts\n");
    pic_enable(IRQ_TIMER);
}
