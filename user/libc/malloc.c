#include "stdlib.h"

#include <stddef.h>
#include <stdint.h>

#include "string.h"
#include "unistd.h"

struct malloc_block {
    uint32_t size;
    uint8_t free;
    uint8_t _pad[3];
    struct malloc_block *next;
    struct malloc_block *prev;
};

#define MALLOC_ALIGN  8U

static struct malloc_block *malloc_head = 0;
static struct malloc_block *malloc_tail = 0;

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t malloc_header_size(void)
{
    return align_up_u32((uint32_t)sizeof(struct malloc_block), MALLOC_ALIGN);
}

static struct malloc_block *find_block_from_payload(void *ptr)
{
    uint8_t *raw;
    struct malloc_block *target;
    struct malloc_block *cur;
    uint32_t header_size = malloc_header_size();

    if (ptr == 0) {
        return 0;
    }

    raw = (uint8_t *)(uintptr_t)ptr;
    target = (struct malloc_block *)(void *)(raw - header_size);

    cur = malloc_head;
    while (cur != 0) {
        if (cur == target) {
            return cur;
        }
        cur = cur->next;
    }

    return 0;
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
    uint32_t header_size = malloc_header_size();

    if (block == 0 || block->size <= size + header_size + MALLOC_ALIGN) {
        return;
    }

    {
        uint8_t *base = (uint8_t *)(uintptr_t)block;
        struct malloc_block *new_block =
            (struct malloc_block *)(void *)(base + header_size + size);

        new_block->size = block->size - size - header_size;
        new_block->free = 1U;
        new_block->prev = block;
        new_block->next = block->next;
        if (new_block->next != 0) {
            new_block->next->prev = new_block;
        }

        block->size = size;
        block->next = new_block;

        if (malloc_tail == block) {
            malloc_tail = new_block;
        }
    }
}

static struct malloc_block *request_more(uint32_t size)
{
    uint32_t header_size = malloc_header_size();
    uint32_t total_size = header_size + size;
    struct malloc_block *block = (struct malloc_block *)sbrk((int32_t)total_size);

    if (block == (void *)0xFFFFFFFFU) {
        return 0;
    }

    block->size = size;
    block->free = 0U;
    block->prev = 0;
    block->next = 0;

    if (malloc_head == 0) {
        malloc_head = block;
        malloc_tail = block;
        return block;
    }

    block->prev = malloc_tail;
    malloc_tail->next = block;
    malloc_tail = block;
    return block;
}

static void try_release_tail(void)
{
    uint32_t header_size = malloc_header_size();

    while (malloc_tail != 0 && malloc_tail->free != 0U) {
        struct malloc_block *tail = malloc_tail;
        struct malloc_block *prev = tail->prev;
        uint32_t shrink_size = header_size + tail->size;

        if (shrink_size > 0x7FFFFFFFU) {
            break;
        }

        if (sbrk(-((int32_t)shrink_size)) == (void *)0xFFFFFFFFU) {
            break;
        }

        malloc_tail = prev;
        if (malloc_tail != 0) {
            malloc_tail->next = 0;
        } else {
            malloc_head = 0;
        }
    }
}

static void coalesce_with_next(struct malloc_block *block)
{
    uint32_t header_size = malloc_header_size();

    while (block != 0 && block->next != 0 && block->next->free != 0U) {
        struct malloc_block *next = block->next;
        block->size += header_size + next->size;
        block->next = next->next;
        if (block->next != 0) {
            block->next->prev = block;
        } else {
            malloc_tail = block;
        }
    }
}

void *malloc(size_t req_size)
{
    struct malloc_block *block;
    uint32_t size;
    uint32_t header_size = malloc_header_size();

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
    struct malloc_block *block;

    if (ptr == 0) {
        return;
    }

    block = find_block_from_payload(ptr);
    if (block == 0 || block->free != 0U) {
        return;
    }

    block->free = 1U;
    coalesce_with_next(block);

    if (block->prev != 0 && block->prev->free != 0U) {
        coalesce_with_next(block->prev);
        block = block->prev;
    }

    if (block->next == 0) {
        try_release_tail();
    }
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *ptr;

    if (count == 0U || size == 0U) {
        return malloc(1U);
    }

    if (count > ((size_t)-1) / size) {
        return 0;
    }

    total = count * size;
    ptr = malloc(total);
    if (ptr == 0) {
        return 0;
    }

    (void)memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t new_size)
{
    struct malloc_block *block;
    void *new_ptr;
    uint32_t header_size = malloc_header_size();
    uint32_t target_size;

    if (ptr == 0) {
        return malloc(new_size);
    }

    if (new_size == 0U) {
        free(ptr);
        return 0;
    }

    if (new_size > 0xFFFFFF00U) {
        return 0;
    }

    target_size = align_up_u32((uint32_t)new_size, MALLOC_ALIGN);
    block = find_block_from_payload(ptr);
    if (block == 0) {
        return 0;
    }

    if (block->size >= target_size) {
        split_block(block, target_size);
        return ptr;
    }

    if (block->next != 0 && block->next->free != 0U &&
        block->size + header_size + block->next->size >= target_size) {
        coalesce_with_next(block);
        split_block(block, target_size);
        block->free = 0U;
        return ptr;
    }

    new_ptr = malloc(new_size);
    if (new_ptr == 0) {
        return 0;
    }

    (void)memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}
