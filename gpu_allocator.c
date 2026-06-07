// Original work by: Sebastian Aaltonen (https://github.com/sebbbi)
// https://github.com/sebbbi/OffsetAllocator

#include "gpu_allocator.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#define MANTISSA_BITS   (3)
#define MANTISSA_VALUE  (1 << MANTISSA_BITS)
#define MANTISSA_MASK   (MANTISSA_VALUE - 1)

// NOTE: Trace logging, adjust this to the requirements of your project
#ifndef TLOG
#define TLOG(...) printf(__VA_ARGS__)
#endif

static inline uint32_t lzcnt_nonzero(uint32_t v)
{
#ifdef _MSC_VER
    unsigned long rc;
    _BitScanReverse(&rc, v);
    return 31 - rc;
#else
    return (uint32_t)(__builtin_clz(v));
#endif
}

static inline uint32_t tzcnt_nonzero(uint32_t v)
{
#ifdef _MSC_VER
    unsigned long rc;
    _BitScanForward(&rc, v);
    return rc;
#else
    return (uint32_t)(__builtin_ctz(v));
#endif
}

static inline void *xmalloc(size_t size)
{
    void *p;

    p = malloc(size);
    if (!p)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

static uint32_t uint_to_float_round_up(uint32_t size)
{
    uint32_t exp, mantissa, lz, hsb, msb, lsb_mask;

    exp = mantissa = 0;
    if (size < MANTISSA_VALUE)
    {
        mantissa = size;
    }
    else
    {
        lz = lzcnt_nonzero(size);
        hsb = 31 - lz;
        msb = hsb - MANTISSA_BITS;
        exp = msb + 1;
        mantissa = (size >> msb) & MANTISSA_MASK;
        lsb_mask = (1 << msb) - 1;
        if ((size & lsb_mask) != 0)
            mantissa++;
    }
    return (exp << MANTISSA_BITS) + mantissa;
}

static uint32_t uint_to_float_round_down(uint32_t size)
{
    uint32_t exp, mantissa, lz, hsb, msb;

    exp = mantissa = 0;
    if (size < MANTISSA_VALUE)
    {
        mantissa = size;
    }
    else
    {
        lz = lzcnt_nonzero(size);
        hsb = 31 - lz;
        msb = hsb - MANTISSA_BITS;
        exp = msb + 1;
        mantissa = (size >> msb) & MANTISSA_MASK;
    }
    return (exp << MANTISSA_BITS) | mantissa;
}

static uint32_t float_to_uint(uint32_t float_val)
{
    uint32_t exp, mantissa;

    exp = float_val >> MANTISSA_BITS;
    mantissa = float_val & MANTISSA_MASK;
    if (exp == 0)
        return mantissa;
    return (mantissa | MANTISSA_VALUE) << (exp - 1);
}

static uint32_t find_lsb_after(uint32_t mask, uint32_t start_index)
{
    uint32_t mask_before, mask_after, bits_after;

    mask_before = (1 << start_index) - 1;
    mask_after = ~mask_before;
    bits_after = mask & mask_after;
    if (bits_after == 0)
        return UINT32_MAX;
    return tzcnt_nonzero(bits_after);
}

static uint32_t insert_node(gpu_allocator_t *ga, uint32_t size, uint32_t offset)
{
    uint32_t bin_index, top_bin_index, leaf_bin_index, top_node_index, node_index;

    bin_index = uint_to_float_round_down(size);
    top_bin_index = bin_index >> GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
    leaf_bin_index = bin_index & GPU_ALLOCATOR_LEAF_BINS_INDEX_MASK;
    if (ga->bin_indices[bin_index] == UINT32_MAX)
    {
        ga->bins[top_bin_index] |= 1 << leaf_bin_index;
        ga->bins_top |= 1 << top_bin_index;
    }
    top_node_index = ga->bin_indices[bin_index];
    node_index = ga->free_nodes[ga->free_offset--];
    TLOG("gpu allocator: Getting node %u from freelist[%u]", node_index, ga->free_offset+1);
    ga->nodes[node_index] = (gpu_allocator_node_t)
    {
        .offset = offset,
        .size = size,
        .prev = UINT32_MAX,
        .next = top_node_index,
        .nprev = UINT32_MAX,
        .nnext = UINT32_MAX,
        .used = false
    };
    if (top_node_index != UINT32_MAX)
        ga->nodes[top_node_index].prev = node_index;
    ga->bin_indices[bin_index] = node_index;
    ga->free += size;
    TLOG("gpu allocator: Free storage: %u (+%u) (insert_node)", ga->free, size);
    return node_index;
}

static void remove_node(gpu_allocator_t *ga, uint32_t node_index)
{
    gpu_allocator_node_t *node;
    uint32_t bin_index, top_bin_index, leaf_bin_index;

    node = ga->nodes + node_index;
    if (node->prev != UINT32_MAX)
    {
        ga->nodes[node->prev].next = node->next;
        if (node->next != UINT32_MAX) ga->nodes[node->next].prev = node->prev;
    }
    else
    {
        bin_index = uint_to_float_round_down(node->size);
        top_bin_index = bin_index >> GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
        leaf_bin_index = bin_index & GPU_ALLOCATOR_LEAF_BINS_INDEX_MASK;
        ga->bin_indices[bin_index] = node->next;
        if (node->next != UINT32_MAX)
            ga->nodes[node->next].prev = UINT32_MAX;
        if (ga->bin_indices[bin_index] == UINT32_MAX)
        {
            ga->bins[top_bin_index] &= ~(1 << leaf_bin_index);
            if (ga->bins[top_bin_index] == 0)
                ga->bins_top &= (uint32_t)(~(1 << top_bin_index));
        }
    }
    TLOG("gpu allocator: Putting node %u into freelist[%u] (remove_node)", node_index, ga->free_offset + 1);
    ga->free_nodes[++ga->free_offset] = node_index;
    ga->free -= node->size;
    TLOG("gpu allocator: Free storage: %u (-%u) (remove_node)", ga->free, node->size);
}

void gpu_allocator_init(gpu_allocator_t *ga, uint32_t size, uint32_t max_allocs)
{
    ga->size = size;
    ga->max_allocs = max_allocs;
    ga->nodes = xmalloc(max_allocs * sizeof *ga->nodes);
    ga->free_nodes = xmalloc(max_allocs * sizeof *ga->free_nodes);
    gpu_allocator_reset(ga);
}

void gpu_allocator_destroy(gpu_allocator_t *ga)
{
    free(ga->nodes);
    free(ga->free_nodes);
}

void gpu_allocator_reset(gpu_allocator_t *ga)
{
    uint32_t i;
    gpu_allocator_node_t *node;

    assert(ga->size);
    assert(ga->max_allocs);
    assert(ga->nodes);
    assert(ga->free_nodes);
    ga->free = 0;
    ga->bins_top = 0;
    ga->free_offset = ga->max_allocs - 1;
    memset(ga->bins, 0x00, sizeof ga->bins);
    memset(ga->bin_indices, 0xff, sizeof ga->bin_indices);
    for (i = 0; i < ga->max_allocs; ++i)
    {
        node = ga->nodes + i;
        node->offset = node->size = 0;
        node->prev = node->next = node->nprev = node->nnext = UINT32_MAX;
        node->used = false;
        ga->free_nodes[i] = ga->max_allocs - i - 1;
    }
    insert_node(ga, ga->size, 0);
}

gpu_allocation_t gpu_allocator_allocate(gpu_allocator_t *ga, uint32_t size)
{
    uint32_t min_bin_index, min_top_bin_index, min_leaf_bin_index, top_bin_index, leaf_bin_index;
    uint32_t bin_index, node_index, node_size, rem, new_node_index;
    gpu_allocator_node_t *node;

    if (ga->free_offset == 0)
        return (gpu_allocation_t){UINT32_MAX, UINT32_MAX};
    min_bin_index = uint_to_float_round_up(size);
    min_top_bin_index = min_bin_index >> GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
    min_leaf_bin_index = min_bin_index & GPU_ALLOCATOR_LEAF_BINS_INDEX_MASK;
    top_bin_index = min_top_bin_index;
    leaf_bin_index = UINT32_MAX;
    if (ga->bins_top & (1 << top_bin_index))
        leaf_bin_index = find_lsb_after(ga->bins[top_bin_index], min_leaf_bin_index);
    if (leaf_bin_index == UINT32_MAX)
    {
        top_bin_index = find_lsb_after(ga->bins_top, min_top_bin_index + 1);
        if (top_bin_index == UINT32_MAX)
            return (gpu_allocation_t){UINT32_MAX, UINT32_MAX};

        leaf_bin_index = tzcnt_nonzero(ga->bins[top_bin_index]);
    }
    bin_index = (top_bin_index << GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leaf_bin_index;
    node_index = ga->bin_indices[bin_index];
    node = ga->nodes + node_index;
    node_size = node->size;
    node->size = size;
    node->used = true;
    ga->bin_indices[bin_index] = node->next;
    if (node->next != UINT32_MAX)
        ga->nodes[node->next].prev = UINT32_MAX;
    ga->free -= node_size;
    TLOG("gpu allocator: Free storage: %u (-%u) (allocate)", ga->free, node_size);
    if (ga->bin_indices[bin_index] == UINT32_MAX)
    {
        ga->bins[top_bin_index] &= ~(1 << leaf_bin_index);
        if (ga->bins[top_bin_index] == 0)
            ga->bins_top &= (uint32_t)(~(1 << top_bin_index));
    }
    rem = node_size - size;
    if (rem > 0)
    {
        new_node_index = insert_node(ga, rem, node->offset + size);
        if (node->nnext != UINT32_MAX)
            ga->nodes[node->nnext].nprev = new_node_index;
        ga->nodes[new_node_index].nprev = node_index;
        ga->nodes[new_node_index].nnext = node->nnext;
        node->nnext = new_node_index;
    }
    return (gpu_allocation_t){node->offset, node_index};
}

void gpu_allocator_free(gpu_allocator_t *ga, gpu_allocation_t alloc)
{
    uint32_t node_index, offset, size, nnext, nprev, combined_node_index;
    gpu_allocator_node_t *prev, *node, *next;

    assert(alloc.meta != UINT32_MAX);
    node_index = alloc.meta;
    node = ga->nodes + node_index;
    assert(node->used);
    offset = node->offset;
    size = node->size;
    if ((node->nprev != UINT32_MAX) && (ga->nodes[node->nprev].used == false))
    {
        prev = ga->nodes + node->nprev;
        offset = prev->offset;
        size += prev->size;
        remove_node(ga, node->nprev);
        assert(prev->nnext == node_index);
        node->nprev = prev->nprev;
    }
    if ((node->nnext != UINT32_MAX) && (ga->nodes[node->nnext].used == false))
    {
        next = ga->nodes + node->nnext;
        size += next->size;
        remove_node(ga, node->nnext);

        assert(next->nprev == node_index);
        node->nnext = next->nnext;
    }
    nnext = node->nnext;
    nprev = node->nprev;
    TLOG("gpu allocator: Putting node %u into freelist[%u] (free)", node_index, ga->free_offset + 1);
    ga->free_nodes[++ga->free_offset] = node_index;
    combined_node_index = insert_node(ga, size, offset);
    if (nnext != UINT32_MAX)
    {
        ga->nodes[combined_node_index].nnext = nnext;
        ga->nodes[nnext].nprev = combined_node_index;
    }
    if (nprev != UINT32_MAX)
    {
        ga->nodes[combined_node_index].nprev = nprev;
        ga->nodes[nprev].nnext = combined_node_index;
    }
}

gpu_allocator_report_t gpu_allocator_report(const gpu_allocator_t *ga)
{
    uint32_t largest_region, free, top_bin_index, leaf_bin_index;

    largest_region = free = 0;
    if (ga->free_offset > 0)
    {
        free = ga->free;
        if (ga->bins_top)
        {
            top_bin_index = 31 - lzcnt_nonzero(ga->bins_top);
            leaf_bin_index = 31 - lzcnt_nonzero(ga->bins[top_bin_index]);
            largest_region = float_to_uint((top_bin_index << GPU_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leaf_bin_index);
            assert(free >= largest_region);
        }
    }

    return (gpu_allocator_report_t){free, largest_region};
}

gpu_allocator_report2_t gpu_allocator_report2(const gpu_allocator_t *ga)
{
    gpu_allocator_report2_t report;
    uint32_t i, count, node_index;

    memset(&report, 0, sizeof report);
    for (i = 0; i < GPU_ALLOCATOR_NUM_LEAF_BINS; ++i)
    {
        count = 0;
        node_index = ga->bin_indices[i];
        while (node_index != UINT32_MAX)
        {
            node_index = ga->nodes[node_index].next;
            count++;
        }
        report.regions[i] = (gpu_allocator_region_t){float_to_uint(i), count};
    }
    return report;
}
