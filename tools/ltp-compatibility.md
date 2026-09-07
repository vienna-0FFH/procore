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

The latest complete uCore run used the configured four-vCPU QEMU topology
(`tools/ltp-config.psd1`, `QemuSmp = 4`) and finished on 2026-09-08. All ten
configured programs built successfully, reached their result marker, and
returned status 0:

| uCore runner result | Tests |
| --- | --- |
| `PASS` (10/10) | `hello`, `chdirtest`, `clonetest`, `mmaptest`, `fdsharetest`, `vfstest`, `nettest`, `affinitytest`, `schedtest`, `cowtest` |

The machine-readable record is `target/native/ltp/summary.csv`; serial logs are
kept beside it. These are uCore/QEMU results, not upstream Linux LTP results.

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
