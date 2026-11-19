#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

extern int printf(const char *format, ...);

static unsigned int tests_run;
static unsigned int tests_failed;

static void check(int condition, const char *name) {
    tests_run++;
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) {
        tests_failed++;
    }
}

static int finish_test_suite(const char *name) {
    printf("%s: %u checks, %u failures\n", name, tests_run, tests_failed);
    return tests_failed == 0U ? 0 : 1;
}

#endif
