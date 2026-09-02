#ifndef UCORE_BUILD_INIT_PROTOTYPES_H
#define UCORE_BUILD_INIT_PROTOTYPES_H

#ifndef __ASSEMBLER__
struct trapframe;
int mon_backtrace(int argc, char **argv, struct trapframe *tf);
void grade_backtrace(void);
#endif

#endif
