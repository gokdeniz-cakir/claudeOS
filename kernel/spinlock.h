#ifndef CLAUDE_SPINLOCK_H
#define CLAUDE_SPINLOCK_H

#include <stdint.h>

struct spinlock {
    volatile uint32_t value;
};

#define SPINLOCK_INITIALIZER { 0U }

/* Initialize an unlocked spinlock. */
void spinlock_init(struct spinlock *lock);

/* Acquire/release spinlock. */
void spinlock_lock(struct spinlock *lock);
uint8_t spinlock_try_lock(struct spinlock *lock);
void spinlock_unlock(struct spinlock *lock);

/* Save EFLAGS and disable interrupts on current CPU. */
uint32_t spinlock_irq_save(void);

/* Restore EFLAGS previously returned by spinlock_irq_save(). */
void spinlock_irq_restore(uint32_t flags);

/* Convenience helpers for IRQ-safe lock sections. */
uint32_t spinlock_lock_irqsave(struct spinlock *lock);
void spinlock_unlock_irqrestore(struct spinlock *lock, uint32_t flags);

#endif /* CLAUDE_SPINLOCK_H */
