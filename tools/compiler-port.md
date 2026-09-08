# In-OS C Compiler

## Current Stage

`user/c4.c` is a uCore port of Robert Swierczek's small `c4` compiler and
bytecode virtual machine. The source is distributed under the GPL-2.0 license;
the original project is available at `https://github.com/rswier/c4`. The port
keeps the original parser and VM, removes its 64-bit host-width assumption, and
replaces host libc calls with the uCore user ABI.

The compiler is built as an ordinary uCore 32-bit ELF user program. At runtime
it reads `/c4demo.c` from SFS, allocates its symbol/code/data/VM areas through
the uCore `brk` syscall, translates the source to bytecode, and executes the
bytecode in the same process. The source file is copied into SFS by the
`C4_SOURCE` and `C4_SOURCE_NAME` Makefile settings; these are configurable
without changing the compiler.

Run the focused test with:

```powershell
& '.\tools\run-ltp.ps1' -Tests c4
```

The current QEMU/SMP run passes and prints `c4 dynamic compile pass: 42` before
returning status 0. This proves dynamic source loading, parsing, allocation,
and execution; it does not claim to produce a native ELF from inside uCore.

## Linker And Assembler Boundary

The C4 bytecode path intentionally needs neither an assembler nor a linker for
the program being compiled. The host-side build still uses the existing Clang
and LLD to link the `c4` user program itself, exactly like every other uCore
user binary. No host executable is invoked from inside uCore.

A native compiler path has a different contract and is kept separate:

1. The frontend must lower C to an i386 instruction representation.
2. A code emitter must produce 32-bit machine bytes or assembly.
3. An ELF32 writer must create the uCore `_start` entry point, the two load
   segments expected by the current loader, and any static syscall stubs.
4. Relocations and a small uCore C runtime must be resolved before the image is
   executable.

The first native prototype will emit ELF32 directly, avoiding a runtime
assembler dependency. Embedding TinyCC's integrated i386 assembler/linker is a
possible later alternative, but it requires porting substantially more libc,
filesystem, process, and error-handling interfaces. Until that work is done,
the bytecode compiler is the supported in-OS dynamic compilation path and is
reported as such by the test runner.
