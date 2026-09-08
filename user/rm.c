#include <stdio.h>
#include <ulib.h>
#include <dir.h>

int
main(int argc, char **argv) {
    int i, ret = 0;
    if (argc < 2) {
        cprintf("usage: rm PATH...\n");
        return -1;
    }
    for (i = 1; i < argc; i++) {
        int current = unlink(argv[i]);
        if (current != 0) {
            cprintf("rm: %s: %e\n", argv[i], current);
            ret = current;
        }
    }
    return ret;
}
