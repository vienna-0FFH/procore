# Legacy LTP Adaptation

The selected upstream reference is Linux Test Project tag `20210927`, commit
`12beeda351b5d758a729aaf695b836ccc9eb5304`. This version still contains the
classic `runltp` and the syscall runfile used to select the source tests. The
full Linux suite is not copied into uCore: its binaries require Linux/glibc,
`/proc`, signals, namespaces, and privilege operations that are outside the
current ABI.

The uCore adaptation is `user/ltp_legacy.c`. It keeps the old test intent while
replacing Linux-only setup with uCore interfaces:

| 20210927 family | uCore adaptation |
| --- | --- |
| `fork01`, `getpid01`, `getppid01`, `gettid01`, `waitpid01` | one child checks identity and is reaped with `waitpid` |
| `brk01` | grow, touch, and shrink the uCore program break |
| `mmap01`, `munmap01` | anonymous two-page mapping, writes, and unmap |
| `open01`, `read01`, `dup201`, `lseek01` | read the bundled source and verify shared descriptor offset |
| `getcpu01`, `sched_setaffinity01` | query the uCore CPU and set/read the current task mask |

The port deliberately omits `/proc/sys/kernel/pid_max`, symlink-loop cases,
signals, file-backed mappings, and root-only checks. It must not be reported as
an upstream Linux LTP pass; it is a uCore-native port of the 20210927
assertions.

Run only this adapted group with:

```powershell
& '.\tools\run-ltp.ps1' -Tests ltp_legacy
```

The verified result is 15 checks, 0 failures, status 0 on four QEMU CPUs. The
normal configuration includes both `c4` and `ltp_legacy` in addition to the
existing uCore compatibility tests.
