/* Compile a C source from SFS inside uCore, then execute the generated ELF. */
#include <error.h>
#include <file.h>
#include <stdio.h>
#include <ulib.h>

int
main(int argc, char **argv) {
    int child, status;
    const char *source = argc > 1 ? argv[1] : "/src/tccdemo.c";
    const char *output = argc > 2 ? argv[2] : "/tcc-generated";
    const char *compile_argv[] = {
        "/bin/tcc", source, "-o", output, NULL
    };
    const char *run_argv[] = { output, NULL };

    child = fork();
    if (child < 0) {
        cprintf("tcc-run: fork failed: %e\n", child);
        return child;
    }
    if (child == 0) {
        dup2(1, 2);
        __exec(NULL, compile_argv);
        return -E_UNSPECIFIED;
    }
    if (waitpid(child, &status) != 0 || status != 0) {
        cprintf("tcc-run: compiler failed: %d\n", status);
        return -E_UNSPECIFIED;
    }
    cprintf("tcc-run: compiler produced ELF, executing\n");
    __exec(NULL, run_argv);
    return -E_UNSPECIFIED;
}
