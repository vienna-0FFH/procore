#ifndef __UCORE_TCC_SEMAPHORE_H__
#define __UCORE_TCC_SEMAPHORE_H__

typedef struct {
    volatile int value;
} sem_t;

int sem_init(sem_t *, int, unsigned int);
int sem_destroy(sem_t *);
int sem_wait(sem_t *);
int sem_post(sem_t *);

#endif
