#pragma once

#include <stdbool.h>
#include <stdio.h>

#ifdef __linux__
#define TEST_SECTION __attribute__((__section__("tests")))
#endif

#ifdef _WIN32
#pragma section("tests$a", read, write)
#pragma section("tests$b", read, write)
#pragma section("tests$c", read, write)
__declspec(allocate("tests$a")) struct test_entry __start_tests = {0};
__declspec(allocate("tests$c")) struct test_entry __stop_tests = {0};
#define TEST_SECTION __declspec(allocate("tests$b"))
#endif

typedef bool (*test_test_fn)(void);
typedef void (*test_init_fn)(void);

enum test_entry_type
{
    TestEntry_TEST,
    TestEntry_SETUP,
    TestEntry_TEARDOWN
};

struct __attribute__((aligned(64))) test_entry
{
    int                 type;
    const char          *name;
    const char          *suite;
    union
    {
        test_test_fn    test;
        test_init_fn    init;
    };
};

#define TEST_F32_EPSILON 1e-5f

#define TEST(suitename, testname)                                               \
    static bool TEST_##suitename ##_ ##testname(void);                          \
    TEST_SECTION struct test_entry TEST_ENTRY_##suitename ##_ ##testname = {    \
        .type = TestEntry_TEST,                                                 \
        .name = #testname,                                                      \
        .suite = #suitename,                                                    \
        .test = TEST_##suitename ##_ ##testname                                 \
    };                                                                          \
    bool TEST_##suitename ##_ ##testname(void)

#define SETUP(suitename)                                                        \
    static void TEST_##suitename ##_ ##setup(void);                             \
    TEST_SECTION struct test_entry TEST_ENTRY_##suitename ##_ ##setup = {       \
        .type = TestEntry_SETUP,                                                \
        .suite = #suitename,                                                    \
        .init = TEST_##suitename ##_ ##setup                                    \
    };                                                                          \
    void TEST_##suitename ##_ ##setup(void)

#define TEARDOWN(suitename)                                                     \
    static void TEST_##suitename ##_ ##teardown(void);                          \
    TEST_SECTION struct test_entry TEST_ENTRY_##suitename ##_ ##teardown = {    \
        .type = TestEntry_TEARDOWN,                                             \
        .suite = #suitename,                                                    \
        .init = TEST_##suitename ##_ ##teardown                                 \
    };                                                                          \
    void TEST_##suitename ##_ ##teardown(void)

#define EXPECT_EQ(a, b)                         \
    if ((a) != (b)) {                           \
        printf("Expected %s == %s\n", #a, #b);  \
        return false;                           \
    }

#define EXPECT_NE(a, b)                         \
    if ((a) == (b)) {                           \
        printf("Expected %s != %s\n", #a, #b);  \
        return false;                           \
    }

#define EXPECT_EQ_F32(a, b)                                 \
    do {                                                    \
        if (fabsf((a) - (b)) > TEST_F32_EPSILON) {          \
            printf("Expected %.5f == %.5f\n", (a), (b));    \
            return false;                                   \
        }                                                   \
    } while(0)

#define EXPECT_NE_F32(a, b)                                 \
    do {                                                    \
        if (fabsf((a) - (b)) <= TEST_F32_EPSILON) {         \
            printf("Expected %.5f != %.5f\n", (a), (b));    \
            return false;                                   \
        }                                                   \
    } while(0)

#define EXPECT_NULL(x)                          \
    if ((x)) {                                  \
        printf("Expected %s != NULL\n", #x);    \
        return false;                           \
    }

#define EXPECT_NOT_NULL(x)                      \
    if (!(x)) {                                 \
        printf("Expected %s != NULL\n", #x);    \
        return false;                           \
    }
