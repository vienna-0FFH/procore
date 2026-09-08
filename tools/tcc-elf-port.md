# TinyCC ELF32 Port Track

The project now has a hosted cross-build path for the mature Tiny C Compiler.
TinyCC supplies the pieces C4 does not yet have: a C preprocessor, complete
struct/union and typedef handling, an i386 code generator, an integrated
assembler, relocations, and an ELF linker.

The embedded compiler sources are based on TinyCC 0.9.28rc.  Their upstream
copyright and license notices are retained in `user/tcc/src`; TinyCC core is
distributed under the GNU LGPL, while the i386 `libtcc1` helper source retains
its GPL linking-exception notice.  The uCore compatibility files added here
are part of this repository and do not replace those upstream notices.

The reference source is kept outside the uCore image while the port is being
validated. The WSL build is created from the TinyCC checkout at:

```text
/home/vienna/tinycc-native-lf
```

The repository script [build-tcc-elf.ps1](build-tcc-elf.ps1) compiles a uCore
user source with TinyCC's i386 backend, then links it with the already-built
uCore user runtime objects and `target/native/compat/user.ld`:

```powershell
& '.\tools\build-tcc-elf.ps1' -Source user/hello.c -BuildRuntime
```

The resulting file is an `ELF32 / Intel 80386` executable with two non-
overlapping `PT_LOAD` segments, suitable for the existing uCore `exec` loader.
The first proven input is the original `user/hello.c`; the same command accepts
the other original user sources after they pass the uCore header/runtime
compatibility check.

The generated file can be exercised end to end by placing it in the SFS as
`tcc-program` and booting the `tcc_elf` launcher. The launcher performs a normal
uCore `exec`, so the test covers the complete compile/link/write/load/run path.

The runner automates that flow:

```powershell
& '.\tools\run-ltp.ps1' -Tests tcc_elf
& '.\tools\run-ltp.ps1' -Tests tcc_elf -TccSource user/mmaptest.c -TccName mmaptest
```

The first end-to-end runs passed for the original `hello`, `mmaptest`,
`chdirtest`, and `clonetest` programs. A hosted TinyCC compile-only sweep also
passed for the current original user-program set, including `fdsharetest`,
`vfstest`, `nettest`, `affinitytest`, `schedtest`, `cowtest`, `ltp_legacy`,
`elfgen`, `ls`, `matrix`, `netexternal`, `priority`, `sleep`, `yield`,
`waitkill`, `forktest`, and `forktree`. This is a frontend/backend compile
oracle; each program still needs an end-to-end runtime run when its generated
ELF is selected.

TinyCC's full `libtcc1.a` build is not required for the first freestanding
programs. The WSL host build reaches the i386 compiler, assembler, and linker;
only optional 32-bit glibc-dependent runtime objects are skipped. For uCore,
the runtime is supplied by `user/libs`, `libs`, and the static syscall ABI.

## Running TinyCC In uCore

The TinyCC core is now built as the `/bin/tcc` uCore user program.  The image
also installs its freestanding runtime objects under `/tcc/lib`, its compiler
headers under `/tcc/include`, and the uCore public headers under `/tcc/ucore`.
The compiler adds `/tcc/ucore` automatically, so a source using the normal
uCore headers can be compiled without a host include directory:

```text
/bin/tcc /source.c -o /generated
```

The compiler uses uCore's `brk`, VFS file descriptors, and `int 0x80` syscall
ABI.  Its ELF output is statically linked with `_start`, `umain`, the uCore
user library, and the i386 arithmetic helpers.  No Linux libc, dynamic loader,
WSL process, or host linker is involved after the image boots.

The bounded end-to-end test performs exactly this sequence inside uCore:

```powershell
& '.\tools\run-ltp.ps1' -Tests tcc_run
& '.\tools\run-ltp.ps1' -Tests tcc_run `
    -TccInOsSource user/mmaptest.c -TccInOsName tccdemo.c
```

Both tests pass.  The first compiles a `typedef struct` program and executes
the generated ELF; the second compiles the original `mmaptest.c` and exercises
`mmap`, `munmap`, and `brk` in the generated process.  `TCC_DEMO_SOURCE` and
`TCC_DEMO_SOURCE_NAME` are Make variables, so another source can be placed in
the SFS at build time without changing kernel code.

The compatibility layer is intentionally explicit:

| TinyCC dependency | uCore work item |
| --- | --- |
| `malloc/realloc/free` | brk-backed compiler arena with overflow checks |
| `FILE`, `fopen`, `fread`, `fwrite`, `fseek` | fd-backed user stdio layer |
| `errno`, diagnostics, `setjmp/longjmp` | compiler error/runtime support |
| directory search and include paths | VFS directory iteration and path helpers |
| `qsort`, numeric conversion, string helpers | additions to user libc |
| `libtcc1` arithmetic helpers | freestanding i386 helper subset |

The hosted `build-tcc-elf.ps1` path remains useful as a fast compiler/linker
oracle and for preparing one-off ELF images.  It is separate from the
in-uCore path above; the latter is the runtime compilation path used by the
project's final goal.
