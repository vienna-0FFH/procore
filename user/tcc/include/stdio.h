#ifndef __UCORE_TCC_STDIO_H__
#define __UCORE_TCC_STDIO_H__

#include <defs.h>
#include <stdarg.h>

typedef struct __ucore_tcc_FILE {
    int fd;
    int mode;
    int error;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *, const char *);
FILE *fdopen(int, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fgetc(FILE *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
char *fgets(char *, int, FILE *);
int fflush(FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int remove(const char *);
int rename(const char *, const char *);
int fileno(FILE *);
int puts(const char *);
int putchar(int);
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
void perror(const char *);

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
