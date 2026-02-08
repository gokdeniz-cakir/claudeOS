#include "sync.h"

#include "process.h"

static inline void sync_wait_hint(void)
{
    if (process_is_preemption_enabled() != 0U) {
        process_yield();
    } else {
        __asm__ volatile ("pause");
    }
}

void semaphore_init(struct semaphore *sem, int32_t initial_count)
{
    if (sem == 0) {
        return;
    }

    if (initial_count < 0) {
        initial_count = 0;
    }

    sem->count = initial_count;
    spinlock_init(&sem->lock);
}

void semaphore_wait(struct semaphore *sem)
{
    uint32_t flags;

    if (sem == 0) {
        return;
    }

    for (;;) {
        flags = spinlock_lock_irqsave(&sem->lock);
        if (sem->count > 0) {
            sem->count--;
            spinlock_unlock_irqrestore(&sem->lock, flags);
            return;
        }
        spinlock_unlock_irqrestore(&sem->lock, flags);
        sync_wait_hint();
    }
}

void semaphore_signal(struct semaphore *sem)
{
    uint32_t flags;

    if (sem == 0) {
        return;
    }

    flags = spinlock_lock_irqsave(&sem->lock);
    sem->count++;
    spinlock_unlock_irqrestore(&sem->lock, flags);
}

int32_t semaphore_value(struct semaphore *sem)
{
    int32_t snapshot;
    uint32_t flags;

    if (sem == 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&sem->lock);
    snapshot = sem->count;
    spinlock_unlock_irqrestore(&sem->lock, flags);
    return snapshot;
}

void mutex_init(struct mutex *mutex)
{
    if (mutex == 0) {
        return;
    }

    semaphore_init(&mutex->sem, 1);
}

void mutex_lock(struct mutex *mutex)
{
    if (mutex == 0) {
        return;
    }

    semaphore_wait(&mutex->sem);
}

void mutex_unlock(struct mutex *mutex)
{
    if (mutex == 0) {
        return;
    }

    semaphore_signal(&mutex->sem);
}
