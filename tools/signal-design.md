# Signal Mechanism Design

This document describes the signal subsystem planned for uCore. It is a
design document only; the kernel changes are intentionally a later phase.
The design follows the useful Linux/ReactOS properties while matching this
kernel's existing i386 trap frame, per-CPU scheduler, SFS/VFS, and fixed-arity
syscall ABI.

## Current Gap

The current process object has `PF_EXITING`, but no signal state. `do_kill()`
sets that flag and wakes an interrupted process; it does not select a signal,
run a user handler, preserve user registers, or implement a signal mask.
`trap()` restores the current trap frame and only handles `PF_EXITING` and
`need_resched` before returning to user mode. Page faults still terminate the
process directly after the existing uCore fault path.

Consequently, Linux LTP cases involving `sigaction`, `sigprocmask`,
`sigreturn`, `raise`, alarm/timer delivery, signal interruption, or alternate
stacks are not yet claims of compatibility.

## Scope Of The First Phase

The first implementation should provide a small, deterministic signal ABI:

- Standard signals 1 through 31, represented by a `uint32_t` bitset.
- Per-process disposition table: default, ignore, or one user handler.
- Per-thread pending and blocked masks. The current process is the current
  thread, so this works before a separate thread-group object exists.
- `kill(pid, sig)` for process-directed delivery and `raise(sig)` for the
  current process.
- `sigaction(sig, action, old_action)` with a fixed-width user ABI.
- `sigprocmask(how, set, old_set)` with block, unblock, and set-mask modes.
- `sigreturn()` to restore a kernel-created user signal frame.
- Default actions for terminate, ignore, stop, and continue signals. The
  initial useful set is `SIGTERM`, `SIGKILL`, `SIGINT`, `SIGCHLD`, `SIGSTOP`,
  and `SIGCONT`; the remaining standard signals can initially map to the
  terminate default or explicit `SIG_DFL` behavior.
- `EINTR`-style interruption for blocking waits, `read`, `recv`, `poll`, and
  `accept`, with restart behavior controlled by the action flags in a later
  phase.

The first phase deliberately excludes realtime queued values, `sigaltstack`,
`signalfd`, process groups, session leadership, ptrace signal injection,
seccomp, and Linux-compatible `rt_sigaction` extensions. These can be added
without changing the core delivery model if the ABI reserves the fields now.

## Process Data Structures

Add a signal block to `struct proc_struct` in `kern/process/proc.h`:

```c
#define UCORE_NSIG              32
#define UCORE_SIG_WORDS         1

typedef void (*signal_handler_t)(int);

struct ucore_sigaction {
    uintptr_t handler;      /* SIG_DFL, SIG_IGN, or user virtual address */
    uint32_t flags;
    uint32_t mask;
};

struct signal_state {
    struct ucore_sigaction action[UCORE_NSIG];
    uint32_t pending;
    uint32_t blocked;
    uint32_t in_handler;
    uintptr_t frame;
    spinlock_t lock;
};
```

The action table is process-wide for the current uCore process model. Pending
and blocked masks are per `proc_struct`, so a future thread-group conversion
can move the action table into a shared `signal_struct` without changing the
user frame ABI. `SIGKILL` and `SIGSTOP` must never be blocked or ignored.

Initialization belongs in `alloc_proc()` and inheritance belongs in the fork/
clone path:

- `fork`: copy dispositions; clear pending and handler-frame state; inherit
  the blocked mask.
- `clone(CLONE_VM | CLONE_THREAD)`: share dispositions through a future
  signal-group object, but give the new thread an empty pending mask and its
  own blocked mask.
- `exec`: reset caught handlers to `SIG_DFL`, preserve only ignored handlers
  as Linux does, clear pending and handler-frame state, and reset the blocked
  mask to the ABI default.

All signal-state mutations use the signal lock. Never hold it while copying a
user frame or while scheduling.

## Signal Numbers And ABI

`libs/unistd.h` should define the public numbers and syscall IDs in one place.
The initial set can use the conventional values:

```c
#define SIGHUP       1
#define SIGINT       2
#define SIGQUIT      3
#define SIGILL       4
#define SIGTRAP      5
#define SIGABRT      6
#define SIGFPE       8
#define SIGKILL      9
#define SIGSEGV     11
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20

#define SIG_DFL      0U
#define SIG_IGN      1U

#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2
```

The first syscall allocation should be kept contiguous and documented, for
example `SYS_kill`, `SYS_raise`, `SYS_sigaction`, `SYS_sigprocmask`, and
`SYS_sigreturn`. The syscall dispatcher already passes five fixed 32-bit
arguments through the i386 register ABI; signal APIs must use that mechanism
instead of adding variadic or pointer-width-dependent entry points.

User headers should expose both low-level wrappers and libc-shaped helpers:

```c
int kill(int pid, int sig);
int raise(int sig);
int sigaction(int sig, const struct sigaction *act,
              struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigreturn(void);
```

The structure layout must be fixed-width (`uint32_t`/`uintptr_t`) and shared
by the kernel and `user/libs`. Do not use a host compiler's `long` layout.

## Delivery Point

Signals are delivered only when returning to user mode. The common path is:

1. `trap()` or a syscall returns with `current->tf` still pointing at the
   active user trap frame.
2. `signal_dequeue_unblocked()` takes the lowest-numbered pending signal that
   is not blocked, except for unmaskable `SIGKILL`/`SIGSTOP`.
3. The kernel inspects the disposition under the signal lock, clears the
   pending bit, and releases the lock.
4. Default or ignored actions are applied in kernel context.
5. For a caught signal, the kernel builds a user signal frame and changes the
   saved trap frame to enter the handler.
6. `trap()` performs the normal reschedule check, then the assembly epilogue
   restores the modified frame and executes `iret`.

The delivery helper must run after a syscall or hardware trap has produced a
valid frame, but before `current->tf` is restored to its outer value. It must
not run for a kernel-mode trap frame. If a signal arrives while the process is
on another CPU, `kill()` sets pending under the target signal lock and sends a
reschedule IPI; the target CPU performs delivery at its user return point.

## User Signal Frame

The kernel must not call a user handler as a normal C function. It creates a
frame in the target process's user stack and edits the saved trap frame:

```c
struct ucore_signal_frame {
    uint32_t magic;
    int32_t  signo;
    uint32_t saved_mask;
    uintptr_t saved_frame;
    struct trapframe saved_tf;
    uintptr_t restorer;
};
```

The frame is copied to `tf->tf_esp - align_up(sizeof(frame), 16)`, after
checking the full user address range with the existing VM copy helpers. The
edited user frame is:

- `tf_eip = action.handler`
- `tf_esp = frame_address + offsetof(frame, handler_arg)`
- first stack word = `signo`
- `tf_regs.reg_eax = 0`
- `tf_eflags` retains user IF and arithmetic flags, while privileged bits are
  masked exactly as the normal trap return path requires.

The handler return address is a small user ABI trampoline whose only job is to
load the frame pointer and issue `SYS_sigreturn`. A fixed trampoline can be
placed in the user runtime, or the kernel can store a validated restorer
address in the action structure. The first implementation should use the
runtime trampoline so arbitrary user code cannot request a kernel address.

`sigreturn()` validates the magic, frame address, user CS/SS, EIP/ESP range,
and allowed EFLAGS bits before copying the saved trap frame back to
`current->tf`. It restores the old blocked mask and clears `in_handler`.
Malformed frames terminate the process rather than returning to an arbitrary
kernel address.

## Default Actions And Special Cases

Default actions are implemented centrally so every delivery path has the same
semantics:

- `SIG_DFL` terminate: set `PF_EXITING`, record `-E_KILLED`, and wake a
  blocked wait/read operation.
- `SIG_IGN`: discard the pending bit. `SIGCHLD` defaults to ignore in the
  first compatibility phase, with child zombie reaping still controlled by
  `waitpid`.
- `SIGSTOP`: mark the process stopped and remove it from its run queue.
- `SIGCONT`: change a stopped process to runnable and enqueue it through the
  normal affinity-aware scheduler path.
- `SIGKILL`: terminate immediately and cannot be blocked, ignored, or caught.

`SIGPIPE` should be generated by the pipe/socket write layer when the peer has
no readers or the stream has entered a write-shutdown state. The write syscall
returns `-E_PIPE` only when the signal is ignored or blocked; otherwise the
pending signal is delivered on the return-to-user path.

## Blocking Syscalls And EINTR

Existing waits use semaphores and `do_sleep()`. Signal interruption must be
explicit rather than inferred from a scheduler wakeup:

- Add `WT_SIGNAL` to the wait-state definitions.
- Before sleeping, register the current process as interruptible and check
  `signal_pending(current)`.
- `kill()` wakes a `WT_SIGNAL` waiter and sets a wake flag.
- The syscall returns `-E_INTR` (a new public error code) unless the selected
  action has a future `SA_RESTART` flag.
- `read`, `recv`, `poll`, `accept`, `wait`, and `sleep` are the first blocking
  operations to convert. Pipe and socket semaphore paths must not consume a
  wakeup intended for a real data event.

The current semaphore API asserts that every wake has `wakeup_flags ==
WT_KSEM`; signal-aware waits therefore need a separate interruptible helper or
an extended `down_interruptible()` instead of changing `down()` semantics for
all existing kernel users.

## SMP And Locking Rules

Signal delivery crosses CPUs, so the order is:

1. lock `proc_lock` to find and reference the target process;
2. lock the target signal state and set `pending`;
3. unlock signal state and `proc_lock`;
4. wake an interruptible waiter or send a reschedule IPI to the target CPU.

Never hold `proc_lock` while sending an IPI or copying user memory. A process
reference or an existing scheduler/list membership guarantee must keep the
target alive until the wake operation completes. Signal delivery itself runs
on the target CPU, so it can use that CPU's current trap frame and TSS stack;
it must not edit another CPU's live frame.

## Implementation Order

1. Add fixed signal constants, structs, syscall numbers, and `E_INTR`.
2. Add `signal_state` to `proc_struct`, initialize/inherit/reset it in
   alloc/fork/clone/exec paths.
3. Implement `kill`, `raise`, `sigaction`, `sigprocmask`, and pending-bit
   selection without user handlers; test default terminate/ignore.
4. Add user signal frame construction and the runtime `sigreturn` trampoline.
5. Add `sigreturn` validation and handler delivery after trap/syscall return.
6. Convert `read`, `recv`, `poll`, `accept`, `wait`, and `sleep` to
   interruptible waits.
7. Add `SIGCHLD`, `SIGPIPE`, `SIGSTOP`, and `SIGCONT` behavior.
8. Add SMP delivery tests and only then expand toward realtime signals,
   alternate stacks, and process groups.

## Test Plan

Focused uCore tests should cover:

- handler receives the signal number and returns through `sigreturn`;
- blocked signal remains pending until unblocked;
- ignored signal does not terminate the process;
- `SIGKILL` cannot be blocked or caught;
- a signal interrupts blocking `read`, `recv`, `poll`, `accept`, and `waitpid`;
- fork/clone/exec disposition and pending-mask rules;
- cross-CPU `kill` with affinity pinned to each online CPU;
- malformed user signal frames terminate safely without kernel panic;
- `SIGPIPE` on a closed pipe/TCP peer;
- handler delivery and `sigreturn` while the process has COW mappings.

The host runner should use bounded QEMU timeouts and a distinct result marker,
as existing LTP-style tests do. No upstream Linux LTP binary should be called
a uCore pass until its assumptions about `/proc`, signals, ELF loading, and
libc have been replaced or explicitly supported.
