/*
 * Tiny single-threaded test harness for Pt.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_test_harness_h
#define pt_test_harness_h

#include <math.h>
#include <stdio.h>

static int pt_test_checks;
static int pt_test_failures;

#define pt_check(condition)                                                          \
    do {                                                                             \
        ++pt_test_checks;                                                            \
        if (!(condition)) {                                                          \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
                    #condition);                                                     \
            ++pt_test_failures;                                                      \
        }                                                                            \
    } while (0)

#define pt_check_near(actual, expected, epsilon)                                     \
    do {                                                                             \
        ++pt_test_checks;                                                            \
        double pt_actual_ = (double)(actual);                                        \
        double pt_expected_ = (double)(expected);                                    \
        if (fabs(pt_actual_ - pt_expected_) > (double)(epsilon)) {                   \
            fprintf(stderr, "%s:%d: expected %f, got %f (epsilon %f)\n", __FILE__,   \
                    __LINE__, pt_expected_, pt_actual_, (double)(epsilon));           \
            ++pt_test_failures;                                                      \
        }                                                                            \
    } while (0)

static inline int pt_test_finish(const char *name) {
    if (pt_test_failures == 0) {
        printf("ok %s (%d checks)\n", name, pt_test_checks);
        return 0;
    }
    printf("not ok %s (%d of %d checks failed)\n", name, pt_test_failures, pt_test_checks);
    return 1;
}

#endif
