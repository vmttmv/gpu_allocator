// Original work by: Sebastian Aaltonen (https://github.com/sebbbi)
// https://github.com/sebbbi/OffsetAllocator

// Ignore allocator output
#define TLOG(...)

#include "gpu_allocator.c"
#include "test.h"

#define LEN(x) (sizeof((x)) / sizeof(*(x)))

struct test_number
{
    uint32_t number;
    uint32_t up;
    uint32_t down;
};

TEST(numbers, uint_to_float)
{
    const struct test_number test_data[] =
    {
        {17,        17,     16},
        {118,       39,     38},
        {1024,      64,     64},
        {65536,     112,    112},
        {529445,    137,    136},
        {1048575,   144,    143}
    };

    uint32_t count, i, round_up, round_down;
    const struct test_number *v;

    count = 17;
    for (i = 0; i < count; ++i) {
        round_up = uint_to_float_round_up(i);
        round_down = uint_to_float_round_down(i);
        EXPECT_EQ(i, round_up);
        EXPECT_EQ(i, round_down);
    }

    for (i = 0; i < LEN(test_data); ++i) {
        v = test_data + i;
        round_up = uint_to_float_round_up(v->number);
        round_down = uint_to_float_round_down(v->number);
        EXPECT_EQ(round_up, v->up);
        EXPECT_EQ(round_down, v->down);
    }
    return true;
}

TEST(numbers, float_to_uint)
{
    uint32_t count, i, v, round_up, round_down;

    count = 17;
    for (i = 0; i < count; ++i) {
        v = float_to_uint(i);
        EXPECT_EQ(i, v);
    }
    for (i = 0; i < 240; ++i) {
        v = float_to_uint(i);
        round_up = uint_to_float_round_up(v);
        round_down = uint_to_float_round_down(v);
        EXPECT_EQ(i, round_up);
        EXPECT_EQ(i, round_down);
    }
    return true;
}

TEST(allocate, basic)
{
    gpu_allocator_t allocator;
    gpu_allocation_t a;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);
    a = gpu_allocator_allocate(&allocator, 1337);
    EXPECT_EQ(a.offset, 0);

    gpu_allocator_destroy(&allocator);
    return true;
}

TEST(allocate, simple)
{
    gpu_allocator_t allocator;
    gpu_allocation_t a, b, c, d, validate;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);

    a = gpu_allocator_allocate(&allocator, 0);
    EXPECT_EQ(a.offset, 0);

    b = gpu_allocator_allocate(&allocator, 1);
    EXPECT_EQ(b.offset, 0);

    c = gpu_allocator_allocate(&allocator, 123);
    EXPECT_EQ(c.offset, 1);

    d = gpu_allocator_allocate(&allocator, 1234);
    EXPECT_EQ(d.offset, 124);

    gpu_allocator_free(&allocator, a);
    gpu_allocator_free(&allocator, b);
    gpu_allocator_free(&allocator, c);
    gpu_allocator_free(&allocator, d);

    validate = gpu_allocator_allocate(&allocator, 1024 * 1024 * 256);
    EXPECT_EQ(validate.offset, 0);

    return true;
}

TEST(allocate, merge_trivial)
{
    gpu_allocator_t allocator;
    gpu_allocation_t a, b, validate;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);

    a = gpu_allocator_allocate(&allocator, 1337);
    EXPECT_EQ(a.offset, 0);
    gpu_allocator_free(&allocator, a);

    b = gpu_allocator_allocate(&allocator, 1337);
    EXPECT_EQ(b.offset, 0);
    gpu_allocator_free(&allocator, b);

    validate = gpu_allocator_allocate(&allocator, 1024 * 1024 * 256);
    EXPECT_EQ(validate.offset, 0);

    return true;
}

TEST(allocate, reuse_trivial)
{
    gpu_allocator_t allocator;
    gpu_allocation_t a, b, c, validate;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);

    a = gpu_allocator_allocate(&allocator, 1024);
    EXPECT_EQ(a.offset, 0);

    b = gpu_allocator_allocate(&allocator, 3456);
    EXPECT_EQ(b.offset, 1024);

    gpu_allocator_free(&allocator, a);

    c = gpu_allocator_allocate(&allocator, 1024);
    EXPECT_EQ(c.offset, 0);

    gpu_allocator_free(&allocator, c);
    gpu_allocator_free(&allocator, b);

    validate = gpu_allocator_allocate(&allocator, 1024 * 1024 * 256);
    EXPECT_EQ(validate.offset, 0);

    return true;
}

TEST(allocate, reuse_complex)
{
    gpu_allocator_t allocator;
    gpu_allocation_t a, b, c, d, e, validate;
    gpu_allocator_report_t report;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);

    a = gpu_allocator_allocate(&allocator, 1024);
    EXPECT_EQ(a.offset, 0);

    b = gpu_allocator_allocate(&allocator, 3456);
    EXPECT_EQ(b.offset, 1024);

    gpu_allocator_free(&allocator, a);

    c = gpu_allocator_allocate(&allocator, 2345);
    EXPECT_EQ(c.offset, 1024 + 3456);

    d = gpu_allocator_allocate(&allocator, 456);
    EXPECT_EQ(d.offset, 0);

    e = gpu_allocator_allocate(&allocator, 512);
    EXPECT_EQ(e.offset, 456);

    report = gpu_allocator_report(&allocator);
    EXPECT_EQ(report.free, 1024 * 1024 * 256 - 3456 - 2345 - 456 - 512);
    EXPECT_NE(report.largest_region, report.free);

    gpu_allocator_free(&allocator, c);
    gpu_allocator_free(&allocator, d);
    gpu_allocator_free(&allocator, b);
    gpu_allocator_free(&allocator, e);

    validate = gpu_allocator_allocate(&allocator, 1024 * 1024 * 256);
    EXPECT_EQ(validate.offset, 0);

    return true;
}

TEST(allocate, zero_fragmentation)
{
    gpu_allocator_t allocator;
    gpu_allocation_t allocations[256], validate;
    uint32_t i;
    gpu_allocator_report_t report, report2;

    gpu_allocator_init(&allocator, 1024 * 1024 * 256, 128 * 1024);
    for (i = 0; i < 256; ++i)
    {
        allocations[i] = gpu_allocator_allocate(&allocator, 1024 * 1024);
        EXPECT_EQ(allocations[i].offset, i * 1024 * 1024);
    }
    report = gpu_allocator_report(&allocator);
    EXPECT_EQ(report.free, 0);
    EXPECT_EQ(report.largest_region, 0);

    gpu_allocator_free(&allocator, allocations[243]);
    gpu_allocator_free(&allocator, allocations[5]);
    gpu_allocator_free(&allocator, allocations[123]);
    gpu_allocator_free(&allocator, allocations[95]);

    gpu_allocator_free(&allocator, allocations[151]);
    gpu_allocator_free(&allocator, allocations[152]);
    gpu_allocator_free(&allocator, allocations[153]);
    gpu_allocator_free(&allocator, allocations[154]);

    allocations[243] = gpu_allocator_allocate(&allocator, 1024 * 1024);
    allocations[5] = gpu_allocator_allocate(&allocator, 1024 * 1024);
    allocations[123] = gpu_allocator_allocate(&allocator, 1024 * 1024);
    allocations[95] = gpu_allocator_allocate(&allocator, 1024 * 1024);
    allocations[151] = gpu_allocator_allocate(&allocator, 1024 * 1024 * 4);
    EXPECT_NE(allocations[243].offset, UINT32_MAX);
    EXPECT_NE(allocations[5].offset, UINT32_MAX);
    EXPECT_NE(allocations[123].offset, UINT32_MAX);
    EXPECT_NE(allocations[95].offset, UINT32_MAX);
    EXPECT_NE(allocations[151].offset, UINT32_MAX);

    for (i = 0; i < 256; ++i)
    {
        if (i < 152 || i > 154)
            gpu_allocator_free(&allocator, allocations[i]);
    }

    report2 = gpu_allocator_report(&allocator);
    EXPECT_EQ(report2.free, 1024 * 1024 * 256);
    EXPECT_EQ(report2.largest_region, 1024 * 1024 * 256);

    validate = gpu_allocator_allocate(&allocator, 1024 * 1024 * 256);
    EXPECT_EQ(validate.offset, 0);

    return true;
}
