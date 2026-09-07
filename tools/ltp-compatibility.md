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

## Upstream LTP audit

The upstream source was downloaded outside this repository at:

```text
E:\project_learning\ltp-upstream
```

The audited checkout is version `20260529`, commit
`463b33ad464b6840d0a1df5d41939e50d55d542c`. Its syscall runfile contains the
standard families relevant to this OS, including `brk`, `chdir`, `clone`,
`close`, `dup`, `fork`, `getcpu`, `getpid`, `getppid`, `gettid`, `lseek`,
`mmap`, `munmap`, `open`, `read`, `sched_setaffinity`, `socket`, `wait`, and
`write`. The source tree also contains the corresponding C tests under
`testcases/kernel/syscalls/`.

The official runner in this checkout is no longer `runltp`: invoking it exits
with the message that `runltp` was removed and that `kirk` should be used.
The official source build was attempted with the documented `make autotools`
entry point. On the Windows MSYS environment it stops before configuration
because `aclocal` is not installed. The first attempt also showed that the
native MSYS PATH does not provide the Unix `dirname`/`sed` tool set expected by
the make rules; with `/usr/bin:/bin` restored, the deterministic blocker is:

```text
make: aclocal: No such file or directory
make: *** .../include/mk/automake.mk:31: aclocal.m4] Error 127
```

WSL2 has `Ubuntu` and `docker-desktop` registrations, but both are stopped;
starting the Ubuntu instance currently fails with
`HCS_SERVICE_NOT_AVAILABLE`. No Windows feature, distribution, or Docker
daemon was changed during this audit. Once a Linux environment is available,
the upstream path is:

```sh
make autotools
./configure --prefix="$HOME/ltp-install"
make all
```

The official LTP result must remain separate from the uCore runner results:
the upstream binaries require Linux headers, glibc, a Linux ELF loader,
`/proc`/`/sys`, signals, users/groups, and many privileged kernel interfaces.
The passing table above therefore records uCore-adapted tests, while this
section records the upstream source/build audit and its environment blockers.

## Upstream syscall-family mapping

The current upstream `runtest/syscalls` file contains 172 entries matching the
families below. The mapping is by test intent, not by claiming that a Linux LTP
binary can execute on uCore.

| Upstream family | Representative upstream checks | uCore status | uCore entry point |
| --- | --- | --- | --- |
| `getpid`, `getppid`, `gettid` | range/parent relationship and single-thread tid equality | `PASS` for the supported semantics | `clonetest`, `hello` |
| `getcpu`, `sched_setaffinity` | set an allowed CPU, query current CPU, reject invalid masks | `PASS` for the supported mask/counter ABI | `affinitytest`, `schedtest` |
| `brk` | grow/shrink break and touch newly allocated pages | `PASS` for anonymous heap semantics | `mmaptest` |
| `mmap`, `munmap` | anonymous mappings, page alignment, partial unmap and fault behavior | `PASS` for anonymous subset; file-backed and signal-fault cases are not implemented | `mmaptest` |
| `chdir` | directory, missing path, permissions, symlink-loop cases | `PORT`/`PASS` for uCore VFS subset; permissions/symlink cases `NOT_IMPL` | `chdirtest`, `vfstest` |
| `open`, `close`, `read`, `write`, `fstat` | descriptor errors, data transfer, metadata and lifecycle | `PASS` for supported SFS/device subset | `vfstest`, `fdsharetest` |
| `dup`, `dup2`, `lseek` | invalid descriptors, replacement, self-dup, shared offset | `PASS` for the implemented descriptor-description model | `chdirtest`, `fdsharetest` |
| `fork`, `clone`, `wait`, `waitpid` | child lifecycle, clone entry, parent wait and status | `PASS` for uCore's supported flags and status ABI | `clonetest`, `fdsharetest` |
| `socket`, UDP send/receive | invalid domain/type cases plus datagram loopback | `PASS` for AF_INET/SOCK_DGRAM; TCP/UNIX/raw cases `NOT_IMPL` | `nettest` |

The following upstream families deliberately remain outside the current uCore
claim: `openat*`, `dup3`, `close_range`, `readv/writev`, signals, `execveat`,
`/proc` and `/sys` inspection, user/group privilege transitions, namespaces,
futexes, epoll, io_uring, filesystem mounts, and file-backed mmap. They need
new ABI and kernel subsystems before a faithful port can be made.
