# uCore LTP-style Compatibility Matrix

This project is not a Linux kernel and cannot run the upstream Linux Test
Project binaries directly. The native runner uses the same intent and
assertion style for tests that fit uCore's 32-bit ABI, then boots each user
program in QEMU and records a deterministic result marker.

Run the default subset from PowerShell:

```powershell
& '.\tools\run-ltp.ps1'
```

The editable defaults live in `tools/ltp-config.psd1`. Command-line
parameters (or `UCORE_QEMU`) override that file, so a different QEMU build,
memory size, CPU count, timeout, or test list does not require script edits.

Run one test or change the CPU topology:

```powershell
& '...\tools\run-ltp.ps1' -Tests fdsharetest -QemuSmp 4
```

Each result is classified as follows:

| Result | Meaning |
| --- | --- |
| `PASS` | The user program exited with status 0 and the kernel emitted the result marker. |
| `FAIL` | The program ran but returned a nonzero status, or a panic/fault occurred before the marker. |
| `BUILD_FAIL` | The native cross build failed. |
| `TIMEOUT` | The marker was not observed before QEMU was forcefully reaped. |

The current initial subset maps these LTP themes:

| uCore test | LTP-style area | Covered behavior |
| --- | --- | --- |
| `hello` | process baseline | exec, user entry, clean exit |
| `chdirtest` | `chdir`, `dup`, `lseek` | VFS cwd and shared open-file offset |
| `clonetest` | `clone`, `gettid`, `getppid`, `getcpu` | shared address space/thread entry |
| `mmaptest` | `mmap`, `munmap`, `brk` | anonymous mappings and heap boundary |
| `fdsharetest` | `dup`, `close`, `fork` | shared descriptions and SMP lifetime races |
| `vfstest` | VFS namespace | mkdir, link, rename, unlink, traversal |
| `nettest` | socket/UDP | loopback datagrams and descriptor sharing |
| `affinitytest` | scheduler affinity | CPU mask and per-CPU counters |
| `schedtest` | scheduler/load balance | runnable migration and accounting |
| `cowtest` | memory management | fork COW and reclaim |

Upstream cases that depend on Linux-only facilities such as `/proc`, signals,
ptrace, namespaces, cgroups, futexes, or a dynamic ELF loader remain
`NOT_IMPL` until uCore grows the corresponding subsystem. They should not be
reported as upstream-LTP passes merely because a similarly named uCore test
exists.
