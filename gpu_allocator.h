#pragma once

// Original work by: Sebastian Aaltonen (https://github.com/sebbbi)
// https://github.com/sebbbi/OffsetAllocator

#include <stdbool.h>
#include <stdint.h>

#define GPU_ALLOCATOR_NUM_TOP_BINS          32
#define GPU_ALLOCATOR_BINS_PER_LEAF         8
#define GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT  3
#define GPU_ALLOCATOR_LEAF_BINS_INDEX_MASK  0x7
#define GPU_ALLOCATOR_NUM_LEAF_BINS         (GPU_ALLOCATOR_NUM_TOP_BINS*GPU_ALLOCATOR_BINS_PER_LEAF)
#define GPU_ALLOCATION_INIT                 (gpu_allocation_t){UINT32_MAX, UINT32_MAX}

typedef struct gpu_allocation_t
{
    uint32_t offset;
    uint32_t meta;
} gpu_allocation_t;

typedef struct gpu_allocator_report_t
{
    uint32_t free;
    uint32_t largest_region;
} gpu_allocator_report_t;

typedef struct gpu_allocator_region_t
{
    uint32_t size;
    uint32_t count;
} gpu_allocator_region_t;

typedef struct gpu_allocator_report2_t
{
    gpu_allocator_region_t regions[GPU_ALLOCATOR_NUM_LEAF_BINS];
} gpu_allocator_report2_t;

typedef struct gpu_allocator_node_t
{
    uint32_t    offset;
    uint32_t    size;
    uint32_t    prev, next;     // binListPrev/next
    uint32_t    nprev, nnext;   // neighborPrev/next
    bool        used;
} gpu_allocator_node_t;

typedef struct gpu_allocator_t
{
    uint32_t                size;
    uint32_t                max_allocs;
    uint32_t                free;

    uint32_t                bins_top;
    uint8_t                 bins[GPU_ALLOCATOR_NUM_TOP_BINS];
    uint32_t                bin_indices[GPU_ALLOCATOR_NUM_LEAF_BINS];

    gpu_allocator_node_t    *nodes;
    uint32_t                *free_nodes;
    uint32_t                free_offset;
} gpu_allocator_t;

void gpu_allocator_init(gpu_allocator_t *ga, uint32_t size, uint32_t max_allocs);
void gpu_allocator_destroy(gpu_allocator_t *ga);
void gpu_allocator_reset(gpu_allocator_t *ga);

// Allocation interface
gpu_allocation_t gpu_allocator_allocate(gpu_allocator_t *ga, uint32_t size);
void gpu_allocator_free(gpu_allocator_t *ga, gpu_allocation_t alloc);

// Statistics
gpu_allocator_report_t gpu_allocator_report(const gpu_allocator_t *ga);
gpu_allocator_report2_t gpu_allocator_report2(const gpu_allocator_t *ga);
