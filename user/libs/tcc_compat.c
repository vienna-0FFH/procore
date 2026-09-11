/* Freestanding libc pieces used by the in-uCore TinyCC process. */
#include <defs.h>
#include <error.h>
#include <file.h>
#include <dir.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include <ulib.h>
#include <unistd.h>

int errno;

void *malloc(size_t);

char *
strdup(const char *src) {
    size_t len = strlen(src) + 1;
    char *dst = (char *)malloc(len);
    if (dst != NULL) memcpy(dst, src, len);
    return dst;
}

typedef struct tcc_mem_block {
    size_t size;
    struct tcc_mem_block *next;
    int free;
} tcc_mem_block_t;

static uintptr_t tcc_heap_end;
static tcc_mem_block_t *tcc_free_blocks;

static size_t
tcc_align(size_t value) {
    return (value + 7U) & ~7U;
}

static tcc_mem_block_t *
tcc_find_free(size_t size) {
    tcc_mem_block_t *block;
    for (block = tcc_free_blocks; block != NULL; block = block->next) {
        if (block->free && block->size >= size) {
            return block;
        }
    }
    return NULL;
}

void *
malloc(size_t size) {
    tcc_mem_block_t *block;
    uintptr_t old_end, new_end;

    if (size == 0) {
        size = 1;
    }
    size = tcc_align(size);
    block = tcc_find_free(size);
    if (block != NULL) {
        block->free = 0;
        return (void *)(block + 1);
    }

    if (tcc_heap_end == 0) {
        tcc_heap_end = brk(0);
    }
    old_end = tcc_heap_end;
    if (size > (uintptr_t)-1 - old_end - sizeof(*block)) {
        errno = 12;
        return NULL;
    }
    new_end = old_end + sizeof(*block) + size;
    if (brk(new_end) != new_end) {
        errno = 12;
        return NULL;
    }
    block = (tcc_mem_block_t *)old_end;
    block->size = size;
    block->free = 0;
    block->next = tcc_free_blocks;
    tcc_free_blocks = block;
    tcc_heap_end = new_end;
    return (void *)(block + 1);
}

void *
calloc(size_t count, size_t size) {
    size_t total;
    void *ptr;
    if (count != 0 && size > (uintptr_t)-1 / count) {
        errno = 12;
        return NULL;
    }
    total = count * size;
    ptr = malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void
free(void *ptr) {
    tcc_mem_block_t *block;
    if (ptr == NULL) {
        return;
    }
    block = ((tcc_mem_block_t *)ptr) - 1;
    block->free = 1;
}

void *
realloc(void *ptr, size_t size) {
    tcc_mem_block_t *block;
    void *new_ptr;
    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block = ((tcc_mem_block_t *)ptr) - 1;
    if (block->size >= size) {
        return ptr;
    }
    new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

static int
tcc_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static unsigned long long
tcc_parse_unsigned(const char *text, char **endptr, int base) {
    const char *cursor = text;
    unsigned long long value = 0;
    int digit;

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r' || *cursor == '\f' || *cursor == '\v') {
        cursor++;
    }
    if (base == 0) {
        base = 10;
        if (cursor[0] == '0') {
            base = 8;
            if (cursor[1] == 'x' || cursor[1] == 'X') {
                base = 16;
                cursor += 2;
            }
        }
    } else if (base == 16 && cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    while ((digit = tcc_digit(*cursor)) >= 0 && digit < base) {
        value = value * (unsigned)base + (unsigned)digit;
        cursor++;
    }
    if (endptr != NULL) *endptr = (char *)cursor;
    return value;
}

unsigned long
strtoul(const char *s, char **endptr, int base) {
    int negative = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
           *s == '\f' || *s == '\v') s++;
    if (*s == '+' || *s == '-') {
        negative = *s == '-';
        s++;
    }
    {
        unsigned long value = (unsigned long)tcc_parse_unsigned(s, endptr, base);
        return negative ? (unsigned long)(0U - value) : value;
    }
}

long long
strtoll(const char *s, char **endptr, int base) {
    int negative = 0;
    unsigned long long value;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
           *s == '\f' || *s == '\v') s++;
    if (*s == '+' || *s == '-') {
        negative = *s == '-';
        s++;
    }
    value = tcc_parse_unsigned(s, endptr, base);
    return negative ? -(long long)value : (long long)value;
}

unsigned long long
strtoull(const char *s, char **endptr, int base) {
    int negative = 0;
    unsigned long long value;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
           *s == '\f' || *s == '\v') s++;
    if (*s == '+' || *s == '-') {
        negative = *s == '-';
        s++;
    }
    value = tcc_parse_unsigned(s, endptr, base);
    return negative ? (0ULL - value) : value;
}

static double
tcc_pow10(int exponent) {
    double value = 1.0;
    int i;
    if (exponent > 308) exponent = 308;
    if (exponent < -308) exponent = -308;
    if (exponent >= 0) {
        for (i = 0; i < exponent; i++) value *= 10.0;
    } else {
        for (i = 0; i > exponent; i--) value /= 10.0;
    }
    return value;
}

double
strtod(const char *s, char **endptr) {
    int neg = 0, exponent = 0, exp_neg = 0, digit, any = 0;
    double value = 0.0, scale = 0.1;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '+' || *s == '-') {
        neg = *s == '-';
        s++;
    }
    while ((digit = tcc_digit(*s)) >= 0 && digit < 10) {
        value = value * 10.0 + digit;
        s++;
        any = 1;
    }
    if (*s == '.') {
        s++;
        while ((digit = tcc_digit(*s)) >= 0 && digit < 10) {
            value += digit * scale;
            scale *= 0.1;
            s++;
            any = 1;
        }
    }
    if (any && (*s == 'e' || *s == 'E')) {
        const char *before = s;
        s++;
        if (*s == '+' || *s == '-') {
            exp_neg = *s == '-';
            s++;
        }
        if (tcc_digit(*s) < 0 || tcc_digit(*s) > 9) {
            s = before;
        } else {
            while ((digit = tcc_digit(*s)) >= 0 && digit < 10) {
                exponent = exponent * 10 + digit;
                if (exponent > 400) exponent = 400;
                s++;
            }
            if (exp_neg) exponent = -exponent;
            value *= tcc_pow10(exponent);
        }
    }
    if (endptr != NULL) *endptr = (char *)(any ? s : (const char *)s);
    return neg ? -value : value;
}

float
strtof(const char *s, char **endptr) {
    return (float)strtod(s, endptr);
}

long double
strtold(const char *s, char **endptr) {
    return (long double)strtod(s, endptr);
}

int
atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}

int
abs(int value) {
    return value < 0 ? -value : value;
}

long
labs(long value) {
    return value < 0 ? -value : value;
}

void
qsort(void *base, size_t count, size_t size,
      int (*compare)(const void *, const void *)) {
    size_t i, j;
    unsigned char *bytes = (unsigned char *)base;
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (compare(bytes + i * size, bytes + j * size) > 0) {
                size_t k;
                for (k = 0; k < size; k++) {
                    unsigned char tmp = bytes[i * size + k];
                    bytes[i * size + k] = bytes[j * size + k];
                    bytes[j * size + k] = tmp;
                }
            }
        }
    }
}

double fabs(double value) { return value < 0 ? -value : value; }
double floor(double value) {
    long integer = (long)value;
    if (value < 0 && (double)integer != value) integer--;
    return (double)integer;
}
double ceil(double value) {
    long integer = (long)value;
    if (value > 0 && (double)integer != value) integer++;
    return (double)integer;
}
double ldexp(double value, int exponent) {
    if (exponent > 0) while (exponent--) value *= 2.0;
    else while (exponent++) value *= 0.5;
    return value;
}
long double ldexpl(long double value, int exponent) {
    if (exponent > 0) while (exponent--) value *= 2.0L;
    else while (exponent++) value *= 0.5L;
    return value;
}

char *
getenv(const char *name) {
    (void)name;
    return NULL;
}

char *
strerror(int error) {
    static char buffer[32];
    snprintf(buffer, sizeof(buffer), "uCore error %d", error);
    return buffer;
}

char *
realpath(const char *path, char *resolved) {
    if (resolved == NULL) resolved = (char *)malloc(strlen(path) + 1);
    if (resolved != NULL) strcpy(resolved, path);
    return resolved;
}

int
access(const char *path, int mode) {
    int fd;
    (void)mode;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

int
remove(const char *path) { return unlink(path); }

int
execvp(const char *file, char *const argv[]) {
    (void)file;
    (void)argv;
    errno = 38;
    return -1;
}

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
} tcc_glob_t;

int
glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
     tcc_glob_t *result) {
    int fd;
    (void)flags;
    (void)errfunc;
    result->gl_pathc = 0;
    result->gl_pathv = NULL;
    fd = open(pattern, O_RDONLY);
    if (fd < 0) return -1;
    close(fd);
    result->gl_pathv = (char **)malloc(2 * sizeof(char *));
    if (result->gl_pathv == NULL) return -1;
    result->gl_pathv[0] = strdup(pattern);
    result->gl_pathv[1] = NULL;
    result->gl_pathc = 1;
    return 0;
}

void
globfree(tcc_glob_t *result) {
    if (result == NULL) return;
    if (result->gl_pathv != NULL) {
        free(result->gl_pathv[0]);
        free(result->gl_pathv);
    }
    result->gl_pathc = 0;
    result->gl_pathv = NULL;
}

typedef struct {
    volatile int value;
} tcc_sem_t;

int sem_init(tcc_sem_t *sem, int shared, unsigned int value) {
    (void)shared;
    sem->value = (int)value;
    return 0;
}
int sem_destroy(tcc_sem_t *sem) { (void)sem; return 0; }
int sem_wait(tcc_sem_t *sem) {
    while (sem->value <= 0) yield();
    sem->value--;
    return 0;
}
int sem_post(tcc_sem_t *sem) { sem->value++; return 0; }

typedef int32_t tcc_time_t;
struct tcc_tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
};
struct tcc_tm *localtime(const tcc_time_t *value) {
    static struct tcc_tm zero;
    (void)value;
    memset(&zero, 0, sizeof(zero));
    return &zero;
}

void abort(void) { exit(-1); }
