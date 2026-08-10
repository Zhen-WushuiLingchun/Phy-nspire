#include <stdbool.h>
#include <stdint.h>

#include "phy/permutation.h"
#include "phy/platform.h"
#include "phy_test.h"

static void test_permutation_primitives(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    static const uint16_t p[] = {1, 2, 0};
    static const uint16_t q[] = {1, 0, 2};
    uint16_t composed[3] = {0};
    uint16_t inverse[3] = {0};
    PHY_CHECK(phy_permutation_valid(p, 3u));
    PHY_CHECK(phy_permutation_valid(q, 3u));
    PHY_CHECK_EQ_INT(
        phy_permutation_compose(p, q, 3u, composed), PHY_OK);
    /* p(q(i)): 0 -> 2, 1 -> 1, 2 -> 0. */
    PHY_CHECK_EQ_INT(composed[0], 2);
    PHY_CHECK_EQ_INT(composed[1], 1);
    PHY_CHECK_EQ_INT(composed[2], 0);
    PHY_CHECK_EQ_INT(phy_permutation_inverse(p, 3u, inverse), PHY_OK);
    PHY_CHECK_EQ_INT(inverse[0], 2);
    PHY_CHECK_EQ_INT(inverse[1], 0);
    PHY_CHECK_EQ_INT(inverse[2], 1);
    static const uint16_t invalid[] = {0, 0, 2};
    PHY_CHECK(!phy_permutation_valid(invalid, 3u));
    phy_platform_shutdown();
}

static phy_perm_group *make_group(size_t degree)
{
    phy_perm_group *group = NULL;
    PHY_CHECK_EQ_INT(
        phy_perm_group_create(degree, NULL, &group), PHY_OK);
    PHY_CHECK(group != NULL);
    return group;
}

static void test_s3_bsgs_membership_and_orbit(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_perm_group *group = make_group(3u);
    static const uint16_t swap01[] = {1, 0, 2};
    static const uint16_t swap12[] = {0, 2, 1};
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(group, swap01, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(group, swap12, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_perm_group_build_bsgs(group), PHY_OK);
    PHY_CHECK(phy_perm_group_is_built(group));
    PHY_CHECK(phy_perm_group_strong_generator_count(group) >= 2u);
    PHY_CHECK_EQ_INT(phy_perm_group_order(group), 6);

    static const uint16_t cycle012[] = {1, 2, 0};
    bool member = false;
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(group, cycle012, 1, &member), PHY_OK);
    PHY_CHECK(member);
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(group, cycle012, -1, &member), PHY_OK);
    PHY_CHECK(!member);

    uint16_t orbit[3] = {0};
    size_t orbit_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_perm_group_orbit(
            group, 0u, orbit, 3u, &orbit_count), PHY_OK);
    PHY_CHECK_EQ_INT(orbit_count, 3);
    bool seen[3] = {false, false, false};
    for (size_t i = 0u; i < orbit_count; ++i) {
        PHY_CHECK(orbit[i] < 3u);
        seen[orbit[i]] = true;
    }
    PHY_CHECK(seen[0] && seen[1] && seen[2]);

    phy_perm_group_destroy(group);
    phy_platform_shutdown();
}

static void test_signed_group_and_negative_identity(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    static const uint16_t swap[] = {1, 0};
    phy_perm_group *antisymmetric = make_group(2u);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(antisymmetric, swap, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_build_bsgs(antisymmetric), PHY_OK);
    PHY_CHECK_EQ_INT(phy_perm_group_order(antisymmetric), 2);
    PHY_CHECK(!phy_perm_group_has_negative_identity(antisymmetric));
    bool member = false;
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(antisymmetric, swap, -1, &member), PHY_OK);
    PHY_CHECK(member);
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(antisymmetric, swap, 1, &member), PHY_OK);
    PHY_CHECK(!member);
    phy_perm_group_destroy(antisymmetric);

    phy_perm_group *zero_symmetry = make_group(2u);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(zero_symmetry, swap, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(zero_symmetry, swap, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_build_bsgs(zero_symmetry), PHY_OK);
    PHY_CHECK(phy_perm_group_has_negative_identity(zero_symmetry));
    PHY_CHECK_EQ_INT(phy_perm_group_order(zero_symmetry), 4);
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(zero_symmetry, swap, 1, &member), PHY_OK);
    PHY_CHECK(member);
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(zero_symmetry, swap, -1, &member), PHY_OK);
    PHY_CHECK(member);
    phy_perm_group_destroy(zero_symmetry);
    phy_platform_shutdown();
}

static void test_riemann_slot_group(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_perm_group *riemann = make_group(4u);
    static const uint16_t antisym_first[] = {1, 0, 2, 3};
    static const uint16_t antisym_second[] = {0, 1, 3, 2};
    static const uint16_t pair_exchange[] = {2, 3, 0, 1};
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(riemann, antisym_first, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(riemann, antisym_second, -1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(riemann, pair_exchange, 1), PHY_OK);
    PHY_CHECK_EQ_INT(phy_perm_group_build_bsgs(riemann), PHY_OK);
    PHY_CHECK_EQ_INT(phy_perm_group_order(riemann), 8);
    PHY_CHECK(!phy_perm_group_has_negative_identity(riemann));

    /* (ab)(cd) carries positive sign. */
    static const uint16_t both_pairs[] = {1, 0, 3, 2};
    bool member = false;
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(riemann, both_pairs, 1, &member), PHY_OK);
    PHY_CHECK(member);
    PHY_CHECK_EQ_INT(
        phy_perm_group_contains(riemann, both_pairs, -1, &member), PHY_OK);
    PHY_CHECK(!member);
    phy_perm_group_destroy(riemann);
    phy_platform_shutdown();
}

static void test_group_limits(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    phy_perm_limits limits = {0};
    limits.max_degree = 4u;
    limits.max_generators = 1u;
    limits.max_strong_generators = 8u;
    limits.max_steps = 10000u;
    phy_perm_group *group = NULL;
    PHY_CHECK_EQ_INT(
        phy_perm_group_create(5u, &limits, &group), PHY_ERR_TERM_LIMIT);
    PHY_CHECK(group == NULL);
    PHY_CHECK_EQ_INT(
        phy_perm_group_create(4u, &limits, &group), PHY_OK);
    static const uint16_t first[] = {1, 0, 2, 3};
    static const uint16_t second[] = {0, 2, 1, 3};
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(group, first, 1), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_perm_group_add_generator(group, second, 1),
        PHY_ERR_TERM_LIMIT);
    phy_perm_group_destroy(group);
    phy_platform_shutdown();
}

typedef struct {
    uint16_t image[4];
    int8_t sign;
} brute_perm;

static bool brute_equal(const brute_perm *left, const brute_perm *right)
{
    if (left->sign != right->sign) {
        return false;
    }
    for (size_t i = 0u; i < 4u; ++i) {
        if (left->image[i] != right->image[i]) {
            return false;
        }
    }
    return true;
}

static bool brute_has(const brute_perm *group, size_t count,
                      const brute_perm *candidate)
{
    for (size_t i = 0u; i < count; ++i) {
        if (brute_equal(&group[i], candidate)) {
            return true;
        }
    }
    return false;
}

static size_t brute_closure(const brute_perm *generators,
                            size_t generator_count, brute_perm *out)
{
    size_t count = 1u;
    out[0].sign = 1;
    for (size_t i = 0u; i < 4u; ++i) {
        out[0].image[i] = (uint16_t)i;
    }
    for (size_t cursor = 0u; cursor < count; ++cursor) {
        for (size_t generator = 0u; generator < generator_count;
             ++generator) {
            brute_perm candidate = {{0}, 0};
            PHY_CHECK_EQ_INT(
                phy_permutation_compose(
                    generators[generator].image, out[cursor].image, 4u,
                    candidate.image),
                PHY_OK);
            candidate.sign =
                (int8_t)(generators[generator].sign * out[cursor].sign);
            if (!brute_has(out, count, &candidate)) {
                PHY_CHECK(count < 48u);
                out[count++] = candidate;
            }
        }
    }
    return count;
}

static bool next_permutation4(uint16_t image[4])
{
    int pivot = 2;
    while (pivot >= 0 && image[pivot] >= image[pivot + 1]) {
        pivot--;
    }
    if (pivot < 0) {
        return false;
    }
    int successor = 3;
    while (image[successor] <= image[pivot]) {
        successor--;
    }
    const uint16_t temporary = image[pivot];
    image[pivot] = image[successor];
    image[successor] = temporary;
    for (int left = pivot + 1, right = 3; left < right;
         ++left, --right) {
        const uint16_t swapped = image[left];
        image[left] = image[right];
        image[right] = swapped;
    }
    return true;
}

static void check_against_brute(const brute_perm *generators,
                                size_t generator_count)
{
    brute_perm closure[48];
    const size_t expected_count =
        brute_closure(generators, generator_count, closure);
    phy_perm_group *group = make_group(4u);
    for (size_t i = 0u; i < generator_count; ++i) {
        PHY_CHECK_EQ_INT(
            phy_perm_group_add_generator(
                group, generators[i].image, generators[i].sign),
            PHY_OK);
    }
    PHY_CHECK_EQ_INT(phy_perm_group_build_bsgs(group), PHY_OK);
    PHY_CHECK_EQ_INT(phy_perm_group_order(group), expected_count);

    brute_perm candidate = {{0, 1, 2, 3}, 1};
    do {
        for (int sign = -1; sign <= 1; sign += 2) {
            candidate.sign = (int8_t)sign;
            bool actual = false;
            PHY_CHECK_EQ_INT(
                phy_perm_group_contains(
                    group, candidate.image, sign, &actual),
                PHY_OK);
            PHY_CHECK_EQ_INT(
                actual, brute_has(closure, expected_count, &candidate));
        }
    } while (next_permutation4(candidate.image));
    phy_perm_group_destroy(group);
}

static void test_bsgs_against_brute_force_groups(void)
{
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    static const brute_perm s4[] = {
        {{1, 0, 2, 3}, 1},
        {{0, 2, 1, 3}, 1},
        {{0, 1, 3, 2}, 1}};
    static const brute_perm a4[] = {
        {{1, 2, 0, 3}, 1},
        {{1, 0, 3, 2}, 1}};
    static const brute_perm d4[] = {
        {{1, 2, 3, 0}, 1},
        {{0, 3, 2, 1}, 1}};
    static const brute_perm signed_group[] = {
        {{1, 0, 2, 3}, -1},
        {{0, 2, 1, 3}, 1},
        {{0, 1, 3, 2}, -1}};
    check_against_brute(s4, sizeof s4 / sizeof s4[0]);
    check_against_brute(a4, sizeof a4 / sizeof a4[0]);
    check_against_brute(d4, sizeof d4 / sizeof d4[0]);
    check_against_brute(
        signed_group, sizeof signed_group / sizeof signed_group[0]);
    phy_platform_shutdown();
}

int main(void)
{
    PHY_TEST_CASE(test_permutation_primitives);
    PHY_TEST_CASE(test_s3_bsgs_membership_and_orbit);
    PHY_TEST_CASE(test_signed_group_and_negative_identity);
    PHY_TEST_CASE(test_riemann_slot_group);
    PHY_TEST_CASE(test_group_limits);
    PHY_TEST_CASE(test_bsgs_against_brute_force_groups);
    return PHY_TEST_REPORT("permutation");
}
