#include <stdio.h>
#include <ulib.h>
#include <file.h>

int
main(int argc, char **argv) {
    int i, ret = 0;
    if (argc < 2) {
        cprintf("usage: touch FILE...\n");
        return -1;
    }
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT);
        if (fd < 0) {
            cprintf("touch: %s: %e\n", argv[i], fd);
            ret = fd;
        } else {
            close(fd);
        }
    }
    return ret;
}
