#include <error.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

int
main(void) {
    struct utsname name;
    struct sysinfo info;
    uint32_t real_uid, effective_uid, saved_uid;
    uint32_t real_gid, effective_gid, saved_gid;

    assert(uname(&name) == 0);
    assert(name.sysname[0] != '\0' && name.nodename[0] != '\0');
    assert(name.release[0] != '\0' && name.version[0] != '\0');
    assert(name.machine[0] != '\0' && name.domainname[0] != '\0');
    assert(uname(NULL) == -E_INVAL);

    assert(sysinfo(&info) == 0);
    assert(info.uptime >= 0);
    assert(info.totalram > 0 && info.freeram <= info.totalram);
    assert(info.totalswap >= info.freeswap);
    assert(info.procs >= 1 && info.mem_unit == UCORE_PAGE_SIZE);
    assert(sysinfo(NULL) == -E_INVAL);

    assert(getuid() == geteuid());
    assert(getgid() == getegid());
    assert(getresuid(&real_uid, &effective_uid, &saved_uid) == 0);
    assert(real_uid == effective_uid && effective_uid == saved_uid);
    assert(getresgid(&real_gid, &effective_gid, &saved_gid) == 0);
    assert(real_gid == effective_gid && effective_gid == saved_gid);
    assert(getresuid(NULL, NULL, NULL) == 0);
    assert(getresgid(NULL, NULL, NULL) == 0);
    assert(getresuid((uint32_t *)1, NULL, NULL) == -E_INVAL);
    assert(getresgid((uint32_t *)1, NULL, NULL) == -E_INVAL);

    cprintf("uname, sysinfo and identity query test pass. %s %s\n",
            name.sysname, name.machine);
    return 0;
}
