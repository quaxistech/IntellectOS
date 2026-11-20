#ifndef INTELLECT_KHEAP64_H
#define INTELLECT_KHEAP64_H
#include <stddef.h>
#include <stdint.h>
/* M2: bounded 1-MiB supervisor-only RW/NX heap. Single-CPU / IRQs off. */
#define INTELLECT_KHEAP64_START_SLOT 32u
#define INTELLECT_KHEAP64_PAGE_COUNT 256u
#define INTELLECT_KHEAP64_MAX_LARGE_PAGES 64u
#define INTELLECT_KHEAP64_MAX_ALIGNMENT 4096u
typedef struct {
    uint32_t mapped_pages;
    uint32_t peak_mapped_pages;
    uint32_t live_allocations;
    uint64_t live_capacity_bytes; /* class/page-rounded, not requested bytes */
} intellect_kheap64_stats_t;
int intellect_kheap64_init(void);
/* Zero-initialized on success; size 1..256 KiB, align power-of-two 1..4096. */
void *intellect_kheap64_alloc(size_t size, size_t alignment);
/* Exact live allocation start only; scrubs on release. Returns 1 on success. */
int intellect_kheap64_free(void *ptr);
int intellect_kheap64_ready(void);
intellect_kheap64_stats_t intellect_kheap64_stats(void);
int intellect_kheap64_self_test(void);
#endif
