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
| `signalchldtest`, `signalproctest` | signals and `waitpid` | caught `SIGCHLD`/`SIGUSR1`, deferred delivery, and `EINTR` |
| `signalmasktest` | signal masks | block/unblock, pending delivery, and uncatchable signals |
| `signaldefaulttest`, `signalstoptest` | default actions/job control | default termination plus `SIGSTOP`/`SIGCONT` |
| `signalpipetest` | `SIGPIPE` | broken pipe default action |
| `fileiotest` | `stat`, `lstat`, `truncate`, `ftruncate`, `pread`, `pwrite`, `readv`, `writev` | path metadata, length changes, positional I/O, and vector I/O |
| `timetest` | `clock_gettime`, `clock_getres`, `gettimeofday`, `nanosleep`, `time` | monotonic/realtime clocks, tick resolution, sleep validation, and process CPU time |
| `sysinfotest` | `uname`, `sysinfo`, `getuid`, `geteuid`, `getgid`, `getegid`, `getresuid`, `getresgid` | OS identity, memory/swap totals, process count, and the configured single-identity model |
| `waitvfstest` | `wait4`, `WNOHANG`, `fchdir`, `rmdir` | non-blocking child polling, descriptor-based cwd, and empty-directory removal |
| `waitidtest` | `waitid`, `P_PID`, `P_ALL`, `WEXITED`, `WNOWAIT` | child status records, non-blocking polling, and optional non-reaping observation |
| `chdirtest` | `chdir`, `dup`, `lseek` | VFS cwd and shared open-file offset |
| `clonetest` | `clone`, `gettid`, `getppid`, `getcpu` | shared address space/thread entry |
| `mmaptest` | `mmap`, `munmap`, `brk` | anonymous mappings and heap boundary |
| `mprotecttest` | `mprotect` | page-aligned VMA permission changes, resident PTE updates, and PROT_NONE restoration |
| `fdsharetest` | `dup`, `close`, `fork` | shared descriptions and SMP lifetime races |
| `vfstest` | VFS namespace | mkdir, link, rename, unlink, traversal |
| `nettest` | socket/UDP | loopback datagrams and descriptor sharing |
| `socketopttest` | `getsockopt`, `setsockopt` | socket type/buffer flags, IP TTL, TCP_NODELAY and TCP_MAXSEG |
| `selecttest` | `select`, `fd_set` | read/write readiness, timeout conversion, and invalid timeout/set boundaries |
| `affinitytest` | scheduler affinity | CPU mask and per-CPU counters |
| `schedtest` | scheduler/load balance | runnable migration and accounting |
| `cowtest` | memory management | fork COW and reclaim |
| `c4` | dynamic compiler/runtime | load C source from SFS, compile to bytecode, execute |
| `ltp_legacy` | legacy LTP syscall intent | adapted 20210927 process, memory, VFS, and CPU checks |
| `elfgen` | native compiler backend contract | generate an ELF32 file in uCore and exec it |
| `tcc_elf` | hosted TinyCC native path | compile an original user source to ELF32, then execute it in uCore |
| `tcc_run` | in-uCore TinyCC | compile an SFS C source to a static ELF from inside uCore, then execute it |

The latest verification used the configured four-vCPU QEMU topology
(`tools/ltp-config.psd1`, `QemuSmp = 4`) on 2026-09-10. Each configured
program built successfully and was verified individually with the result
marker and `status=0`; the signal-specific programs are included in the
current configuration and are listed above. One long default batch observed a
single unrepeatable COW assertion failure; four repeated `cowtest` runs and a
`cowstress` run passed afterward, so it is recorded as residual scheduling
stress rather than a confirmed deterministic failure.

| uCore runner result | Tests |
| --- | --- |
| `PASS` | `hello`, signal tests, `chdirtest`, `clonetest`, `mmaptest`, `fdsharetest`, `pipetest`, `polltest`, `vfstest`, local network tests, `affinitytest`, `schedtest`, `cowtest`, `c4`, `tcc_run`, `ltp_legacy`, `elfgen` |

The machine-readable record is `target/native/ltp/summary.csv`; serial logs are
kept beside it. These are uCore/QEMU results, not upstream Linux LTP results.

The compiler, legacy-port, native ELF, and in-uCore TinyCC additions were also
run individually on the same four-vCPU topology:

| Test | Result | Detail |
| --- | --- | --- |
| `c4` | `PASS` | `/c4demo.c` compiled to bytecode and executed; status 0 |
| `tcc_run` | `PASS` | in-uCore TinyCC compiled and executed `struct/typedef`, `mmaptest`, `clonetest`, `chdirtest`, `vfstest`, `cowtest`, `nettest`, `affinitytest`, `schedtest`, `fdsharetest`, `forktree`, `exit`, `sleepkill`, `priority`, `matrix`, `ltp_legacy`, `forktest`, `sleep`, `yield`, `cowstress`, `netexternal_tx`, `pgdir`, `ls`, and `spin` sources; status 0 |
| `ltp_legacy` | `PASS` | 25 adapted checks, 0 failures; status 0 |
| `elfgen` | `PASS` | generated ELF32 executed and returned status 0 |

Their implementation and provenance are documented in `tools/compiler-port.md`
and `tools/ltp-legacy.md`. The editable runner configuration contains the
signal coverage and deterministic local tests shown above. TCP tests that
contact the host are enabled explicitly with the runner's host-service
switches, for example:

```powershell
& '.\tools\run-ltp.ps1' -Tests tcpwindowtest -QemuUserNet -QemuHostTcpEcho
```

Upstream cases that depend on Linux-only facilities such as `/proc`, advanced
signal queues and job-control details,
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
The official source build was first attempted with the documented
`make autotools` entry point in Windows/MSYS. It stops before configuration
because `aclocal` is not installed. The first attempt also showed that the
native MSYS PATH does not provide the Unix `dirname`/`sed` tool set expected by
the make rules; with `/usr/bin:/bin` restored, the deterministic blocker is:

```text
make: aclocal: No such file or directory
make: *** .../include/mk/automake.mk:31: aclocal.m4] Error 127
```

WSL2 Ubuntu is now available on this host. The out-of-tree Linux build uses
the persistent paths below (outside this repository):

```text
source:  E:\project_learning\ltp-linux-src
build:   E:\project_learning\ltp-linux-build
install: E:\project_learning\ltp-linux-install
```

The initial build on `/mnt/e` produced 1,553 syscall test binaries before a
15-minute continuation limit reached the container/network-namespace
families. It was terminated by that limit with no compiler error. This was a
build-time bound, not a test failure. To remove the DrvFS I/O bottleneck, the
same source was then copied to WSL's native filesystem and built from:

```text
source:  /home/vienna/ltp-linux-src-native
build:   /home/vienna/ltp-linux-build-native
install: /home/vienna/ltp-linux-install-native
```

That native build completed with `RC=0` and produced 1,539 syscall binaries
(2,253 executable test artifacts in total); installation also completed with
`RC=0`. The checkout does not contain the optional `tools/kirk/kirk-src`
submodule, so installation intentionally contains the upstream `runltp`
compatibility shim, which tells users to install Kirk. The representative tests
below were run directly from the built binaries, so this missing runner
submodule does not affect their results. No Windows feature, distribution, or
Docker daemon was changed during the audit. If the build needs to be repeated,
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

## Linux-host representative run

The following binaries were run directly on WSL2 Ubuntu from the native build
tree (`/home/vienna/ltp-linux-build-native`),
with `timeout --kill-after=5s 45s` around each process. They validate the
upstream test harness and Linux behavior only; they are not uCore results.
`TPASS` is the number of passing assertions printed by each test.

| Test | Result | TPASS | Note |
| --- | --- | ---: | --- |
| `getpid01` | `PASS` | 100 | process identity |
| `getppid01` | `PASS` | 1 | parent identity |
| `gettid01` | `PASS` | 2 | thread identity |
| `getcpu01` | `PASS` | 1 | CPU query |
| `brk01` | `PASS` | 2 | heap boundary |
| `clone01` | `PASS` | 2 | clone lifecycle |
| `close01` | `PASS` | 3 | file/pipe/socket close |
| `dup201` | `PASS` | 4 | invalid `dup2` descriptors |
| `fork01` | `PASS` | 2 | fork lifecycle |
| `lseek01` | `PASS` | 4 | file offset behavior |
| `mmap01` | `PASS` | 1 | anonymous mapping |
| `munmap01` | `PASS` | 2 | mapping removal |
| `open01` | `PASS` | 2 | open error/success paths |
| `read01` | `PASS` | 1 | read transfer |
| `write01` | `PASS` | 1 | write transfer |
| `socket01` | `PASS` | 9 | socket creation/error paths |
| `wait01` | `PASS` | 1 | child wait |
| `waitpid01` | `PASS` | 146 | waitpid matrix |
| `chdir01` | `TCONF` | 0 | WSL user is not root |
| `sched_setaffinity01` | `TCONF` | 0 | WSL CPU mask is restricted |

No selected representative test reported `TFAIL` or `TBROK`. The two
`TCONF` results are environment constraints and must not be counted as uCore
failures or Linux kernel regressions. The corresponding uCore checks remain the
programs listed in `tools/ltp-config.psd1` and are run by `tools/run-ltp.ps1`.

## Upstream syscall-family mapping

The current upstream `runtest/syscalls` file contains 172 entries matching the
families below. The mapping is by test intent, not by claiming that a Linux LTP
binary can execute on uCore.

| Upstream family | Representative upstream checks | uCore status | uCore entry point |
| --- | --- | --- | --- |
| `getpid`, `getppid`, `gettid` | range/parent relationship and single-thread tid equality | `PASS` for the supported semantics | `clonetest`, `hello` |
| `getcpu`, `sched_setaffinity` | set an allowed CPU, query current CPU, reject invalid masks | `PASS` for the supported mask/counter ABI | `affinitytest`, `schedtest` |
| `brk` | grow/shrink break and touch newly allocated pages | `PASS` for anonymous heap semantics | `mmaptest` |
| `mmap`, `munmap` | anonymous mappings, page alignment, partial unmap and fault behavior | `PASS` for anonymous subset; file-backed mappings are not implemented | `mmaptest` |
| `mprotect` | writable/read-only transitions, `PROT_NONE`, and resident-page preservation | `PASS` for page-aligned anonymous mappings; hardware read/execute distinction remains i386-limited | `mprotecttest` |
| `chdir` | directory, missing path, permissions, symlink-loop cases | `PORT`/`PASS` for uCore VFS subset; permissions/symlink cases `NOT_IMPL` | `chdirtest`, `vfstest` |
| `open`, `close`, `read`, `write`, `fstat`, `stat`, `lstat` | descriptor/path errors, data transfer, metadata and lifecycle | `PASS` for supported SFS/device subset; `lstat` matches `stat` until symlink traversal is added | `vfstest`, `fdsharetest`, `fileiotest` |
| `truncate`, `ftruncate`, `pread`, `pwrite`, `readv`, `writev` | file length changes, positional operations, and scatter/gather buffers | `PASS` for regular SFS files | `fileiotest` |

The vector-I/O count is bounded by the editable `FS_IOV_MAX` policy in
`kern/fs/fs_config.h` (default 16); builds can override it with
`FS_DEFS+=-DFS_IOV_MAX=...`.
| `dup`, `dup2`, `lseek` | invalid descriptors, replacement, self-dup, shared offset | `PASS` for the implemented descriptor-description model | `chdirtest`, `fdsharetest` |
| `fork`, `clone`, `wait`, `waitpid` | child lifecycle, clone entry, parent wait and status | `PASS` for uCore's supported flags and status ABI | `clonetest`, `fdsharetest` |
| `socket`, UDP send/receive | invalid domain/type cases plus datagram loopback | `PASS` for AF_INET/SOCK_DGRAM; TCP/raw and connected UNIX sockets remain `NOT_IMPL` | `nettest` |
| `socketpair` | unnamed AF_UNIX stream pair, full-duplex byte flow, nonblocking read, and peer close readiness | `PASS` for the in-kernel pipe-backed SOCK_STREAM subset | `socketpairtest` |
| `raise`, `kill`, `sigaction`, `sigprocmask`, `sigreturn` | pending delivery, handler return, masks, default actions, stop/continue, `SIGCHLD`, `SIGPIPE` | `PASS` for the first-phase process-directed ABI; realtime queues, timers, and signalfd are not implemented | signal tests |

The following upstream families deliberately remain outside the current uCore
claim: `openat*`, `dup3`, `close_range`, `readv/writev`, advanced Linux signal
queue/timer APIs, `execveat`,
`/proc` and `/sys` inspection, user/group privilege transitions, namespaces,
futexes, epoll, io_uring, filesystem mounts, and file-backed mmap. They need
new ABI and kernel subsystems before a faithful port can be made.
