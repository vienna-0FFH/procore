#include <stdio.h>
#include <ulib.h>
#include <dir.h>

int
main(int argc, char **argv) {
    int ret;
    if (argc != 3) {
        cprintf("usage: mv SOURCE DEST\n");
        return -1;
    }
    ret = rename(argv[1], argv[2]);
    if (ret != 0) {
        cprintf("mv: %s -> %s: %e\n", argv[1], argv[2], ret);
    }
    return ret;
}
