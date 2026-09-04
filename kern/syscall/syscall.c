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
    return do_kill(pid);
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
sys_fsync(uint32_t arg[]) {
    int fd = (int)arg[0];
    return sysfile_fsync(fd);
}

static int
sys_chdir(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_chdir(path);
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

static int (*syscalls[])(uint32_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_clone]             sys_clone,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_getppid]           sys_getppid,
    [SYS_gettid]            sys_gettid,
    [SYS_getcpu]            sys_getcpu,
    [SYS_mmap]              sys_mmap,
    [SYS_munmap]            sys_munmap,
    [SYS_brk]               sys_brk,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
    [SYS_gettime]           sys_gettime,
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
    [SYS_mkdir]             sys_mkdir,
    [SYS_link]              sys_link,
    [SYS_unlink]            sys_unlink,
    [SYS_rename]            sys_rename,
    [SYS_getcwd]            sys_getcwd,
    [SYS_getdirentry]       sys_getdirentry,
    [SYS_dup]               sys_dup,
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
            tf->tf_regs.reg_eax = syscalls[num](arg);
            return ;
        }
    }
    /* Unknown or reserved calls are a user-visible capability boundary.
     * Match the normal negative-error convention instead of taking the whole
     * kernel into the monitor for a harmless feature probe. */
    tf->tf_regs.reg_eax = -E_UNIMP;
}
