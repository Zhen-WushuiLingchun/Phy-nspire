/*
 * Phy-nspire — dynamic exact vector and matrix tests.
 *
 * A matrix owns only context-local IR handles.  Arithmetic is delegated to the
 * exact scalar CAS, so every result below is checked symbolically rather than
 * converted to floating point.
 */
#include "phy/cas.h"
#include "phy/ir.h"
#include "phy/linear.h"
#include "phy/platform.h"
#include "phy/platform_host.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
} fixture;

static fixture fixture_open(void)
{
    fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static phy_ir_ref number(fixture *f, int64_t numerator, int64_t denominator)
{
    phy_ir_ref out = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(f->cas, numerator, denominator, &out), PHY_OK);
    return out;
}

static void expect_entry(fixture *f, const phy_matrix *matrix, size_t row,
                         size_t column, int64_t numerator,
                         int64_t denominator)
{
    phy_ir_ref actual = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_get(matrix, row, column, &actual), PHY_OK);
    phy_cas_decision equal = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f->cas, actual, number(f, numerator, denominator), &equal),
        PHY_OK);
    PHY_CHECK_EQ_INT(equal, PHY_CAS_ZERO);
}

static phy_matrix *make_matrix(fixture *f, size_t rows, size_t columns,
                               const int64_t *values)
{
    const size_t count = rows * columns;
    phy_ir_ref entries[64];
    PHY_CHECK(count <= sizeof entries / sizeof entries[0]);
    for (size_t i = 0u; i < count; ++i) {
        entries[i] = number(f, values[i], 1);
    }
    phy_matrix *matrix = NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_create(f->cas, rows, columns, entries, NULL, &matrix),
        PHY_OK);
    PHY_CHECK(matrix != NULL);
    return matrix;
}

static void test_shape_and_exact_entries(void)
{
    fixture f = fixture_open();

    phy_matrix *invalid = NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_create(f.cas, 0u, 3u, NULL, NULL, &invalid),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK(invalid == NULL);

    phy_ir_ref entries[42];
    for (size_t i = 0u; i < 42u; ++i) {
        entries[i] = number(&f, (int64_t)i + 1, 3);
    }
    phy_matrix *matrix = NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_create(f.cas, 6u, 7u, entries, NULL, &matrix), PHY_OK);
    PHY_CHECK_EQ_INT(phy_matrix_rows(matrix), 6);
    PHY_CHECK_EQ_INT(phy_matrix_columns(matrix), 7);
    expect_entry(&f, matrix, 5u, 6u, 14, 1);

    phy_ir_ref ignored = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_get(matrix, 6u, 0u, &ignored), PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(
        phy_matrix_get(matrix, 0u, 7u, &ignored), PHY_ERR_INVALID_ARGUMENT);

    phy_matrix_destroy(matrix);
    fixture_close(&f);
}

static void test_add_transpose_and_multiply(void)
{
    fixture f = fixture_open();
    static const int64_t a_values[] = {1, 2, 3, 4, 5, 6};
    static const int64_t b_values[] = {6, 5, 4, 3, 2, 1};
    phy_matrix *a = make_matrix(&f, 2u, 3u, a_values);
    phy_matrix *b = make_matrix(&f, 2u, 3u, b_values);

    phy_matrix *sum = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_add(a, b, &sum), PHY_OK);
    for (size_t row = 0u; row < 2u; ++row) {
        for (size_t column = 0u; column < 3u; ++column) {
            expect_entry(&f, sum, row, column, 7, 1);
        }
    }

    phy_matrix *transposed = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_transpose(a, &transposed), PHY_OK);
    PHY_CHECK_EQ_INT(phy_matrix_rows(transposed), 3);
    PHY_CHECK_EQ_INT(phy_matrix_columns(transposed), 2);
    expect_entry(&f, transposed, 2u, 1u, 6, 1);

    static const int64_t c_values[] = {7, 8, 9, 10, 11, 12};
    phy_matrix *c = make_matrix(&f, 3u, 2u, c_values);
    phy_matrix *product = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_multiply(a, c, &product), PHY_OK);
    expect_entry(&f, product, 0u, 0u, 58, 1);
    expect_entry(&f, product, 0u, 1u, 64, 1);
    expect_entry(&f, product, 1u, 0u, 139, 1);
    expect_entry(&f, product, 1u, 1u, 154, 1);

    phy_matrix *wrong = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_multiply(a, b, &wrong), PHY_ERR_TYPE);
    PHY_CHECK(wrong == NULL);

    phy_matrix_destroy(product);
    phy_matrix_destroy(c);
    phy_matrix_destroy(transposed);
    phy_matrix_destroy(sum);
    phy_matrix_destroy(b);
    phy_matrix_destroy(a);
    fixture_close(&f);
}

static void test_determinant_inverse_rref_and_rank(void)
{
    fixture f = fixture_open();
    static const int64_t values[] = {2, 1, 1, 3};
    phy_matrix *matrix = make_matrix(&f, 2u, 2u, values);

    phy_ir_ref determinant = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_determinant(matrix, &determinant), PHY_OK);
    phy_cas_decision equal = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f.cas, determinant, number(&f, 5, 1), &equal), PHY_OK);
    PHY_CHECK_EQ_INT(equal, PHY_CAS_ZERO);

    phy_matrix *inverse = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_inverse(matrix, &inverse), PHY_OK);
    expect_entry(&f, inverse, 0u, 0u, 3, 5);
    expect_entry(&f, inverse, 0u, 1u, -1, 5);
    expect_entry(&f, inverse, 1u, 0u, -1, 5);
    expect_entry(&f, inverse, 1u, 1u, 2, 5);

    static const int64_t rectangular_values[] = {
        1, 2, 3, 4,
        2, 4, 6, 8,
        0, 1, 1, 1};
    phy_matrix *rectangular =
        make_matrix(&f, 3u, 4u, rectangular_values);
    phy_matrix *reduced = NULL;
    size_t rank = 0u;
    PHY_CHECK_EQ_INT(
        phy_matrix_rref(rectangular, &reduced, &rank), PHY_OK);
    PHY_CHECK_EQ_INT(rank, 2);
    expect_entry(&f, reduced, 0u, 0u, 1, 1);
    expect_entry(&f, reduced, 0u, 1u, 0, 1);
    expect_entry(&f, reduced, 0u, 2u, 1, 1);
    expect_entry(&f, reduced, 0u, 3u, 2, 1);
    expect_entry(&f, reduced, 1u, 0u, 0, 1);
    expect_entry(&f, reduced, 1u, 1u, 1, 1);
    expect_entry(&f, reduced, 1u, 2u, 1, 1);
    expect_entry(&f, reduced, 1u, 3u, 1, 1);

    size_t rank_only = 0u;
    PHY_CHECK_EQ_INT(phy_matrix_rank(rectangular, &rank_only), PHY_OK);
    PHY_CHECK_EQ_INT(rank_only, rank);

    phy_matrix_destroy(reduced);
    phy_matrix_destroy(rectangular);
    phy_matrix_destroy(inverse);
    phy_matrix_destroy(matrix);
    fixture_close(&f);
}

static void test_exact_linear_solve_and_limits(void)
{
    fixture f = fixture_open();
    static const int64_t a_values[] = {2, 1, 1, -1};
    static const int64_t b_values[] = {5, 1};
    phy_matrix *a = make_matrix(&f, 2u, 2u, a_values);
    phy_matrix *b = make_matrix(&f, 2u, 1u, b_values);

    phy_matrix *solution = NULL;
    PHY_CHECK_EQ_INT(phy_matrix_solve(a, b, &solution), PHY_OK);
    expect_entry(&f, solution, 0u, 0u, 2, 1);
    expect_entry(&f, solution, 1u, 0u, 1, 1);

    phy_linear_limits tiny = {0};
    tiny.max_entries = 4u;
    phy_ir_ref entries[6];
    for (size_t i = 0u; i < 6u; ++i) {
        entries[i] = number(&f, 0, 1);
    }
    phy_matrix *too_large = NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_create(f.cas, 2u, 3u, entries, &tiny, &too_large),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK(too_large == NULL);

    phy_matrix_destroy(solution);
    phy_matrix_destroy(b);
    phy_matrix_destroy(a);
    fixture_close(&f);
}

static void test_vectors_and_null_space(void)
{
    fixture f = fixture_open();
    const phy_ir_ref left_entries[3] = {
        number(&f, 1, 1), number(&f, 2, 1), number(&f, 3, 1)};
    const phy_ir_ref right_entries[3] = {
        number(&f, 4, 1), number(&f, 5, 1), number(&f, 6, 1)};
    phy_vector *left = NULL;
    phy_vector *right = NULL;
    PHY_CHECK_EQ_INT(
        phy_vector_create(f.cas, 3u, left_entries, NULL, &left), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_vector_create(f.cas, 3u, right_entries, NULL, &right), PHY_OK);
    PHY_CHECK_EQ_INT(phy_vector_length(left), 3);
    phy_ir_ref dot = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(phy_vector_dot(left, right, &dot), PHY_OK);
    phy_cas_decision equal = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(f.cas, dot, number(&f, 32, 1), &equal), PHY_OK);
    PHY_CHECK_EQ_INT(equal, PHY_CAS_ZERO);

    static const int64_t values[] = {1, 2, 3, 2, 4, 6};
    phy_matrix *matrix = make_matrix(&f, 2u, 3u, values);
    phy_matrix *basis = NULL;
    size_t nullity = 0u;
    PHY_CHECK_EQ_INT(
        phy_matrix_null_space(matrix, &basis, &nullity), PHY_OK);
    PHY_CHECK_EQ_INT(nullity, 2);
    PHY_CHECK_EQ_INT(phy_matrix_rows(basis), 3);
    PHY_CHECK_EQ_INT(phy_matrix_columns(basis), 2);
    expect_entry(&f, basis, 0u, 0u, -2, 1);
    expect_entry(&f, basis, 1u, 0u, 1, 1);
    expect_entry(&f, basis, 2u, 0u, 0, 1);
    expect_entry(&f, basis, 0u, 1u, -3, 1);
    expect_entry(&f, basis, 1u, 1u, 0, 1);
    expect_entry(&f, basis, 2u, 1u, 1, 1);

    phy_matrix_destroy(basis);
    phy_matrix_destroy(matrix);
    phy_matrix_destroy(right);
    phy_matrix_destroy(left);
    fixture_close(&f);
}

static void test_allocation_failure_unwinds(void)
{
    fixture f = fixture_open();
    const phy_ir_ref entries[4] = {
        number(&f, 1, 1), number(&f, 2, 1),
        number(&f, 3, 1), number(&f, 4, 1)};
    phy_telemetry before;
    phy_telemetry after;
    phy_telemetry_get(&before);

    for (unsigned fail_at = 1u; fail_at <= 2u; ++fail_at) {
        phy_host_fail_alloc_after(fail_at);
        phy_matrix *matrix = NULL;
        PHY_CHECK_EQ_INT(
            phy_matrix_create(f.cas, 2u, 2u, entries, NULL, &matrix),
            PHY_ERR_MEMORY_LIMIT);
        PHY_CHECK(matrix == NULL);
        phy_host_fail_alloc_after(0u);
        phy_telemetry_get(&after);
        PHY_CHECK_EQ_INT(after.bytes_live, before.bytes_live);
        PHY_CHECK_EQ_INT(phy_cas_validate(f.cas), PHY_OK);
    }

    fixture_close(&f);
}

static void test_characteristic_polynomial_and_eigenvalues(void)
{
    fixture f = fixture_open();
    const phy_ir_ref x = phy_ir_symbol_ref(
        f.ir, phy_ir_intern(f.ir, "x"));
    PHY_CHECK(x != PHY_IR_NULL);

    static const int64_t rotation_values[] = {0, -1, 1, 0};
    phy_matrix *rotation = make_matrix(&f, 2u, 2u, rotation_values);
    phy_ir_ref characteristic = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_characteristic_polynomial(
            rotation, x, &characteristic), PHY_OK);
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_ir_read(f.ir, "(+ 1 (^ x 2))", &expected, NULL), PHY_OK);
    phy_cas_decision equal = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_equivalent(
            f.cas, characteristic, expected, &equal), PHY_OK);
    PHY_CHECK_EQ_INT(equal, PHY_CAS_ZERO);
    phy_ir_ref eigenvalues = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_eigenvalues(rotation, x, &eigenvalues), PHY_OK);
    char text[512];
    size_t length = 0u;
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, eigenvalues, text, sizeof text, &length),
        PHY_OK);
    PHY_CHECK_EQ_STR(text, "(fn List I (* -1 I))");
    phy_matrix_destroy(rotation);

    static const int64_t repeated_values[] = {2, 1, 0, 2};
    phy_matrix *repeated = make_matrix(&f, 2u, 2u, repeated_values);
    characteristic = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_characteristic_polynomial(
            repeated, x, &characteristic), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, characteristic, text, sizeof text, &length),
        PHY_OK);
    PHY_CHECK_EQ_STR(text, "(+ 4 (* -4 x) (^ x 2))");
    eigenvalues = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_eigenvalues(repeated, x, &eigenvalues), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, eigenvalues, text, sizeof text, &length),
        PHY_OK);
    PHY_CHECK_EQ_STR(text, "(fn List 2 2)");
    phy_matrix_destroy(repeated);

    static const int64_t companion_values[] = {
        0, 0, 2,
        1, 0, 0,
        0, 1, 0};
    phy_matrix *companion = make_matrix(
        &f, 3u, 3u, companion_values);
    eigenvalues = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_matrix_eigenvalues(companion, x, &eigenvalues), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_ir_write(f.ir, eigenvalues, text, sizeof text, &length),
        PHY_OK);
    PHY_CHECK_EQ_STR(
        text,
        "(fn List "
        "(fn Root (fn List -2 0 0 1) 1) "
        "(fn Root (fn List -2 0 0 1) 2) "
        "(fn Root (fn List -2 0 0 1) 3))");
    phy_matrix_destroy(companion);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_shape_and_exact_entries);
    PHY_TEST_CASE(test_add_transpose_and_multiply);
    PHY_TEST_CASE(test_determinant_inverse_rref_and_rank);
    PHY_TEST_CASE(test_exact_linear_solve_and_limits);
    PHY_TEST_CASE(test_vectors_and_null_space);
    PHY_TEST_CASE(test_allocation_failure_unwinds);
    PHY_TEST_CASE(test_characteristic_polynomial_and_eigenvalues);
    return PHY_TEST_REPORT("linear");
}
