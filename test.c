#include "test.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern struct test_entry __start_tests;
extern struct test_entry __stop_tests;

struct test_suite
{
    const char          *name;
    uint32_t            max_tests;
    uint32_t            num_tests;
    size_t              longest_name;
    struct test_entry   **tests;
    struct test_entry   *setup;
    struct test_entry    *teardown;
};

static uint32_t max_suites;
static uint32_t num_suites;
static struct test_suite *suites;

struct test_suite *get_suite(const char *name)
{
    uint32_t i;
    struct test_suite *suite;

    for (i = 0; i < num_suites; i++) {
        suite = suites + i;
        if (strcmp(suite->name, name) == 0)
            return suite;
    }

    if (num_suites == max_suites) {
        max_suites = max_suites ? max_suites*2 : 16;
        suites = realloc(suites, sizeof *suites * max_suites);
    }
    suite = suites + num_suites++;
    memset(suite, 0, sizeof *suite);
    suite->name = name;
    return suite;
}

void add_test(struct test_suite *suite, struct test_entry *test)
{
    size_t name_len;

    if (suite->num_tests == suite->max_tests) {
        suite->max_tests = suite->max_tests ? suite->max_tests*2 : 16;
        suite->tests = realloc(suite->tests, sizeof *suite->tests * suite->max_tests);
    }
    suite->tests[suite->num_tests++] = test;
    name_len = strlen(test->name);
    if (name_len > suite->longest_name)
        suite->longest_name = name_len;
}

static void sort_tests(void)
{
    struct test_entry *it;
    struct test_suite *suite;

    it = &__start_tests;
    for (; it < &__stop_tests; it++) {
        suite = get_suite(it->suite);
        if (it->type == TestEntry_SETUP)
            suite->setup = it;
        else if (it->type == TestEntry_TEARDOWN)
            suite->teardown = it;
        else
            add_test(suite, it);
    }
}

int main(int argc, char *argv[])
{
    int rc;
    uint32_t i, j;
    struct test_suite *suite;
    struct test_entry *test;
    size_t name_len;
    bool val;

    rc = 0;
    sort_tests();
    for (i = 0; i < num_suites; i++)
    {
        suite = suites + i;
        printf("Running suite: %s\n", suite->name);
        for (j = 0; j < suite->num_tests; j++)
        {
            if (suite->setup)
                suite->setup->init();
            test = suite->tests[j];
            printf("\tRunning test: %s...", test->name);
            val = test->test();
            name_len = strlen(test->name);
            printf("%*s%s\x1b[0m\n", (int)(suite->longest_name - name_len + 3), "", val ? "\x1b[32m[OK]" : "\x1b[31m[FAIL]");
            if (!val)
                rc = 1;
            if (suite->teardown)
                suite->teardown->init();
        }
    }
    return rc;
}
