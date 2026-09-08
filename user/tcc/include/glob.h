#ifndef __UCORE_TCC_GLOB_H__
#define __UCORE_TCC_GLOB_H__

#include <defs.h>

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
} glob_t;

#define GLOB_NOSORT 1

int glob(const char *, int, int (*)(const char *, int), glob_t *);
void globfree(glob_t *);

#endif
