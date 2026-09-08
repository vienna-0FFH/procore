#ifndef __UCORE_TCC_SETJMP_H__
#define __UCORE_TCC_SETJMP_H__

typedef unsigned int jmp_buf[8];

int setjmp(jmp_buf);
void longjmp(jmp_buf, int) __attribute__((noreturn));

#endif
