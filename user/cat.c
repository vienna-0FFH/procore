#include <stdio.h>
#include <ulib.h>
#include <file.h>

#define CAT_BUFFER_SIZE 4096

static int
cat_file(const char *path) {
    static char buffer[CAT_BUFFER_SIZE];
    int fd, ret;

    if ((fd = open(path, O_RDONLY)) < 0) {
        cprintf("cat: cannot open %s: %e\n", path, fd);
        return fd;
    }
    while ((ret = read(fd, buffer, sizeof(buffer))) > 0) {
        int written = 0;
        while (written < ret) {
            int count = write(1, buffer + written, ret - written);
            if (count <= 0) {
                close(fd);
                cprintf("cat: write failed: %e\n", count);
                return count < 0 ? count : -1;
            }
            written += count;
        }
    }
    close(fd);
    if (ret < 0) {
        cprintf("cat: read %s failed: %e\n", path, ret);
        return ret;
    }
    return 0;
}

int
main(int argc, char **argv) {
    int i, ret = 0;
    if (argc < 2) {
        return cat_file("stdin:");
    }
    for (i = 1; i < argc; i++) {
        int current = cat_file(argv[i]);
        if (current != 0) ret = current;
    }
    return ret;
}
