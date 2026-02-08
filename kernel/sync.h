#ifndef CLAUDE_SYNC_H
#define CLAUDE_SYNC_H

#include <stdint.h>

#include "spinlock.h"

struct semaphore {
    int32_t count;
    struct spinlock lock;
};

/* Initialize counting semaphore with initial token count (>= 0). */
void semaphore_init(struct semaphore *sem, int32_t initial_count);

/* Busy-waiting semaphore operations (wait must not be called in IRQ context). */
void semaphore_wait(struct semaphore *sem);
void semaphore_signal(struct semaphore *sem);

/* Read current semaphore value. */
int32_t semaphore_value(struct semaphore *sem);

struct mutex {
    struct semaphore sem;
};

/* Binary semaphore helpers. */
void mutex_init(struct mutex *mutex);
void mutex_lock(struct mutex *mutex);
void mutex_unlock(struct mutex *mutex);

#endif /* CLAUDE_SYNC_H */
