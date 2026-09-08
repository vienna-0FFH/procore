#include <defs.h>
#include <stdio.h>
#include <syscall.h>
#include <file.h>
#include <dir.h>
#include <ulib.h>
#include <unistd.h>
#include <string.h>

#ifndef EOF
#define EOF (-1)
#endif

typedef struct {
    int fd;
    int mode;
    int error;
} tcc_file_t;

extern void *malloc(size_t);
extern void free(void *);

static tcc_file_t tcc_stdin = { 0, O_RDONLY, 0 };
static tcc_file_t tcc_stdout = { 1, O_WRONLY, 0 };
static tcc_file_t tcc_stderr = { 2, O_WRONLY, 0 };
tcc_file_t *stdin = &tcc_stdin;
tcc_file_t *stdout = &tcc_stdout;
tcc_file_t *stderr = &tcc_stderr;

/* *
 * cputch - writes a single character @c to stdout, and it will
 * increace the value of counter pointed by @cnt.
 * */
static void
cputch(int c, int *cnt) {
    sys_putc(c);
    (*cnt) ++;
}

/* *
 * vcprintf - format a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want cprintf() instead.
 * */
int
vcprintf(const char *fmt, va_list ap) {
    int cnt = 0;
    vprintfmt((void*)cputch, NO_FD, &cnt, fmt, ap);
    return cnt;
}

/* *
 * cprintf - formats a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int
cprintf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    int cnt = vcprintf(fmt, ap);
    va_end(ap);

    return cnt;
}

/* *
 * cputs- writes the string pointed by @str to stdout and
 * appends a newline character.
 * */
int
cputs(const char *str) {
    int cnt = 0;
    char c;
    while ((c = *str ++) != '\0') {
        cputch(c, &cnt);
    }
    cputch('\n', &cnt);
    return cnt;
}


static void
fputch(char c, int *cnt, int fd) {
    write(fd, &c, sizeof(char));
    (*cnt) ++;
}

int
vfprintf(int stream_or_fd, const char *fmt, va_list ap) {
    int fd = stream_or_fd;
    int cnt = 0;
    if ((uintptr_t)(uint32_t)stream_or_fd > 2U) {
        fd = ((tcc_file_t *)(uintptr_t)(uint32_t)stream_or_fd)->fd;
    }
    vprintfmt((void*)fputch, fd, &cnt, fmt, ap);
    return cnt;
}

int
fprintf(int stream_or_fd, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    int cnt = vfprintf(stream_or_fd, fmt, ap);
    va_end(ap);

    return cnt;
}

int
printf(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vfprintf((int)(uintptr_t)stdout, fmt, ap);
    va_end(ap);
    return cnt;
}

static int
tcc_mode_flags(const char *mode) {
    int flags = O_RDONLY;
    if (mode != NULL && (*mode == 'w' || *mode == 'a')) {
        flags = O_WRONLY | O_CREAT;
        if (*mode == 'w') flags |= O_TRUNC;
        if (*mode == 'a') flags |= O_APPEND;
    } else if (mode != NULL && *mode == 'r' && mode[1] == '+') {
        flags = O_RDWR;
    }
    return flags;
}

void *
fopen(const char *path, const char *mode) {
    tcc_file_t *stream = (tcc_file_t *)malloc(sizeof(*stream));
    int fd;
    if (stream == NULL) return NULL;
    fd = open(path, tcc_mode_flags(mode));
    if (fd < 0) {
        free(stream);
        return NULL;
    }
    stream->fd = fd;
    stream->mode = tcc_mode_flags(mode);
    stream->error = 0;
    return stream;
}

void *
fdopen(int fd, const char *mode) {
    tcc_file_t *stream = (tcc_file_t *)malloc(sizeof(*stream));
    if (stream == NULL) return NULL;
    stream->fd = fd;
    stream->mode = tcc_mode_flags(mode);
    stream->error = 0;
    return stream;
}

int
fclose(void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    int ret;
    if (stream == NULL) return -1;
    if (stream == stdin || stream == stdout || stream == stderr) return 0;
    ret = close(stream->fd);
    free(stream);
    return ret;
}

size_t
fread(void *buffer, size_t size, size_t count, void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    size_t total = size * count;
    int ret;
    if (stream == NULL || size == 0) return 0;
    ret = read(stream->fd, buffer, total);
    if (ret < 0) {
        stream->error = ret;
        return 0;
    }
    return (size_t)ret / size;
}

size_t
fwrite(const void *buffer, size_t size, size_t count, void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    size_t total = size * count;
    int ret;
    if (stream == NULL || size == 0) return 0;
    ret = write(stream->fd, (void *)buffer, total);
    if (ret < 0) {
        stream->error = ret;
        return 0;
    }
    return (size_t)ret / size;
}

int fgetc(void *opaque) {
    unsigned char c;
    return fread(&c, 1, 1, opaque) == 1 ? c : EOF;
}
int fputc(int c, void *opaque) {
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, opaque) == 1 ? ch : EOF;
}
char *fgets(char *buffer, int length, void *opaque) {
    int i, c;
    if (length <= 1) return NULL;
    for (i = 0; i < length - 1; i++) {
        c = fgetc(opaque);
        if (c == EOF) break;
        buffer[i] = (char)c;
        if (c == '\n') { i++; break; }
    }
    if (i == 0) return NULL;
    buffer[i] = 0;
    return buffer;
}
int fputs(const char *text, void *opaque) {
    size_t len = strlen(text);
    return fwrite(text, 1, len, opaque) == len ? 0 : EOF;
}
int fflush(void *opaque) { (void)opaque; return 0; }
int fseek(void *opaque, long offset, int whence) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    return stream == NULL ? -1 : seek(stream->fd, (off_t)offset, whence) < 0 ? -1 : 0;
}
long ftell(void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    return stream == NULL ? -1 : (long)seek(stream->fd, 0, LSEEK_CUR);
}
int feof(void *opaque) { (void)opaque; return 0; }
int ferror(void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    return stream != NULL && stream->error != 0;
}
void clearerr(void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    if (stream != NULL) stream->error = 0;
}
int sprintf(char *buffer, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(buffer, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}
int puts(const char *text) {
    int ret = printf("%s\n", text);
    return ret < 0 ? ret : 0;
}
int putchar(int c) { return fputc(c, stdout); }
int fileno(void *opaque) {
    tcc_file_t *stream = (tcc_file_t *)opaque;
    return stream == NULL ? -1 : stream->fd;
}
