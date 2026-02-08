#ifndef CLAUDE_HEAP_H
#define CLAUDE_HEAP_H

#include <stddef.h>

/* Initialize kernel heap allocator */
void kheap_init(void);

/* Allocate/free kernel heap memory */
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif /* CLAUDE_HEAP_H */
