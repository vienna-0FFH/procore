#ifndef __UCORE_MBEDTLS_TIME_H__
#define __UCORE_MBEDTLS_TIME_H__
#include <defs.h>
#include <string.h>
typedef intptr_t time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year; };
static inline time_t time(time_t *value) { time_t now = 0; if (value) *value = now; return now; }
static inline struct tm *gmtime_r(const time_t *value, struct tm *result) { (void)value; if (result) memset(result, 0, sizeof(*result)); return result; }
#endif
