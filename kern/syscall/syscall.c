#include <defs.h>
#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <vmm.h>
#include <assert.h>
#include <clock.h>
#include <stat.h>
#include <dirent.h>
#include <sysfile.h>
#include <error.h>
#include <smp.h>
#include <net.h>
#include <file.h>
#include <kmalloc.h>
#include <fs_config.h>
#include <sysinfo_config.h>
#include <swap.h>

static size_t
sysinfo_copy_string(char *dst, size_t capacity, const char *src) {
    size_t length = strlen(src);
    if (length >= capacity) length = capacity - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
    return length;
}

static bool
sysinfo_copy_to_user(struct mm_struct *mm, uintptr_t address,
                     const void *source, size_t length) {
    bool copied;
    lock_mm(mm);
    copied = address != 0 && user_mem_check(mm, address, length, 1) &&
             copy_to_user(mm, (void *)address, source, length);
    unlock_mm(mm);
    return copied;
}

static int
sys_exit(uint32_t arg[]) {
    int error_code = (int)arg[0];
    return do_exit(error_code);
}

static int
sys_fork(uint32_t arg[]) {
    struct trapframe *tf = current->tf;
    uintptr_t stack = tf->tf_esp;
    return do_fork(0, stack, tf);
}

static int
sys_clone(uint32_t arg[]) {
    const uint32_t supported = CLONE_VM | CLONE_FS;
    uint32_t clone_flags = arg[0];
    uintptr_t stack = arg[1];
    uintptr_t entry = arg[2];
    uintptr_t fn = arg[3];
    uintptr_t fn_arg = arg[4];
    struct trapframe *tf = current->tf;
    uintptr_t frame[3] = { 0, fn, fn_arg };
    struct mm_struct *mm = current->mm;

    /* The entry trampoline and a distinct stack make CLONE_VM useful without
     * borrowing the parent's in-flight syscall frame.  Thread groups are not
     * represented by proc_struct yet, so reject those flags explicitly. */
    if ((clone_flags & ~supported) != 0 ||
        (clone_flags & CLONE_VM) == 0 || mm == NULL ||
        entry == 0 || fn == 0) {
        return -E_INVAL;
    }
    if (stack < 3 * sizeof(uint32_t) || stack > USERTOP) {
        return -E_INVAL;
    }
    lock_mm(mm);
    if (!user_mem_check(mm, entry, sizeof(uint32_t), 0) ||
        !user_mem_check(mm, fn, sizeof(uint32_t), 0) ||
        !user_mem_check(mm, stack - 3 * sizeof(uint32_t),
                        3 * sizeof(uint32_t), 1) ||
        !copy_to_user(mm, (void *)(stack - 3 * sizeof(uint32_t)),
                      frame, sizeof(frame))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return do_fork_with_entry(clone_flags,
                              stack - 3 * sizeof(uint32_t), tf, entry);
}

static int
sys_wait(uint32_t arg[]) {
    int pid = (int)arg[0];
    int *store = (int *)arg[1];
    return do_wait(pid, store);
}

static int
sys_wait4(uint32_t arg[]) {
    int pid = (int)arg[0];
    int *store = (int *)arg[1];
    uint32_t options = arg[2];
    if ((options & ~WNOHANG) != 0) return -E_INVAL;
    return do_wait_options(pid, store, options);
}

static int
sys_waitid(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct siginfo info;
    int ret;
    if (mm == NULL || arg[2] == 0 ||
        !user_mem_check(mm, (uintptr_t)arg[2], sizeof(info), 1)) {
        return -E_INVAL;
    }
    ret = do_waitid((int)arg[0], (int)arg[1], &info, arg[3]);
    if (ret != 0) return ret;
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[2], &info, sizeof(info))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_exec(uint32_t arg[]) {
    const char *name = (const char *)arg[0];
    int argc = (int)arg[1];
    const char **argv = (const char **)arg[2];
    return do_execve(name, argc, argv);
}

static int
sys_yield(uint32_t arg[]) {
    return do_yield();
}

static int
sys_kill(uint32_t arg[]) {
    int pid = (int)arg[0];
    return do_kill_signal(pid, (int)arg[1]);
}

static int
sys_raise(uint32_t arg[]) {
    return signal_queue(current, (int)arg[0]);
}

static int
sys_sigaction(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sigaction action;
    struct sigaction old_action;
    int signo = (int)arg[0];
    int ret;

    if (mm == NULL || signo <= 0 || signo >= UCORE_NSIG) {
        return -E_INVAL;
    }
    if (arg[1] != 0) {
        lock_mm(mm);
        ret = copy_from_user(mm, &action, (void *)arg[1],
                             sizeof(action), 0);
        unlock_mm(mm);
        if (!ret) {
            return -E_INVAL;
        }
        if (action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN) {
            lock_mm(mm);
            ret = user_mem_check(mm, action.sa_handler, 1, 0) &&
                  action.sa_restorer != 0 &&
                  user_mem_check(mm, action.sa_restorer, 1, 0);
            unlock_mm(mm);
            if (!ret) {
                return -E_INVAL;
            }
        }
    }
    ret = signal_exchange_action(current, signo,
                                 arg[1] != 0 ? &action : NULL,
                                 arg[2] != 0 ? &old_action : NULL);
    if (ret != 0) {
        return ret;
    }
    if (arg[2] != 0) {
        lock_mm(mm);
        ret = copy_to_user(mm, (void *)arg[2], &old_action,
                           sizeof(old_action));
        unlock_mm(mm);
        if (!ret) {
            return -E_INVAL;
        }
    }
    return 0;
}

static int
sys_sigprocmask(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    sigset_t set;
    sigset_t old_set;
    int ret;

    if (mm == NULL ||
        (int)arg[0] != SIG_BLOCK && (int)arg[0] != SIG_UNBLOCK &&
        (int)arg[0] != SIG_SETMASK) {
        return -E_INVAL;
    }
    ret = signal_get_mask(current, &old_set);
    if (ret != 0) {
        return ret;
    }
    if (arg[1] != 0) {
        lock_mm(mm);
        ret = copy_from_user(mm, &set, (void *)arg[1], sizeof(set), 0);
        unlock_mm(mm);
        if (!ret) {
            return -E_INVAL;
        }
        ret = signal_set_mask(current, (int)arg[0], set);
        if (ret != 0) {
            return ret;
        }
    }
    if (arg[2] != 0) {
        lock_mm(mm);
        ret = copy_to_user(mm, (void *)arg[2], &old_set, sizeof(old_set));
        unlock_mm(mm);
        if (!ret) {
            return -E_INVAL;
        }
    }
    return 0;
}

static int
sys_sigreturn(uint32_t arg[]) {
    return signal_sigreturn();
}

static int
sys_getpid(uint32_t arg[]) {
    return current->pid;
}

static int
sys_getppid(uint32_t arg[]) {
    return current->parent != NULL ? current->parent->pid : 0;
}

static int
sys_gettid(uint32_t arg[]) {
    /* Each schedulable uCore thread currently has its own proc_struct. */
    return current->pid;
}

static int
sys_getcpu(uint32_t arg[]) {
    return smp_current_cpu();
}

static uint16_t
sysinfo_process_count(void) {
    list_entry_t *le;
    uint16_t count = 0;
    bool intr_flag;
    local_intr_save(intr_flag);
    spin_lock(&proc_lock);
    for (le = list_next(&proc_list); le != &proc_list;
         le = list_next(le)) {
        if (count != 0xFFFFU) count++;
    }
    spin_unlock(&proc_lock);
    local_intr_restore(intr_flag);
    return count;
}

static int
sys_uname(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct utsname value;
    if (mm == NULL || arg[0] == 0) return -E_INVAL;
    memset(&value, 0, sizeof(value));
    sysinfo_copy_string(value.sysname, sizeof(value.sysname), UCORE_SYSNAME);
    sysinfo_copy_string(value.nodename, sizeof(value.nodename), UCORE_NODENAME);
    sysinfo_copy_string(value.release, sizeof(value.release), UCORE_RELEASE);
    sysinfo_copy_string(value.version, sizeof(value.version), UCORE_VERSION);
    sysinfo_copy_string(value.machine, sizeof(value.machine), UCORE_MACHINE);
    sysinfo_copy_string(value.domainname, sizeof(value.domainname), UCORE_DOMAINNAME);
    return sysinfo_copy_to_user(mm, (uintptr_t)arg[0], &value, sizeof(value)) ?
           0 : -E_INVAL;
}

static int
sys_sysinfo(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sysinfo value;
    if (mm == NULL || arg[0] == 0) return -E_INVAL;
    memset(&value, 0, sizeof(value));
    value.uptime = (int32_t)clock_uptime_seconds();
    value.totalram = (uint32_t)npage;
    value.freeram = (uint32_t)nr_free_pages();
    value.totalswap = (uint32_t)max_swap_offset;
    value.freeswap = (uint32_t)max_swap_offset;
    value.procs = sysinfo_process_count();
    value.mem_unit = PGSIZE;
    return sysinfo_copy_to_user(mm, (uintptr_t)arg[0], &value, sizeof(value)) ?
           0 : -E_INVAL;
}

static int
sys_getuid(uint32_t arg[]) { (void)arg; return (int)UCORE_DEFAULT_UID; }

static int
sys_geteuid(uint32_t arg[]) { (void)arg; return (int)UCORE_DEFAULT_UID; }

static int
sys_getgid(uint32_t arg[]) { (void)arg; return (int)UCORE_DEFAULT_GID; }

static int
sys_getegid(uint32_t arg[]) { (void)arg; return (int)UCORE_DEFAULT_GID; }

static int
sys_getresuid(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uint32_t value = UCORE_DEFAULT_UID;
    int i;
    if (mm == NULL) return -E_INVAL;
    for (i = 0; i < 3; i++) {
        if (arg[i] != 0 && !sysinfo_copy_to_user(mm, (uintptr_t)arg[i],
                                                  &value, sizeof(value))) return -E_INVAL;
    }
    return 0;
}

static int
sys_getresgid(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uint32_t value = UCORE_DEFAULT_GID;
    int i;
    if (mm == NULL) return -E_INVAL;
    for (i = 0; i < 3; i++) {
        if (arg[i] != 0 && !sysinfo_copy_to_user(mm, (uintptr_t)arg[i],
                                                  &value, sizeof(value))) return -E_INVAL;
    }
    return 0;
}

static int
sys_mmap(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uintptr_t hint = arg[0];
    size_t len = (size_t)arg[1];
    uint32_t prot = arg[2];
    uint32_t flags = arg[3];
    uintptr_t start;
    int ret;

    if (mm == NULL || len == 0 ||
        (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
        (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS)) != 0 ||
        (flags & MAP_ANONYMOUS) == 0 ||
        ((flags & (MAP_SHARED | MAP_PRIVATE)) ==
         (MAP_SHARED | MAP_PRIVATE))) {
        return -E_INVAL;
    }
    lock_mm(mm);
    if ((flags & MAP_FIXED) != 0) {
        if ((hint & (PGSIZE - 1)) != 0 || hint + len < hint ||
            !USER_ACCESS(hint, ROUNDUP(hint + len, PGSIZE))) {
            ret = -E_INVAL;
            goto out_unlock;
        }
        start = hint;
    }
    else {
        start = get_unmapped_area(mm, len);
        if (start == 0) {
            ret = -E_NO_MEM;
            goto out_unlock;
        }
    }
    ret = mm_map(mm, start, len, prot, NULL);
    if (ret == 0) {
        ret = (int)start;
    }
out_unlock:
    unlock_mm(mm);
    return ret;
}

static int
sys_munmap(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    int ret;
    if (mm == NULL) {
        return -E_INVAL;
    }
    lock_mm(mm);
    ret = mm_unmap(mm, (uintptr_t)arg[0], (size_t)arg[1]);
    unlock_mm(mm);
    return ret;
}

static int
sys_mprotect(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uint32_t prot = arg[2];
    uint32_t vm_flags = 0;
    int ret;

    if (mm == NULL || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        return -E_INVAL;
    }
    if (prot & PROT_READ) vm_flags |= VM_READ;
    if (prot & PROT_WRITE) vm_flags |= VM_WRITE;
    if (prot & PROT_EXEC) vm_flags |= VM_EXEC;
    lock_mm(mm);
    ret = mm_mprotect(mm, (uintptr_t)arg[0], (size_t)arg[1], vm_flags);
    unlock_mm(mm);
    return ret;
}

static int
sys_brk(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uintptr_t oldbrk;
    int ret;
    if (mm == NULL) {
        return -E_INVAL;
    }
    lock_mm(mm);
    oldbrk = mm->brk;
    ret = mm_brk(mm, (uintptr_t)arg[0]);
    ret = (ret == 0) ? (int)mm->brk : (int)oldbrk;
    unlock_mm(mm);
    return ret;
}

static int
sys_setaffinity(uint32_t arg[]) {
    return do_setaffinity((int)arg[0], arg[1]);
}

static int
sys_getaffinity(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uint32_t mask;
    int ret;

    if (mm == NULL) {
        return -E_INVAL;
    }
    ret = do_getaffinity((int)arg[0], &mask);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &mask, sizeof(mask))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    return ret;
}

static int
sys_getcpustat(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct cpu_stat stat;
    int ret;

    if (mm == NULL ||
        !user_mem_check(mm, (uintptr_t)arg[1], sizeof(stat), 1)) {
        return -E_INVAL;
    }
    ret = smp_get_cpu_stat((int)arg[0], &stat);
    if (ret != 0) {
        return ret;
    }
    stat.runnable = sched_cpu_load((int)arg[0]);
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &stat, sizeof(stat))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    return ret;
}

static int
sys_putc(uint32_t arg[]) {
    int c = (int)arg[0];
    cputchar(c);
    return 0;
}

static int
sys_pgdir(uint32_t arg[]) {
    print_pgdir();
    return 0;
}

static uint32_t
sys_gettime(uint32_t arg[]) {
    return (int)ticks;
}

static bool
sys_clock_id_supported(int clock_id) {
    switch (clock_id) {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_REALTIME_COARSE:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME:
        return 1;
    default:
        return 0;
    }
}

static void
sys_clock_ticks_to_timespec(uint64_t value, struct timespec *store) {
    uint32_t seconds_low = 0;
    uint32_t seconds_high = 0;
    uint32_t remainder = 0;
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)value;
    uint32_t i;
    uint32_t nanoseconds;
    /* Divide a 64-bit tick count by the configured 32-bit frequency without
     * pulling a software 64-bit division routine into the kernel image. */
    for (i = 32; i-- > 0;) {
        uint64_t shifted = ((uint64_t)remainder << 1) |
                           ((high >> i) & 1U);
        if (shifted >= CLOCK_TICK_HZ) {
            remainder = (uint32_t)(shifted - CLOCK_TICK_HZ);
            seconds_high |= 1U << i;
        } else {
            remainder = (uint32_t)shifted;
        }
    }
    for (i = 32; i-- > 0;) {
        uint64_t shifted = ((uint64_t)remainder << 1) |
                           ((low >> i) & 1U);
        if (shifted >= CLOCK_TICK_HZ) {
            remainder = (uint32_t)(shifted - CLOCK_TICK_HZ);
            seconds_low |= 1U << i;
        } else {
            remainder = (uint32_t)shifted;
        }
    }
    if (seconds_high != 0 || seconds_low > 0x7FFFFFFFU) {
        store->tv_sec = 0x7FFFFFFF;
        store->tv_nsec = 999999999;
        return;
    }
    nanoseconds = remainder * CLOCK_NSEC_PER_TICK;
    store->tv_sec = (int32_t)seconds_low;
    store->tv_nsec = (int32_t)nanoseconds;
}

static int
sys_clock_gettime(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct timespec value;
    uint64_t clock_value;
    int clock_id = (int)arg[0];
    if (mm == NULL || arg[1] == 0 || !sys_clock_id_supported(clock_id)) {
        return -E_INVAL;
    }
    if (clock_id == CLOCK_PROCESS_CPUTIME_ID ||
        clock_id == CLOCK_THREAD_CPUTIME_ID) {
        clock_value = current->cpu_ticks;
    } else if (clock_id == CLOCK_REALTIME ||
               clock_id == CLOCK_REALTIME_COARSE) {
        clock_value = clock_realtime_ticks_read();
    } else {
        clock_value = clock_ticks_read();
    }
    sys_clock_ticks_to_timespec(clock_value, &value);
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &value, sizeof(value))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_clock_getres(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct timespec value;
    int clock_id = (int)arg[0];
    if (mm == NULL || arg[1] == 0 || !sys_clock_id_supported(clock_id)) {
        return -E_INVAL;
    }
    value.tv_sec = 0;
    value.tv_nsec = (int32_t)(1000000000U / CLOCK_TICK_HZ);
    if (value.tv_nsec == 0) value.tv_nsec = 1;
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &value, sizeof(value))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_gettimeofday(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct timespec realtime;
    struct timeval value;
    struct timezone zone;
    if (mm == NULL) return -E_INVAL;
    if (arg[0] != 0 && !user_mem_check(mm, (uintptr_t)arg[0],
                                       sizeof(value), 1)) return -E_INVAL;
    if (arg[1] != 0 && !user_mem_check(mm, (uintptr_t)arg[1],
                                       sizeof(zone), 1)) return -E_INVAL;
    sys_clock_ticks_to_timespec(clock_realtime_ticks_read(), &realtime);
    value.tv_sec = realtime.tv_sec;
    value.tv_usec = realtime.tv_nsec / 1000;
    zone.tz_minuteswest = 0;
    zone.tz_dsttime = 0;
    lock_mm(mm);
    if (arg[0] != 0 && !copy_to_user(mm, (void *)arg[0], &value,
                                     sizeof(value))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    if (arg[1] != 0 && !copy_to_user(mm, (void *)arg[1], &zone,
                                     sizeof(zone))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_nanosleep(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct timespec request, remaining;
    uint32_t requested_ticks, fractional_ticks;
    uint64_t start_ticks, elapsed_ticks;
    int ret;
    if (mm == NULL || arg[0] == 0) return -E_INVAL;
    lock_mm(mm);
    ret = copy_from_user(mm, &request, (void *)arg[0], sizeof(request), 0);
    if (ret && arg[1] != 0) ret = user_mem_check(mm, (uintptr_t)arg[1],
                                                 sizeof(remaining), 1);
    unlock_mm(mm);
    if (!ret || request.tv_sec < 0 || request.tv_nsec < 0 ||
        request.tv_nsec >= 1000000000) return -E_INVAL;
    if ((uint32_t)request.tv_sec > 0xFFFFFFFFU / CLOCK_TICK_HZ)
        return -E_TOO_BIG;
    requested_ticks = (uint32_t)request.tv_sec * CLOCK_TICK_HZ;
    fractional_ticks = (uint32_t)request.tv_nsec;
    fractional_ticks = (fractional_ticks + CLOCK_NSEC_PER_TICK - 1U) /
                       CLOCK_NSEC_PER_TICK;
    if (requested_ticks > 0xFFFFFFFFU - fractional_ticks)
        return -E_TOO_BIG;
    requested_ticks += fractional_ticks;
    start_ticks = clock_ticks_read();
    ret = requested_ticks == 0 ? 0 : do_sleep((unsigned int)requested_ticks);
    elapsed_ticks = clock_ticks_read() - start_ticks;
    if (elapsed_ticks >= requested_ticks) {
        remaining.tv_sec = 0;
        remaining.tv_nsec = 0;
    } else {
        sys_clock_ticks_to_timespec(requested_ticks - elapsed_ticks, &remaining);
    }
    if (arg[1] != 0) {
        lock_mm(mm);
        if (!copy_to_user(mm, (void *)arg[1], &remaining, sizeof(remaining)))
            ret = -E_INVAL;
        unlock_mm(mm);
    }
    return ret;
}

static int
sys_time(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    uint64_t seconds = clock_realtime_seconds();
    int32_t value = seconds > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int32_t)seconds;
    if (arg[0] != 0) {
        if (mm == NULL || !user_mem_check(mm, (uintptr_t)arg[0],
                                           sizeof(value), 1)) return -E_INVAL;
        lock_mm(mm);
        if (!copy_to_user(mm, (void *)arg[0], &value, sizeof(value))) {
            unlock_mm(mm);
            return -E_INVAL;
        }
        unlock_mm(mm);
    }
    return value;
}

static uint32_t
sys_lab6_set_priority(uint32_t arg[])
{
    uint32_t priority = (uint32_t)arg[0];
    lab6_set_priority(priority);
    return 0;
}

static int
sys_sleep(uint32_t arg[]) {
    unsigned int time = (unsigned int)arg[0];
    return do_sleep(time);
}

static int
sys_open(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    uint32_t open_flags = (uint32_t)arg[1];
    return sysfile_open(path, open_flags);
}

static int
sys_close(uint32_t arg[]) {
    int fd = (int)arg[0];
    return sysfile_close(fd);
}

static int
sys_read(uint32_t arg[]) {
    int fd = (int)arg[0];
    void *base = (void *)arg[1];
    size_t len = (size_t)arg[2];
    return sysfile_read(fd, base, len);
}

static int
sys_write(uint32_t arg[]) {
    int fd = (int)arg[0];
    void *base = (void *)arg[1];
    size_t len = (size_t)arg[2];
    return sysfile_write(fd, base, len);
}

static int
sys_seek(uint32_t arg[]) {
    int fd = (int)arg[0];
    off_t pos = (off_t)arg[1];
    int whence = (int)arg[2];
    return sysfile_seek(fd, pos, whence);
}

static int
sys_fstat(uint32_t arg[]) {
    int fd = (int)arg[0];
    struct stat *stat = (struct stat *)arg[1];
    return sysfile_fstat(fd, stat);
}

static int
sys_stat(uint32_t arg[]) {
    return sysfile_stat((const char *)arg[0],
                        (struct stat *)arg[1], 0);
}

static int
sys_lstat(uint32_t arg[]) {
    return sysfile_stat((const char *)arg[0],
                        (struct stat *)arg[1], 1);
}

static int
sys_fsync(uint32_t arg[]) {
    int fd = (int)arg[0];
    return sysfile_fsync(fd);
}

static int
sys_ftruncate(uint32_t arg[]) {
    return sysfile_ftruncate((int)arg[0], (off_t)arg[1]);
}

static int
sys_truncate(uint32_t arg[]) {
    return sysfile_truncate((const char *)arg[0], (off_t)arg[1]);
}

static int
sys_pread(uint32_t arg[]) {
    return sysfile_pread((int)arg[0], (void *)arg[1],
                         (size_t)arg[2], (off_t)arg[3]);
}

static int
sys_pwrite(uint32_t arg[]) {
    return sysfile_pwrite((int)arg[0], (const void *)arg[1],
                          (size_t)arg[2], (off_t)arg[3]);
}

static int
sys_readv(uint32_t arg[]) {
    return sysfile_readv((int)arg[0],
                         (const struct iovec *)arg[1], (size_t)arg[2]);
}

static int
sys_writev(uint32_t arg[]) {
    return sysfile_writev((int)arg[0],
                          (const struct iovec *)arg[1], (size_t)arg[2]);
}

static int
sys_chdir(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_chdir(path);
}

static int
sys_fchdir(uint32_t arg[]) {
    return sysfile_fchdir((int)arg[0]);
}

static int
sys_mkdir(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_mkdir(path);
}

static int
sys_link(uint32_t arg[]) {
    const char *old_path = (const char *)arg[0];
    const char *new_path = (const char *)arg[1];
    return sysfile_link(old_path, new_path);
}

static int
sys_unlink(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_unlink(path);
}

static int
sys_rmdir(uint32_t arg[]) {
    return sysfile_rmdir((const char *)arg[0]);
}

static int
sys_rename(uint32_t arg[]) {
    const char *old_path = (const char *)arg[0];
    const char *new_path = (const char *)arg[1];
    return sysfile_rename(old_path, new_path);
}

static int
sys_getcwd(uint32_t arg[]) {
    char *buf = (char *)arg[0];
    size_t len = (size_t)arg[1];
    return sysfile_getcwd(buf, len);
}

static int
sys_getdirentry(uint32_t arg[]) {
    int fd = (int)arg[0];
    struct dirent *direntp = (struct dirent *)arg[1];
    return sysfile_getdirentry(fd, direntp);
}

static int
sys_dup(uint32_t arg[]) {
    int fd1 = (int)arg[0];
    int fd2 = (int)arg[1];
    return sysfile_dup(fd1, fd2);
}

static int
sys_pipe(uint32_t arg[]) {
    return sysfile_pipe((int *)arg[0]);
}

static int
sys_pipe2(uint32_t arg[]) {
    return sysfile_pipe2((int *)arg[0], arg[1]);
}

static int
sys_socket(uint32_t arg[]) {
    return file_socket_create((int)arg[0], (int)arg[1], (int)arg[2]);
}

static int
sys_bind(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in address;
    int ret;

    if (mm == NULL || (size_t)arg[2] < sizeof(address)) {
        return -E_INVAL;
    }
    lock_mm(mm);
    ret = copy_from_user(mm, &address, (void *)arg[1], sizeof(address), 0);
    unlock_mm(mm);
    if (!ret) {
        return -E_INVAL;
    }
    return file_socket_bind((int)arg[0], &address, (size_t)arg[2]);
}

static int
sys_sendto(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in destination;
    void *buffer;
    size_t length = (size_t)arg[2];
    int ret;

    if (mm == NULL || length == 0 || length > NET_MAX_DATAGRAM) {
        return -E_INVAL;
    }
    if ((buffer = kmalloc(length)) == NULL) {
        return -E_NO_MEM;
    }
    lock_mm(mm);
    ret = copy_from_user(mm, buffer, (void *)arg[1], length, 0) &&
          copy_from_user(mm, &destination, (void *)arg[3],
                         sizeof(destination), 0);
    unlock_mm(mm);
    if (!ret) {
        kfree(buffer);
        return -E_INVAL;
    }
    ret = file_socket_sendto((int)arg[0], buffer, length,
                             &destination, (size_t)arg[4]);
    kfree(buffer);
    return ret;
}

static int
sys_recvfrom(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in source;
    size_t source_length = sizeof(source);
    void *buffer;
    size_t length = (size_t)arg[2];
    int ret;

    if (mm == NULL || length == 0 || length > NET_MAX_DATAGRAM) {
        return -E_INVAL;
    }
    if ((buffer = kmalloc(length)) == NULL) {
        return -E_NO_MEM;
    }
    ret = file_socket_recvfrom((int)arg[0], buffer, length,
                               &source, &source_length);
    if (ret > 0) {
        lock_mm(mm);
        if (!copy_to_user(mm, (void *)arg[1], buffer, (size_t)ret) ||
            (arg[3] != 0 &&
             !copy_to_user(mm, (void *)arg[3], &source, sizeof(source)))) {
            ret = -E_INVAL;
        }
        unlock_mm(mm);
    }
    kfree(buffer);
    return ret;
}

static int
sys_netstat(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct net_stats stats;

    if (mm == NULL) {
        return -E_INVAL;
    }
    net_get_stats(&stats);
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[0], &stats, sizeof(stats))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_connect(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in address;
    int ret;
    if (mm == NULL || (size_t)arg[2] < sizeof(address)) {
        return -E_INVAL;
    }
    lock_mm(mm);
    ret = copy_from_user(mm, &address, (void *)arg[1], sizeof(address), 0);
    unlock_mm(mm);
    if (!ret) {
        return -E_INVAL;
    }
    return file_socket_connect((int)arg[0], &address, (size_t)arg[2]);
}

static int
sys_listen(uint32_t arg[]) {
    return file_socket_listen((int)arg[0], (int)arg[1]);
}

static int
sys_accept(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in address;
    int ret;

    if (mm == NULL || (arg[1] != 0 && (size_t)arg[2] < sizeof(address))) {
        return -E_INVAL;
    }
    ret = file_socket_accept((int)arg[0], &address, 0);
    if (ret < 0 || arg[1] == 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &address, sizeof(address))) {
        unlock_mm(mm);
        file_close(ret);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return ret;
}

static int
sys_shutdown(uint32_t arg[]) {
    if (arg[1] > SHUT_RDWR) {
        return -E_INVAL;
    }
    return file_socket_shutdown((int)arg[0], (int)arg[1]);
}

static int
sys_send(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    void *buffer;
    size_t length = (size_t)arg[2];
    int ret;
    if (mm == NULL || length == 0 || length > NET_TCP_MAX_WRITE) {
        return -E_INVAL;
    }
    if ((buffer = kmalloc(length)) == NULL) {
        return -E_NO_MEM;
    }
    lock_mm(mm);
    ret = copy_from_user(mm, buffer, (void *)arg[1], length, 0);
    unlock_mm(mm);
    if (!ret) {
        kfree(buffer);
        return -E_INVAL;
    }
    ret = file_socket_send((int)arg[0], buffer, length);
    kfree(buffer);
    return ret;
}

static int
sys_recv(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    void *buffer;
    size_t length = (size_t)arg[2];
    int ret;
    if (mm == NULL || length == 0 || length > NET_TCP_MAX_WRITE) {
        return -E_INVAL;
    }
    if ((buffer = kmalloc(length)) == NULL) {
        return -E_NO_MEM;
    }
    ret = file_socket_recv((int)arg[0], buffer, length);
    if (ret > 0) {
        lock_mm(mm);
        if (!copy_to_user(mm, (void *)arg[1], buffer, (size_t)ret)) {
            ret = -E_INVAL;
        }
        unlock_mm(mm);
    }
    kfree(buffer);
    return ret;
}

static int
sys_getsockname(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in address;
    int ret;
    if (mm == NULL || arg[1] == 0 || (size_t)arg[2] < sizeof(address)) {
        return -E_INVAL;
    }
    ret = file_socket_getsockname((int)arg[0], &address);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &address, sizeof(address))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    return ret;
}

static int
sys_getpeername(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct sockaddr_in address;
    int ret;
    if (mm == NULL || arg[1] == 0 || (size_t)arg[2] < sizeof(address)) {
        return -E_INVAL;
    }
    ret = file_socket_getpeername((int)arg[0], &address);
    if (ret != 0) {
        return ret;
    }
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)arg[1], &address, sizeof(address))) {
        ret = -E_INVAL;
    }
    unlock_mm(mm);
    return ret;
}

static int
sys_getsockopt(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    int level = (int)arg[1];
    int option = (int)arg[2];
    uint32_t length;
    int value;
    int ret;
    if (mm == NULL || arg[3] == 0 || arg[4] == 0) return -E_INVAL;
    lock_mm(mm);
    ret = copy_from_user(mm, &length, (void *)arg[4], sizeof(length), 1);
    unlock_mm(mm);
    if (!ret || length < sizeof(value)) return -E_INVAL;
    ret = file_socket_getsockopt((int)arg[0], level, option, &value,
                                 (size_t *)&length);
    if (ret != 0) return ret;
    lock_mm(mm);
    ret = copy_to_user(mm, (void *)arg[3], &value, sizeof(value)) &&
          copy_to_user(mm, (void *)arg[4], &length, sizeof(length)) ? 0 : -E_INVAL;
    unlock_mm(mm);
    return ret;
}

static int
sys_setsockopt(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    void *value;
    size_t length = (size_t)arg[4];
    int ret;
    if (mm == NULL || arg[3] == 0 || length < sizeof(int) || length > 256) {
        return -E_INVAL;
    }
    value = kmalloc(length);
    if (value == NULL) return -E_NO_MEM;
    lock_mm(mm);
    ret = copy_from_user(mm, value, (void *)arg[3], length, 0);
    unlock_mm(mm);
    if (!ret) {
        kfree(value);
        return -E_INVAL;
    }
    ret = file_socket_setsockopt((int)arg[0], (int)arg[1], (int)arg[2],
                                 value, length);
    kfree(value);
    return ret;
}

static int
sys_fcntl(uint32_t arg[]) {
    return file_fcntl((int)arg[0], (int)arg[1], arg[2]);
}

static int
sys_poll(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    struct pollfd *fds;
    size_t count = (size_t)arg[1];
    int timeout = (int)arg[2];
    size_t bytes;
    size_t i;
    size_t start;
    int ready;
    int ret = 0;

    if (mm == NULL || count > FS_POLL_MAX_FDS ||
        (count != 0 && arg[0] == 0) || timeout < -1) {
        return -E_INVAL;
    }
    bytes = count * sizeof(struct pollfd);
    fds = count == 0 ? NULL : kmalloc(bytes);
    if (count != 0 && fds == NULL) {
        return -E_NO_MEM;
    }
    lock_mm(mm);
    if (count != 0 && !copy_from_user(mm, fds, (void *)arg[0], bytes, 0)) {
        unlock_mm(mm);
        kfree(fds);
        return -E_INVAL;
    }
    unlock_mm(mm);

    start = ticks;
    for (;;) {
        ready = 0;
        for (i = 0; i < count; i++) {
            int16_t revents = 0;
            if (file_poll(fds[i].fd, fds[i].events, &revents) != 0) {
                revents = POLLNVAL;
            }
            fds[i].revents = revents;
            if (revents != 0) {
                ready++;
            }
        }
        if (ready != 0 || timeout == 0) {
            ret = ready;
            break;
        }
        if (timeout > 0 && (size_t)(ticks - start) >=
            ((size_t)timeout + 9) / 10) {
            ret = 0;
            break;
        }
        if (do_sleep(1) == -E_INTR) {
            ret = -E_INTR;
            break;
        }
    }
    if (count != 0) {
        lock_mm(mm);
        if (!copy_to_user(mm, (void *)arg[0], fds, bytes)) {
            ret = -E_INVAL;
        }
        unlock_mm(mm);
    }
    kfree(fds);
    return ret;
}

static int
sys_select_copy_set(struct mm_struct *mm, uintptr_t address, fd_set *store) {
    if (address == 0) {
        FD_ZERO(store);
        return 0;
    }
    lock_mm(mm);
    if (!copy_from_user(mm, store, (void *)address, sizeof(*store), 0)) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_select_store_set(struct mm_struct *mm, uintptr_t address,
                     const fd_set *source) {
    if (address == 0) return 0;
    lock_mm(mm);
    if (!copy_to_user(mm, (void *)address, source, sizeof(*source))) {
        unlock_mm(mm);
        return -E_INVAL;
    }
    unlock_mm(mm);
    return 0;
}

static int
sys_select(uint32_t arg[]) {
    struct mm_struct *mm = current->mm;
    fd_set read_in, write_in, except_in;
    fd_set read_out, write_out, except_out;
    struct timeval timeout_value;
    int nfds = (int)arg[0];
    int timeout_mode = 0;
    uint32_t timeout_ticks = 0;
    uint32_t usec_per_tick;
    uint64_t start_ticks;
    int ret = 0;

    if (mm == NULL || nfds < 0 || nfds > UCORE_FD_SETSIZE) return -E_INVAL;
    if (sys_select_copy_set(mm, (uintptr_t)arg[1], &read_in) != 0 ||
        sys_select_copy_set(mm, (uintptr_t)arg[2], &write_in) != 0 ||
        sys_select_copy_set(mm, (uintptr_t)arg[3], &except_in) != 0) {
        return -E_INVAL;
    }
    {
        int fd;
        for (fd = 0; fd < nfds; fd++) {
            if (FD_ISSET(fd, &except_in)) return -E_UNIMP;
        }
    }
    if (arg[4] != 0) {
        lock_mm(mm);
        if (!copy_from_user(mm, &timeout_value, (void *)arg[4],
                            sizeof(timeout_value), 0)) {
            unlock_mm(mm);
            return -E_INVAL;
        }
        unlock_mm(mm);
        if (timeout_value.tv_sec < 0 || timeout_value.tv_usec < 0 ||
            timeout_value.tv_usec >= 1000000) return -E_INVAL;
        if ((uint32_t)timeout_value.tv_sec > 0xFFFFFFFFU / CLOCK_TICK_HZ) {
            return -E_TOO_BIG;
        }
        usec_per_tick = 1000000U / CLOCK_TICK_HZ;
        if (usec_per_tick == 0) usec_per_tick = 1;
        timeout_ticks = (uint32_t)timeout_value.tv_sec * CLOCK_TICK_HZ;
        if (timeout_value.tv_usec != 0) {
            uint32_t fraction = ((uint32_t)timeout_value.tv_usec +
                                 usec_per_tick - 1U) / usec_per_tick;
            if (timeout_ticks > 0xFFFFFFFFU - fraction) return -E_TOO_BIG;
            timeout_ticks += fraction;
        }
        timeout_mode = 1;
    }
    start_ticks = clock_ticks_read();
    for (;;) {
        int fd;
        int ready = 0;
        FD_ZERO(&read_out);
        FD_ZERO(&write_out);
        FD_ZERO(&except_out);
        for (fd = 0; fd < nfds; fd++) {
            int16_t events = 0;
            int16_t revents = 0;
            bool selected = 0;
            if (FD_ISSET(fd, &read_in)) events |= POLLIN;
            if (FD_ISSET(fd, &write_in)) events |= POLLOUT;
            if (events == 0) continue;
            ret = file_poll(fd, events, &revents);
            if (ret != 0) goto select_done;
            if ((revents & POLLNVAL) != 0) {
                ret = -E_INVAL;
                goto select_done;
            }
            if (FD_ISSET(fd, &read_in) &&
                (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                FD_SET(fd, &read_out);
                selected = 1;
            }
            if (FD_ISSET(fd, &write_in) &&
                (revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
                FD_SET(fd, &write_out);
                selected = 1;
            }
            if (selected) ready++;
        }
        if (ready != 0 || (timeout_mode && timeout_ticks == 0)) {
            ret = ready;
            break;
        }
        if (timeout_mode &&
            (uint64_t)(clock_ticks_read() - start_ticks) >= timeout_ticks) {
            ret = 0;
            break;
        }
        if (do_sleep(1) == -E_INTR) {
            ret = -E_INTR;
            break;
        }
    }
select_done:
    if (ret >= 0 || ret == -E_INTR) {
        if (sys_select_store_set(mm, (uintptr_t)arg[1], &read_out) != 0 ||
            sys_select_store_set(mm, (uintptr_t)arg[2], &write_out) != 0 ||
            sys_select_store_set(mm, (uintptr_t)arg[3], &except_out) != 0) {
            return -E_INVAL;
        }
    }
    return ret;
}

static int (*syscalls[])(uint32_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_clone]             sys_clone,
    [SYS_wait]              sys_wait,
    [SYS_wait4]             sys_wait4,
    [SYS_waitid]            sys_waitid,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_raise]             sys_raise,
    [SYS_sigaction]         sys_sigaction,
    [SYS_sigprocmask]       sys_sigprocmask,
    [SYS_sigreturn]         sys_sigreturn,
    [SYS_stat]              sys_stat,
    [SYS_lstat]             sys_lstat,
    [SYS_ftruncate]         sys_ftruncate,
    [SYS_truncate]          sys_truncate,
    [SYS_pread]             sys_pread,
    [SYS_pwrite]            sys_pwrite,
    [SYS_readv]             sys_readv,
    [SYS_writev]            sys_writev,
    [SYS_getpid]            sys_getpid,
    [SYS_getppid]           sys_getppid,
    [SYS_gettid]            sys_gettid,
    [SYS_getcpu]            sys_getcpu,
    [SYS_uname]             sys_uname,
    [SYS_sysinfo]           sys_sysinfo,
    [SYS_getuid]            sys_getuid,
    [SYS_geteuid]           sys_geteuid,
    [SYS_getgid]            sys_getgid,
    [SYS_getegid]           sys_getegid,
    [SYS_getresuid]         sys_getresuid,
    [SYS_getresgid]         sys_getresgid,
    [SYS_mmap]              sys_mmap,
    [SYS_munmap]            sys_munmap,
    [SYS_mprotect]          sys_mprotect,
    [SYS_brk]               sys_brk,
    [SYS_setaffinity]       sys_setaffinity,
    [SYS_getaffinity]       sys_getaffinity,
    [SYS_getcpustat]        sys_getcpustat,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
    [SYS_gettime]           sys_gettime,
    [SYS_clock_gettime]     sys_clock_gettime,
    [SYS_clock_getres]      sys_clock_getres,
    [SYS_gettimeofday]      sys_gettimeofday,
    [SYS_nanosleep]         sys_nanosleep,
    [SYS_time]              sys_time,
    [SYS_lab6_set_priority] sys_lab6_set_priority,
    [SYS_sleep]             sys_sleep,
    [SYS_open]              sys_open,
    [SYS_close]             sys_close,
    [SYS_read]              sys_read,
    [SYS_write]             sys_write,
    [SYS_seek]              sys_seek,
    [SYS_fstat]             sys_fstat,
    [SYS_fsync]             sys_fsync,
    [SYS_chdir]             sys_chdir,
    [SYS_fchdir]            sys_fchdir,
    [SYS_mkdir]             sys_mkdir,
    [SYS_link]              sys_link,
    [SYS_unlink]            sys_unlink,
    [SYS_rmdir]             sys_rmdir,
    [SYS_rename]            sys_rename,
    [SYS_getcwd]            sys_getcwd,
    [SYS_getdirentry]       sys_getdirentry,
    [SYS_dup]               sys_dup,
    [SYS_pipe]              sys_pipe,
    [SYS_pipe2]             sys_pipe2,
    [SYS_socket]            sys_socket,
    [SYS_bind]              sys_bind,
    [SYS_sendto]            sys_sendto,
    [SYS_recvfrom]          sys_recvfrom,
    [SYS_netstat]           sys_netstat,
    [SYS_connect]           sys_connect,
    [SYS_send]              sys_send,
    [SYS_recv]              sys_recv,
    [SYS_getsockname]       sys_getsockname,
    [SYS_getpeername]       sys_getpeername,
    [SYS_getsockopt]        sys_getsockopt,
    [SYS_setsockopt]        sys_setsockopt,
    [SYS_listen]            sys_listen,
    [SYS_accept]            sys_accept,
    [SYS_shutdown]          sys_shutdown,
    [SYS_fcntl]             sys_fcntl,
    [SYS_poll]              sys_poll,
    [SYS_select]            sys_select,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

void
syscall(void) {
    struct trapframe *tf = current->tf;
    uint32_t arg[5];
    int num = tf->tf_regs.reg_eax;
    if (num >= 0 && num < NUM_SYSCALLS) {
        if (syscalls[num] != NULL) {
            arg[0] = tf->tf_regs.reg_edx;
            arg[1] = tf->tf_regs.reg_ecx;
            arg[2] = tf->tf_regs.reg_ebx;
            arg[3] = tf->tf_regs.reg_edi;
            arg[4] = tf->tf_regs.reg_esi;
            {
                int ret = syscalls[num](arg);
                /* sigreturn replaces the entire saved trap frame. Writing a
                 * normal syscall return value afterward would overwrite the
                 * restored user EAX and resume at the wrong ABI state. */
                if (num != SYS_sigreturn) {
                    tf->tf_regs.reg_eax = ret;
                }
            }
            return ;
        }
    }
    /* Unknown or reserved calls are a user-visible capability boundary.
     * Match the normal negative-error convention instead of taking the whole
     * kernel into the monitor for a harmless feature probe. */
    tf->tf_regs.reg_eax = -E_UNIMP;
}
