#include "stdlib.h"

#include <stdint.h>

#include "unistd.h"

struct malloc_block {
    uint32_t size;
    uint8_t free;
    struct malloc_block *next;
};

#define MALLOC_ALIGN  8U

static struct malloc_block *malloc_head = 0;

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static struct malloc_block *find_fit(uint32_t size)
{
    struct malloc_block *cur = malloc_head;

    while (cur != 0) {
        if (cur->free != 0U && cur->size >= size) {
            return cur;
        }
        cur = cur->next;
    }

    return 0;
}

static void split_block(struct malloc_block *block, uint32_t size)
{
    uint32_t header_size = align_up_u32((uint32_t)sizeof(struct malloc_block),
                                        MALLOC_ALIGN);

    if (block == 0 || block->size <= size + header_size + MALLOC_ALIGN) {
        return;
    }

    {
        uint8_t *base = (uint8_t *)(uintptr_t)block;
        struct malloc_block *new_block =
            (struct malloc_block *)(void *)(base + header_size + size);

        new_block->size = block->size - size - header_size;
        new_block->free = 1U;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;
    }
}

static struct malloc_block *request_more(uint32_t size)
{
    uint32_t header_size = align_up_u32((uint32_t)sizeof(struct malloc_block),
                                        MALLOC_ALIGN);
    uint32_t total_size = header_size + size;
    struct malloc_block *block = (struct malloc_block *)sbrk((int32_t)total_size);
    struct malloc_block *tail;

    if (block == (void *)0xFFFFFFFFU) {
        return 0;
    }

    block->size = size;
    block->free = 0U;
    block->next = 0;

    if (malloc_head == 0) {
        malloc_head = block;
        return block;
    }

    tail = malloc_head;
    while (tail->next != 0) {
        tail = tail->next;
    }

    tail->next = block;
    return block;
}

void *malloc(size_t req_size)
{
    struct malloc_block *block;
    uint32_t size;
    uint32_t header_size = align_up_u32((uint32_t)sizeof(struct malloc_block),
                                        MALLOC_ALIGN);

    if (req_size == 0U) {
        return 0;
    }

    if (req_size > 0xFFFFFF00U) {
        return 0;
    }

    size = align_up_u32((uint32_t)req_size, MALLOC_ALIGN);
    block = find_fit(size);
    if (block == 0) {
        block = request_more(size);
        if (block == 0) {
            return 0;
        }
    } else {
        block->free = 0U;
        split_block(block, size);
    }

    return (void *)((uint8_t *)(uintptr_t)block + header_size);
}

void free(void *ptr)
{
    struct malloc_block *cur;
    struct malloc_block *prev;
    uint8_t *raw;
    uint32_t header_size = align_up_u32((uint32_t)sizeof(struct malloc_block),
                                        MALLOC_ALIGN);

    if (ptr == 0) {
        return;
    }

    raw = (uint8_t *)(uintptr_t)ptr;
    cur = (struct malloc_block *)(void *)(raw - header_size);
    cur->free = 1U;

    /* Forward coalescing */
    while (cur->next != 0 && cur->next->free != 0U) {
        cur->size += header_size + cur->next->size;
        cur->next = cur->next->next;
    }

    /* Backward coalescing */
    prev = 0;
    cur = malloc_head;
    while (cur != 0 && cur != (struct malloc_block *)(void *)(raw - header_size)) {
        prev = cur;
        cur = cur->next;
    }

    if (prev != 0 && prev->free != 0U && cur != 0) {
        prev->size += header_size + cur->size;
        prev->next = cur->next;
    }
}
