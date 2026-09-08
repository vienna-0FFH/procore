/* A small line editor for the freestanding uCore user environment.
 *
 * Commands:
 *   p             print the current buffer
 *   a TEXT        append one line
 *   d             delete the last line
 *   w             write the buffer to disk
 *   wq            write and quit
 *   q             quit without saving
 *   h             show this help
 */
#include <stdio.h>
#include <ulib.h>
#include <file.h>
#include <error.h>
#include <string.h>

#define EDIT_BUFFER_SIZE (64 * 1024)
#define EDIT_LINE_SIZE   1024

static char edit_buffer[EDIT_BUFFER_SIZE];
static size_t edit_length;

static int
read_line(char *line, size_t capacity) {
    size_t length = 0;
    while (length + 1 < capacity) {
        char c;
        int ret = read(0, &c, 1);
        if (ret <= 0) {
            if (length == 0) return ret;
            break;
        }
        if (c == 3) {
            write(1, "^C\n", 3);
            return -1;
        }
        if (c == '\n' || c == '\r') {
            write(1, "\n", 1);
            break;
        }
        if (c == '\b' && length > 0) {
            length--;
            write(1, "\b \b", 3);
            continue;
        }
        if (c >= ' ') {
            line[length++] = c;
            write(1, &c, 1);
        }
    }
    line[length] = '\0';
    return (int)length;
}

static void
print_buffer(void) {
    size_t offset = 0;
    while (offset < edit_length) {
        int ret = write(1, edit_buffer + offset, edit_length - offset);
        if (ret <= 0) return;
        offset += ret;
    }
    if (edit_length == 0 || edit_buffer[edit_length - 1] != '\n') {
        write(1, "\n", 1);
    }
}

static void
delete_last_line(void) {
    if (edit_length == 0) return;
    if (edit_buffer[edit_length - 1] == '\n') edit_length--;
    while (edit_length > 0 && edit_buffer[edit_length - 1] != '\n') {
        edit_length--;
    }
}

static int
append_line(const char *line) {
    size_t length = strlen(line);
    size_t extra = length + 1;
    if (edit_length > 0 && edit_buffer[edit_length - 1] != '\n') extra++;
    if (edit_length + extra > sizeof(edit_buffer)) return -1;
    if (edit_length > 0 && edit_buffer[edit_length - 1] != '\n') {
        edit_buffer[edit_length++] = '\n';
    }
    memcpy(edit_buffer + edit_length, line, length);
    edit_length += length;
    edit_buffer[edit_length++] = '\n';
    return 0;
}

static int
load_file(const char *path) {
    int fd = open(path, O_RDONLY);
    int ret;
    if (fd < 0) {
        if (fd == -E_NOENT) return 0;
        cprintf("edit: cannot open %s: %e\n", path, fd);
        return fd;
    }
    edit_length = 0;
    while (edit_length < sizeof(edit_buffer)) {
        ret = read(fd, edit_buffer + edit_length,
                   sizeof(edit_buffer) - edit_length);
        if (ret <= 0) break;
        edit_length += ret;
    }
    close(fd);
    return ret < 0 ? ret : 0;
}

static int
save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    size_t offset = 0;
    if (fd < 0) {
        cprintf("edit: cannot write %s: %e\n", path, fd);
        return fd;
    }
    while (offset < edit_length) {
        int ret = write(fd, edit_buffer + offset, edit_length - offset);
        if (ret <= 0) {
            close(fd);
            return ret < 0 ? ret : -1;
        }
        offset += ret;
    }
    close(fd);
    return 0;
}

static void
print_help(void) {
    cprintf("edit commands: p, a TEXT, d, w, wq, q, h\n");
}

int
main(int argc, char **argv) {
    char line[EDIT_LINE_SIZE];
    int ret;
    if (argc != 2) {
        cprintf("usage: edit FILE\n");
        return -1;
    }
    if ((ret = load_file(argv[1])) != 0) return ret;
    print_help();
    while (1) {
        cprintf("edit> ");
        ret = read_line(line, sizeof(line));
        if (ret <= 0) return ret < 0 ? ret : 0;
        if (strcmp(line, "p") == 0) {
            print_buffer();
        } else if (strcmp(line, "d") == 0) {
            delete_last_line();
        } else if (strcmp(line, "w") == 0 || strcmp(line, "wq") == 0) {
            if ((ret = save_file(argv[1])) != 0) return ret;
            cprintf("edit: wrote %lu bytes\n", edit_length);
            if (line[1] == 'q') return 0;
        } else if (strcmp(line, "q") == 0) {
            return 0;
        } else if (strcmp(line, "h") == 0) {
            print_help();
        } else if (line[0] == 'a' && line[1] == ' ') {
            if (append_line(line + 2) != 0) {
                cprintf("edit: buffer full\n");
                return -1;
            }
        } else {
            cprintf("edit: unknown command\n");
            print_help();
        }
    }
}
