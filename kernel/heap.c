#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"

#include <stdint.h>

#define KHEAP_START         0xC1000000U
#define KHEAP_END           0xC2000000U
#define KHEAP_ALIGNMENT     8U
#define KHEAP_MIN_SPLIT     16U

struct heap_block {
    uint32_t size;
    uint32_t used;
    struct heap_block *prev;
    struct heap_block *next;
};

static struct heap_block *kheap_head;
static struct heap_block *kheap_tail;
static uint32_t kheap_top;
static uint32_t kheap_ready;

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint32_t block_end_addr(const struct heap_block *block)
{
    return (uint32_t)(uintptr_t)block + (uint32_t)sizeof(struct heap_block) + block->size;
}

static struct heap_block *find_fit(uint32_t size)
{
    struct heap_block *block = kheap_head;

    while (block != 0) {
        if (block->used == 0U && block->size >= size) {
            return block;
        }
        block = block->next;
    }

    return 0;
}

static struct heap_block *find_block_by_payload(void *ptr)
{
    struct heap_block *block = kheap_head;
    uint32_t payload_addr = (uint32_t)(uintptr_t)ptr;

    while (block != 0) {
        uint32_t block_payload = (uint32_t)(uintptr_t)block + (uint32_t)sizeof(struct heap_block);
        if (block_payload == payload_addr) {
            return block;
        }
        block = block->next;
    }

    return 0;
}

static void split_block(struct heap_block *block, uint32_t size)
{
    uint32_t header_size = (uint32_t)sizeof(struct heap_block);

    if (block->size <= size) {
        return;
    }

    if (block->size - size < header_size + KHEAP_MIN_SPLIT) {
        return;
    }

    uint32_t new_addr = (uint32_t)(uintptr_t)block + header_size + size;
    struct heap_block *new_block = (struct heap_block *)(uintptr_t)new_addr;

    new_block->size = block->size - size - header_size;
    new_block->used = 0U;
    new_block->prev = block;
    new_block->next = block->next;

    if (block->next != 0) {
        block->next->prev = new_block;
    } else {
        kheap_tail = new_block;
    }

    block->size = size;
    block->next = new_block;
}

static void merge_forward(struct heap_block *block)
{
    uint32_t header_size = (uint32_t)sizeof(struct heap_block);

    while (block->next != 0 && block->next->used == 0U) {
        struct heap_block *next = block->next;

        block->size += header_size + next->size;
        block->next = next->next;

        if (next->next != 0) {
            next->next->prev = block;
        } else {
            kheap_tail = block;
        }
    }
}

static void add_region(uint32_t region_start, uint32_t region_size)
{
    uint32_t header_size = (uint32_t)sizeof(struct heap_block);

    if (region_size <= header_size) {
        return;
    }

    if (kheap_tail != 0 && kheap_tail->used == 0U && block_end_addr(kheap_tail) == region_start) {
        kheap_tail->size += region_size;
        return;
    }

    struct heap_block *block = (struct heap_block *)(uintptr_t)region_start;
    block->size = region_size - header_size;
    block->used = 0U;
    block->prev = kheap_tail;
    block->next = 0;

    if (kheap_tail != 0) {
        kheap_tail->next = block;
    } else {
        kheap_head = block;
    }

    kheap_tail = block;
}

static int expand_heap(uint32_t min_bytes)
{
    if (kheap_top >= KHEAP_END) {
        return -1;
    }

    uint32_t bytes_to_map = align_up_u32(min_bytes, PAGE_SIZE);
    if (bytes_to_map < PAGE_SIZE) {
        bytes_to_map = PAGE_SIZE;
    }

    if (bytes_to_map > (KHEAP_END - kheap_top)) {
        bytes_to_map = KHEAP_END - kheap_top;
    }

    uint32_t old_top = kheap_top;
    uint32_t mapped_bytes = 0U;

    while (mapped_bytes < bytes_to_map) {
        uint32_t phys = pmm_alloc_frame();
        if (phys == 0U) {
            break;
        }

        if (paging_map_page(kheap_top, phys, PAGE_WRITABLE) != 0) {
            pmm_free_frame(phys);
            break;
        }

        kheap_top += PAGE_SIZE;
        mapped_bytes += PAGE_SIZE;
    }

    if (mapped_bytes == 0U) {
        return -1;
    }

    add_region(old_top, mapped_bytes);
    return (mapped_bytes == bytes_to_map) ? 0 : -1;
}

void kheap_init(void)
{
    kheap_head = 0;
    kheap_tail = 0;
    kheap_top = KHEAP_START;
    kheap_ready = 0U;

    (void)expand_heap(PAGE_SIZE);
    if (kheap_head == 0) {
        serial_puts("KHEAP: init failed\n");
        return;
    }

    kheap_ready = 1U;
    serial_puts("KHEAP: initialized\n");
}

void *kmalloc(size_t size)
{
    if (kheap_ready == 0U || size == 0U) {
        return 0;
    }

    if (size > (size_t)(KHEAP_END - KHEAP_START)) {
        return 0;
    }

    uint32_t request = (uint32_t)size;
    if (request > 0xFFFFFFFFU - (KHEAP_ALIGNMENT - 1U)) {
        return 0;
    }
    request = align_up_u32(request, KHEAP_ALIGNMENT);

    struct heap_block *block = find_fit(request);

    while (block == 0) {
        uint32_t top_before = kheap_top;
        uint32_t required = request + (uint32_t)sizeof(struct heap_block);

        (void)expand_heap(required);
        if (kheap_top == top_before) {
            return 0;
        }

        block = find_fit(request);
    }

    split_block(block, request);
    block->used = 1U;

    return (void *)(uintptr_t)((uint32_t)(uintptr_t)block + (uint32_t)sizeof(struct heap_block));
}

void kfree(void *ptr)
{
    if (kheap_ready == 0U || ptr == 0) {
        return;
    }

    struct heap_block *block = find_block_by_payload(ptr);
    if (block == 0 || block->used == 0U) {
        return;
    }

    block->used = 0U;
    merge_forward(block);

    if (block->prev != 0 && block->prev->used == 0U) {
        merge_forward(block->prev);
    }
}
