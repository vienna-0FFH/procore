#ifndef __UCORE_TCC_STDLIB_H__
#define __UCORE_TCC_STDLIB_H__

#include <defs.h>

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void *alloca(size_t);
void exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atoi(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
int abs(int);
long labs(long);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(const char *);
char *strerror(int);
char *realpath(const char *, char *);

#endif
