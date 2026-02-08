#include "spinlock.h"

#include <stdint.h>

static inline void cpu_pause(void)
{
    __asm__ volatile ("pause");
}

static inline uint8_t spinlock_try_acquire_raw(volatile uint32_t *value)
{
    uint32_t previous = 1U;
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(previous), "+m"(*value)
                      :
                      : "memory");
    return (uint8_t)(previous == 0U);
}

void spinlock_init(struct spinlock *lock)
{
    if (lock == 0) {
        return;
    }

    lock->value = 0U;
}

void spinlock_lock(struct spinlock *lock)
{
    if (lock == 0) {
        return;
    }

    while (spinlock_try_acquire_raw(&lock->value) == 0U) {
        while (lock->value != 0U) {
            cpu_pause();
        }
    }
}

uint8_t spinlock_try_lock(struct spinlock *lock)
{
    if (lock == 0) {
        return 0U;
    }

    return spinlock_try_acquire_raw(&lock->value);
}

void spinlock_unlock(struct spinlock *lock)
{
    if (lock == 0) {
        return;
    }

    __asm__ volatile ("" : : : "memory");
    lock->value = 0U;
}

uint32_t spinlock_irq_save(void)
{
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void spinlock_irq_restore(uint32_t flags)
{
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
}

uint32_t spinlock_lock_irqsave(struct spinlock *lock)
{
    uint32_t flags = spinlock_irq_save();
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(struct spinlock *lock, uint32_t flags)
{
    spinlock_unlock(lock);
    spinlock_irq_restore(flags);
}
