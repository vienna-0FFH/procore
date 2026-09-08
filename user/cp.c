#include <stdio.h>
#include <ulib.h>
#include <file.h>

#define CP_BUFFER_SIZE 4096

int
main(int argc, char **argv) {
    static char buffer[CP_BUFFER_SIZE];
    int source, target, ret;

    if (argc != 3) {
        cprintf("usage: cp SOURCE DEST\n");
        return -1;
    }
    if ((source = open(argv[1], O_RDONLY)) < 0) {
        cprintf("cp: cannot open %s: %e\n", argv[1], source);
        return source;
    }
    if ((target = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
        cprintf("cp: cannot create %s: %e\n", argv[2], target);
        close(source);
        return target;
    }
    while ((ret = read(source, buffer, sizeof(buffer))) > 0) {
        int written = 0;
        while (written < ret) {
            int count = write(target, buffer + written, ret - written);
            if (count <= 0) {
                cprintf("cp: write %s failed: %e\n", argv[2], count);
                close(source);
                close(target);
                return count < 0 ? count : -1;
            }
            written += count;
        }
    }
    close(source);
    close(target);
    if (ret < 0) {
        cprintf("cp: read %s failed: %e\n", argv[1], ret);
        return ret;
    }
    return 0;
}
