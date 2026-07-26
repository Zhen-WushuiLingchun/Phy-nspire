/*
 * Minimal test harness.
 *
 * Phase 0 deliberately has no test-framework dependency: the host build must
 * stay buildable with nothing but a C compiler, so that a contributor can
 * verify the baseline before installing anything.
 */
#ifndef PHY_TEST_H
#define PHY_TEST_H

#include <stdio.h>
#include <string.h>

static int g_phy_test_failures;
static int g_phy_test_checks;
static const char *g_phy_test_current;

#define PHY_TEST_CASE(name)                                                    \
    do {                                                                       \
        g_phy_test_current = #name;                                            \
        name();                                                                \
    } while (0)

#define PHY_CHECK(condition)                                                   \
    do {                                                                       \
        g_phy_test_checks++;                                                   \
        if (!(condition)) {                                                    \
            g_phy_test_failures++;                                             \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__,        \
                    g_phy_test_current, #condition);                           \
        }                                                                      \
    } while (0)

#define PHY_CHECK_EQ_INT(actual, expected)                                     \
    do {                                                                       \
        g_phy_test_checks++;                                                   \
        const long long phy_actual_ = (long long)(actual);                     \
        const long long phy_expected_ = (long long)(expected);                 \
        if (phy_actual_ != phy_expected_) {                                    \
            g_phy_test_failures++;                                             \
            fprintf(stderr,                                                    \
                    "FAIL %s:%d [%s] %s: expected %lld, got %lld\n", __FILE__, \
                    __LINE__, g_phy_test_current, #actual, phy_expected_,      \
                    phy_actual_);                                              \
        }                                                                      \
    } while (0)

#define PHY_CHECK_EQ_STR(actual, expected)                                     \
    do {                                                                       \
        g_phy_test_checks++;                                                   \
        const char *phy_actual_ = (actual);                                    \
        const char *phy_expected_ = (expected);                                \
        if (phy_actual_ == NULL || strcmp(phy_actual_, phy_expected_) != 0) {   \
            g_phy_test_failures++;                                             \
            fprintf(stderr, "FAIL %s:%d [%s] %s: expected \"%s\", got \"%s\"\n",\
                    __FILE__, __LINE__, g_phy_test_current, #actual,           \
                    phy_expected_, phy_actual_ ? phy_actual_ : "(null)");      \
        }                                                                      \
    } while (0)

#define PHY_TEST_REPORT(suite)                                                 \
    (printf("%s: %d checks, %d failures\n", (suite), g_phy_test_checks,        \
            g_phy_test_failures),                                              \
     g_phy_test_failures == 0 ? 0 : 1)

#endif /* PHY_TEST_H */
