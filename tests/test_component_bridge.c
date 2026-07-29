#include "phy/abstract_tensor.h"
#include "phy/cas.h"
#include "phy/component_tensor.h"
#include "phy/ir.h"
#include "phy/platform.h"
#include "phy_test.h"

typedef struct {
    phy_ir_context *ir;
    phy_cas *cas;
    phy_abstract_context *abstract;
} fixture;

static fixture fixture_open(void)
{
    fixture f = {0};
    PHY_CHECK_EQ_INT(phy_platform_init(), PHY_OK);
    f.ir = phy_ir_context_create(NULL);
    PHY_CHECK(f.ir != NULL);
    f.cas = phy_cas_create(f.ir, NULL);
    PHY_CHECK(f.cas != NULL);
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &f.abstract), PHY_OK);
    return f;
}

static void fixture_close(fixture *f)
{
    phy_abstract_context_destroy(f->abstract);
    phy_cas_destroy(f->cas);
    phy_ir_context_destroy(f->ir);
    phy_platform_shutdown();
}

static phy_index_space *make_space(fixture *f, const char *name,
                                   int64_t dimension,
                                   phy_metric_symmetry metric)
{
    phy_index_space *space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            f->abstract, name, phy_ir_integer(f->ir, dimension),
            metric, &space),
        PHY_OK);
    return space;
}

static phy_component_basis *make_basis(
    const phy_index_space *space, const char *name, size_t dimension)
{
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            space, name, dimension, NULL, NULL, &basis),
        PHY_OK);
    return basis;
}

static phy_abstract_tensor_head *make_head(
    fixture *f, const char *name,
    const phy_index_space *const *spaces, size_t rank)
{
    phy_abstract_tensor_head *head = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_head_create(
            f->abstract, name, spaces, rank, PHY_TENSOR_COMMUTING,
            &head),
        PHY_OK);
    return head;
}

static phy_component_tensor *make_tensor(
    const phy_abstract_tensor_head *head,
    phy_component_basis *const *bases,
    const phy_ir_variance *valence)
{
    phy_component_tensor *tensor = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_create(
            head, bases, valence, NULL, &tensor),
        PHY_OK);
    return tensor;
}

static phy_abstract_index make_index(
    const phy_index_space *space, const char *name,
    phy_ir_variance variance)
{
    phy_abstract_index index = {0};
    PHY_CHECK_EQ_INT(
        phy_abstract_index_make(space, name, variance, &index), PHY_OK);
    return index;
}

static void set_integer(fixture *f, phy_component_tensor *tensor,
                        const uint32_t *indices, int64_t value)
{
    PHY_CHECK_EQ_INT(
        phy_component_tensor_set(
            tensor, indices, phy_ir_integer(f->ir, value)),
        PHY_OK);
}

static void check_rational(fixture *f, phy_ir_ref actual,
                           int64_t numerator, int64_t denominator)
{
    phy_ir_ref expected = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(f->cas, numerator, denominator, &expected),
        PHY_OK);
    phy_ir_ref difference = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_sub(f->cas, actual, expected, &difference), PHY_OK);
    phy_cas_decision decision = PHY_CAS_UNKNOWN;
    PHY_CHECK_EQ_INT(
        phy_cas_is_zero(f->cas, difference, &decision), PHY_OK);
    PHY_CHECK_EQ_INT(decision, PHY_CAS_ZERO);
}

static void test_binding_is_idempotent_and_coherent(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 3, PHY_METRIC_NONE);
    phy_component_basis *basis = make_basis(space, "e", 3u);
    phy_component_basis *rival = make_basis(space, "f", 3u);
    const phy_index_space *slots[1] = {space};
    phy_abstract_tensor_head *head =
        make_head(&f, "V", slots, 1u);
    phy_component_basis *bases[1] = {basis};
    const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    phy_component_tensor *vector =
        make_tensor(head, bases, upper);
    phy_component_tensor *other =
        make_tensor(head, bases, upper);

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, vector),
        PHY_ERR_NOT_INITIALIZED);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_basis_count(binding), 1);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, rival),
        PHY_ERR_ALREADY_INITIALIZED);
    PHY_CHECK(
        phy_component_binding_basis(binding, space) == basis);

    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, vector), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, vector), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_tensor_count(binding), 1);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, other),
        PHY_ERR_ALREADY_INITIALIZED);
    PHY_CHECK(
        phy_component_binding_tensor(binding, head) == vector);

    phy_component_binding *crossed = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &crossed),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(crossed, rival), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(crossed, vector),
        PHY_ERR_TYPE);

    phy_abstract_context *foreign_abstract = NULL;
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(
            f.cas, NULL, &foreign_abstract),
        PHY_OK);
    phy_index_space *foreign_space = NULL;
    PHY_CHECK_EQ_INT(
        phy_index_space_create(
            foreign_abstract, "N", phy_ir_integer(f.ir, 3),
            PHY_METRIC_NONE, &foreign_space),
        PHY_OK);
    phy_component_basis *foreign =
        make_basis(foreign_space, "q", 3u);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, foreign),
        PHY_ERR_TYPE);

    phy_component_basis_destroy(foreign);
    phy_abstract_context_destroy(foreign_abstract);
    phy_component_binding_destroy(crossed);
    phy_component_binding_destroy(binding);
    phy_component_tensor_destroy(other);
    phy_component_tensor_destroy(vector);
    phy_component_basis_destroy(rival);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_exact_contraction_free_order_and_pruning(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 3, PHY_METRIC_SYMMETRIC);
    phy_component_basis *basis = make_basis(space, "e", 3u);
    const phy_index_space *pair_slots[2] = {space, space};
    const phy_index_space *one_slot[1] = {space};
    phy_abstract_tensor_head *t_head =
        make_head(&f, "T", pair_slots, 2u);
    phy_abstract_tensor_head *u_head =
        make_head(&f, "U", one_slot, 1u);
    phy_abstract_tensor_head *phi_head =
        make_head(&f, "Phi", NULL, 0u);

    phy_component_basis *pair_bases[2] = {basis, basis};
    phy_component_basis *one_basis[1] = {basis};
    const phy_ir_variance lower_pair[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    phy_component_tensor *t =
        make_tensor(t_head, pair_bases, lower_pair);
    phy_component_tensor *u =
        make_tensor(u_head, one_basis, upper);
    phy_component_tensor *phi =
        make_tensor(phi_head, NULL, NULL);

    for (uint32_t row = 0u; row < 3u; ++row) {
        for (uint32_t column = 0u; column < 3u; ++column) {
            const uint32_t index[2] = {row, column};
            set_integer(&f, t, index,
                        (int64_t)(3u * row + column + 1u));
        }
    }
    static const int64_t u_values[3] = {1, 0, 2};
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        set_integer(&f, u, &axis, u_values[axis]);
    }
    set_integer(&f, phi, NULL, 7);

    const phy_abstract_index t_indices[2] = {
        make_index(space, "b", PHY_IR_INDEX_LOWER),
        make_index(space, "a", PHY_IR_INDEX_LOWER)};
    const phy_abstract_index b_up =
        make_index(space, "b", PHY_IR_INDEX_UPPER);
    const phy_abstract_factor factors[3] = {
        {phi_head, NULL, 0u},
        {t_head, t_indices, 2u},
        {u_head, &b_up, 1u}};
    phy_ir_ref half = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(f.cas, 1, 2, &half), PHY_OK);
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, half, factors, 3u, &monomial),
        PHY_OK);

    phy_abstract_index_use free_slot = {0};
    size_t free_count = 0u;
    PHY_CHECK_EQ_INT(
        phy_component_value_free_slots(
            monomial, &free_slot, 1u, &free_count),
        PHY_OK);
    PHY_CHECK_EQ_INT(free_count, 1);
    PHY_CHECK_EQ_STR(
        phy_ir_symbol_name(f.ir, free_slot.name), "a");

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, phi), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, t), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, u), PHY_OK);

    const uint32_t a_two[1] = {2u};
    phy_ir_ref value = PHY_IR_NULL;
    phy_bridge_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, monomial, a_two, 1u, &value, &stats),
        PHY_OK);
    check_rational(&f, value, 147, 2);
    PHY_CHECK_EQ_INT(stats.free_count, 1);
    PHY_CHECK_EQ_INT(stats.dummy_count, 1);
    PHY_CHECK_EQ_INT(stats.assignments, 2);
    PHY_CHECK_EQ_INT(stats.pruned, 1);
    PHY_CHECK_EQ_INT(stats.terms, 2);
    PHY_CHECK(stats.bytes_used <= 64u * 1024u);

    value = phy_ir_integer(f.ir, 99);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, monomial, a_two, 0u, &value, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);
    const uint32_t out_of_range[1] = {3u};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, monomial, out_of_range, 1u, &value, NULL),
        PHY_ERR_DOMAIN);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    /* A metric-bearing space does not authorize implicit raising. */
    const phy_abstract_index a_up =
        make_index(space, "a", PHY_IR_INDEX_UPPER);
    const phy_abstract_factor wrong_factor = {
        u_head, &a_up, 1u};
    phy_tensor_monomial *right = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &wrong_factor, 1u, &right),
        PHY_OK);
    const phy_abstract_index a_down =
        make_index(space, "a", PHY_IR_INDEX_LOWER);
    const phy_abstract_factor lowered = {
        u_head, &a_down, 1u};
    phy_tensor_monomial *wrong = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &lowered, 1u, &wrong),
        PHY_OK);
    const uint32_t coordinate[1] = {0u};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, right, coordinate, 1u, &value, NULL),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, wrong, coordinate, 1u, &value, NULL),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    phy_tensor_monomial_destroy(wrong);
    phy_tensor_monomial_destroy(right);
    phy_tensor_monomial_destroy(monomial);
    phy_component_binding_destroy(binding);
    phy_component_tensor_destroy(phi);
    phy_component_tensor_destroy(u);
    phy_component_tensor_destroy(t);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_trace_sign_scalars_and_context(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 3, PHY_METRIC_NONE);
    phy_component_basis *basis = make_basis(space, "e", 3u);
    const phy_index_space *pair_slots[2] = {space, space};

    phy_abstract_tensor_head *mixed_head =
        make_head(&f, "Mixed", pair_slots, 2u);
    phy_component_basis *pair_bases[2] = {basis, basis};
    const phy_ir_variance mixed[2] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER};
    phy_component_tensor *mixed_tensor =
        make_tensor(mixed_head, pair_bases, mixed);
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        const uint32_t diagonal[2] = {axis, axis};
        set_integer(&f, mixed_tensor, diagonal,
                    (int64_t)axis + 2);
    }
    const phy_abstract_index trace_indices[2] = {
        make_index(space, "a", PHY_IR_INDEX_UPPER),
        make_index(space, "a", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor trace_factor = {
        mixed_head, trace_indices, 2u};
    phy_tensor_monomial *trace = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &trace_factor, 1u, &trace),
        PHY_OK);

    phy_abstract_tensor_head *form_head =
        make_head(&f, "A", pair_slots, 2u);
    static const uint16_t swap[2] = {1u, 0u};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(form_head, swap, -1), PHY_OK);
    const phy_ir_variance lower_pair[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_component_tensor *form =
        make_tensor(form_head, pair_bases, lower_pair);
    const uint32_t two_zero[2] = {2u, 0u};
    set_integer(&f, form, two_zero, 5);
    uint32_t in_place[2] = {2u, 0u};
    int sign = 0;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_canonical_indices(
            form, in_place, in_place, &sign),
        PHY_OK);
    PHY_CHECK_EQ_INT(in_place[0], 0);
    PHY_CHECK_EQ_INT(in_place[1], 2);
    PHY_CHECK_EQ_INT(sign, -1);

    const phy_abstract_index form_indices[2] = {
        make_index(space, "x", PHY_IR_INDEX_LOWER),
        make_index(space, "y", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor form_factor = {
        form_head, form_indices, 2u};
    phy_tensor_monomial *form_term = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &form_factor, 1u, &form_term),
        PHY_OK);

    phy_abstract_tensor_head *scalar_head =
        make_head(&f, "Phi", NULL, 0u);
    phy_component_tensor *scalar =
        make_tensor(scalar_head, NULL, NULL);
    set_integer(&f, scalar, NULL, 11);
    const phy_abstract_factor scalar_factor = {
        scalar_head, NULL, 0u};
    phy_tensor_monomial *scalar_term = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 2),
            &scalar_factor, 1u, &scalar_term),
        PHY_OK);
    phy_ir_ref three_fifths = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_cas_number(f.cas, 3, 5, &three_fifths), PHY_OK);
    phy_tensor_monomial *coefficient_only = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, three_fifths, NULL, 0u,
            &coefficient_only),
        PHY_OK);

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(
            binding, mixed_tensor),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, form), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, scalar), PHY_OK);

    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, trace, NULL, 0u, &value, NULL),
        PHY_OK);
    check_rational(&f, value, 9, 1);
    const uint32_t zero_two[2] = {0u, 2u};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, form_term, zero_two, 2u, &value, NULL),
        PHY_OK);
    check_rational(&f, value, -5, 1);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, scalar_term, NULL, 0u, &value, NULL),
        PHY_OK);
    check_rational(&f, value, 22, 1);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, coefficient_only, NULL, 0u, &value, NULL),
        PHY_OK);
    check_rational(&f, value, 3, 5);

    /* Even a factorless scalar carries its abstract-context identity. */
    phy_abstract_context *other = NULL;
    PHY_CHECK_EQ_INT(
        phy_abstract_context_create(f.cas, NULL, &other), PHY_OK);
    phy_tensor_monomial *foreign_scalar = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            other, phy_ir_integer(f.ir, 7), NULL, 0u,
            &foreign_scalar),
        PHY_OK);
    value = phy_ir_integer(f.ir, 123);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, foreign_scalar, NULL, 0u, &value, NULL),
        PHY_ERR_TYPE);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    phy_tensor_monomial_destroy(foreign_scalar);
    phy_abstract_context_destroy(other);
    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(coefficient_only);
    phy_tensor_monomial_destroy(scalar_term);
    phy_tensor_monomial_destroy(form_term);
    phy_tensor_monomial_destroy(trace);
    phy_component_tensor_destroy(scalar);
    phy_component_tensor_destroy(form);
    phy_component_tensor_destroy(mixed_tensor);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_only_dummy_axes_are_enumerated(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 3, PHY_METRIC_NONE);
    phy_component_basis *basis = make_basis(space, "e", 3u);
    const phy_index_space *slots[9] = {
        space, space, space, space, space,
        space, space, space, space};
    phy_abstract_tensor_head *head =
        make_head(&f, "T9", slots, 9u);
    phy_component_basis *bases[9] = {
        basis, basis, basis, basis, basis,
        basis, basis, basis, basis};
    const phy_ir_variance valence[9] = {
        PHY_IR_INDEX_UPPER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER,
        PHY_IR_INDEX_LOWER};
    phy_component_tensor *tensor =
        make_tensor(head, bases, valence);
    const uint32_t stored[9] = {
        2u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    set_integer(&f, tensor, stored, 4);

    const phy_abstract_index indices[9] = {
        make_index(space, "d", PHY_IR_INDEX_UPPER),
        make_index(space, "d", PHY_IR_INDEX_LOWER),
        make_index(space, "a", PHY_IR_INDEX_LOWER),
        make_index(space, "b", PHY_IR_INDEX_LOWER),
        make_index(space, "c", PHY_IR_INDEX_LOWER),
        make_index(space, "e", PHY_IR_INDEX_LOWER),
        make_index(space, "f", PHY_IR_INDEX_LOWER),
        make_index(space, "g", PHY_IR_INDEX_LOWER),
        make_index(space, "h", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor factor = {head, indices, 9u};
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &factor, 1u, &monomial),
        PHY_OK);

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, tensor), PHY_OK);
    const uint32_t free_coordinates[7] = {
        0u, 0u, 0u, 0u, 0u, 0u, 0u};
    phy_ir_ref value = PHY_IR_NULL;
    phy_bridge_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            binding, monomial, free_coordinates, 7u,
            &value, &stats),
        PHY_OK);
    check_rational(&f, value, 4, 1);
    PHY_CHECK_EQ_INT(stats.free_count, 7);
    PHY_CHECK_EQ_INT(stats.dummy_count, 1);
    PHY_CHECK_EQ_INT(stats.assignments, 1);
    PHY_CHECK_EQ_INT(stats.pruned, 2);
    PHY_CHECK_EQ_INT(stats.terms, 1);
    PHY_CHECK(stats.bytes_used <= 64u * 1024u);

    phy_component_binding_destroy(binding);
    phy_tensor_monomial_destroy(monomial);
    phy_component_tensor_destroy(tensor);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_young_expression_component_bridge(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 2, PHY_METRIC_NONE);
    phy_component_basis *basis = make_basis(space, "e", 2u);
    const phy_index_space *slots[2] = {space, space};
    phy_abstract_tensor_head *head =
        make_head(&f, "T", slots, 2u);
    phy_component_basis *bases[2] = {basis, basis};
    const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_component_tensor *tensor =
        make_tensor(head, bases, lower);
    static const int64_t entries[2][2] = {{1, 2}, {3, 4}};
    for (uint32_t row = 0u; row < 2u; ++row) {
        for (uint32_t column = 0u; column < 2u; ++column) {
            const uint32_t index[2] = {row, column};
            set_integer(&f, tensor, index, entries[row][column]);
        }
    }

    const phy_abstract_index indices[2] = {
        make_index(space, "a", PHY_IR_INDEX_LOWER),
        make_index(space, "b", PHY_IR_INDEX_LOWER)};
    const phy_abstract_factor factor = {head, indices, 2u};
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            &factor, 1u, &monomial),
        PHY_OK);

    static const uint16_t tableau_slots[2] = {0u, 1u};
    static const uint16_t row_lengths[1] = {2u};
    const phy_young_tableau tableau = {
        tableau_slots, 2u, row_lengths, 1u,
        PHY_YOUNG_ROW_SYMMETRY_LAST};
    phy_tensor_expression *symmetric = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_young_project(
            monomial, 0u, &tableau, NULL, &symmetric, NULL),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_tensor_expression_term_count(symmetric), 2u);

    phy_component_binding *binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(f.abstract, NULL, &binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(binding, tensor), PHY_OK);

    /*
     * The second projected term scans free indices as (b,a). The expression
     * bridge must remap the caller's canonical (a,b) coordinates instead of
     * accidentally evaluating T[0,1] twice.
     */
    const uint32_t a_zero_b_one[2] = {0u, 1u};
    phy_ir_ref value = PHY_IR_NULL;
    phy_bridge_stats stats = {0};
    PHY_CHECK_EQ_INT(
        phy_component_value_expression(
            binding, symmetric, a_zero_b_one, 2u,
            &value, &stats),
        PHY_OK);
    check_rational(&f, value, 5, 2);
    PHY_CHECK_EQ_INT(stats.free_count, 2u);
    PHY_CHECK_EQ_INT(stats.dummy_count, 0u);
    PHY_CHECK_EQ_INT(stats.assignments, 2u);
    PHY_CHECK_EQ_INT(stats.terms, 2u);
    PHY_CHECK_EQ_INT(stats.steps, 0u);
    PHY_CHECK(stats.bytes_used <= 64u * 1024u);

    value = phy_ir_integer(f.ir, 99);
    PHY_CHECK_EQ_INT(
        phy_component_value_expression(
            binding, symmetric, a_zero_b_one, 1u,
            &value, NULL),
        PHY_ERR_INVALID_ARGUMENT);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    phy_bridge_limits one_term = {0};
    one_term.max_terms = 1u;
    phy_component_binding *bounded = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(
            f.abstract, &one_term, &bounded), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(bounded, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(bounded, tensor), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_value_expression(
            bounded, symmetric, a_zero_b_one, 2u,
            &value, NULL),
        PHY_ERR_TERM_LIMIT);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    phy_component_binding_destroy(bounded);
    phy_component_binding_destroy(binding);
    phy_tensor_expression_destroy(symmetric);
    phy_tensor_monomial_destroy(monomial);
    phy_component_tensor_destroy(tensor);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_resource_failures_are_transactional(void)
{
    fixture f = fixture_open();
    phy_index_space *space =
        make_space(&f, "M", 3, PHY_METRIC_NONE);
    phy_component_basis *basis = make_basis(space, "e", 3u);
    const phy_index_space *slots[1] = {space};
    phy_abstract_tensor_head *v_head =
        make_head(&f, "V", slots, 1u);
    phy_abstract_tensor_head *w_head =
        make_head(&f, "W", slots, 1u);
    phy_component_basis *bases[1] = {basis};
    const phy_ir_variance upper[1] = {PHY_IR_INDEX_UPPER};
    const phy_ir_variance lower[1] = {PHY_IR_INDEX_LOWER};
    phy_component_tensor *v =
        make_tensor(v_head, bases, upper);
    phy_component_tensor *w =
        make_tensor(w_head, bases, lower);
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        set_integer(&f, v, &axis, (int64_t)axis + 1);
        set_integer(&f, w, &axis, (int64_t)axis + 2);
    }
    const phy_abstract_index a_up =
        make_index(space, "a", PHY_IR_INDEX_UPPER);
    const phy_abstract_index a_down =
        make_index(space, "a", PHY_IR_INDEX_LOWER);
    const phy_abstract_factor factors[2] = {
        {v_head, &a_up, 1u}, {w_head, &a_down, 1u}};
    phy_tensor_monomial *monomial = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 1),
            factors, 2u, &monomial),
        PHY_OK);

    static const struct {
        size_t max_terms;
        uint32_t max_steps;
        phy_status expected;
    } cases[] = {
        {2u, 0u, PHY_ERR_TERM_LIMIT},
        {0u, 2u, PHY_ERR_TIMEOUT}};
    for (size_t i = 0u;
         i < sizeof cases / sizeof cases[0]; ++i) {
        phy_bridge_limits limits = {0};
        limits.max_terms = cases[i].max_terms;
        limits.max_steps = cases[i].max_steps;
        phy_component_binding *binding = NULL;
        PHY_CHECK_EQ_INT(
            phy_component_binding_create(
                f.abstract, &limits, &binding),
            PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_component_binding_add_basis(binding, basis), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_component_binding_add_tensor(binding, v), PHY_OK);
        PHY_CHECK_EQ_INT(
            phy_component_binding_add_tensor(binding, w), PHY_OK);
        phy_ir_ref value = phy_ir_integer(f.ir, 91);
        phy_bridge_stats stats = {
            1u, 1u, 1u, 1u, 1u, 1u, 1u};
        PHY_CHECK_EQ_INT(
            phy_component_value_monomial(
                binding, monomial, NULL, 0u, &value, &stats),
            cases[i].expected);
        PHY_CHECK_EQ_INT(value, PHY_IR_NULL);
        PHY_CHECK_EQ_INT(stats.free_count, 0);
        PHY_CHECK_EQ_INT(stats.dummy_count, 0);
        PHY_CHECK_EQ_INT(stats.assignments, 0);
        PHY_CHECK_EQ_INT(stats.pruned, 0);
        PHY_CHECK_EQ_INT(stats.terms, 0);
        PHY_CHECK_EQ_INT(stats.bytes_used, 0);
        PHY_CHECK_EQ_INT(stats.steps, 0);
        phy_component_binding_destroy(binding);
    }

    /* Exact zero is proved before a dummy loop consumes the step budget. */
    phy_tensor_monomial *zero_term = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_monomial_create(
            f.abstract, phy_ir_integer(f.ir, 0),
            factors, 2u, &zero_term),
        PHY_OK);
    phy_bridge_limits one_step = {0};
    one_step.max_steps = 1u;
    phy_component_binding *zero_binding = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(
            f.abstract, &one_step, &zero_binding),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(zero_binding, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(zero_binding, v), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(zero_binding, w), PHY_OK);
    phy_ir_ref zero_value = PHY_IR_NULL;
    phy_bridge_stats zero_stats = {0};
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            zero_binding, zero_term, NULL, 0u,
            &zero_value, &zero_stats),
        PHY_OK);
    check_rational(&f, zero_value, 0, 1);
    PHY_CHECK_EQ_INT(zero_stats.assignments, 0);
    PHY_CHECK_EQ_INT(zero_stats.terms, 0);
    phy_component_binding_destroy(zero_binding);
    phy_tensor_monomial_destroy(zero_term);

    phy_bridge_limits cramped_limits = {0};
    cramped_limits.max_bases = 1u;
    cramped_limits.max_tensors = 2u;
    cramped_limits.max_bytes = 256u;
    phy_component_binding *cramped = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(
            f.abstract, &cramped_limits, &cramped),
        PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_basis(cramped, basis), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(cramped, v), PHY_OK);
    PHY_CHECK_EQ_INT(
        phy_component_binding_add_tensor(cramped, w), PHY_OK);
    phy_ir_ref value = phy_ir_integer(f.ir, 17);
    PHY_CHECK_EQ_INT(
        phy_component_value_monomial(
            cramped, monomial, NULL, 0u, &value, NULL),
        PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK_EQ_INT(value, PHY_IR_NULL);

    phy_bridge_limits impossible = {0};
    impossible.max_bytes = 256u;
    phy_component_binding *none = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_binding_create(
            f.abstract, &impossible, &none),
        PHY_ERR_MEMORY_LIMIT);
    PHY_CHECK(none == NULL);

    phy_component_binding_destroy(cramped);
    phy_tensor_monomial_destroy(monomial);
    phy_component_tensor_destroy(w);
    phy_component_tensor_destroy(v);
    phy_component_basis_destroy(basis);
    fixture_close(&f);
}

static void test_legacy_component_lift_is_exact_and_transactional(void)
{
    fixture f = fixture_open();
    const char *coordinates[2] = {"x", "y"};
    phy_chart *chart = NULL;
    PHY_CHECK_EQ_INT(
        phy_chart_create(f.ir, coordinates, 2u, &chart), PHY_OK);
    const phy_ir_variance lower[2] = {
        PHY_IR_INDEX_LOWER, PHY_IR_INDEX_LOWER};
    phy_tensor *source = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "legacy", 2u, lower, &source),
        PHY_OK);
    const unsigned source_indices[4][2] = {
        {0u, 0u}, {0u, 1u}, {1u, 0u}, {1u, 1u}};
    const int64_t source_values[4] = {1, 2, 2, 3};
    for (size_t i = 0u; i < 4u; ++i) {
        PHY_CHECK_EQ_INT(
            phy_tensor_set(
                source, source_indices[i],
                phy_ir_integer(f.ir, source_values[i])),
            PHY_OK);
    }

    phy_index_space *space =
        make_space(&f, "LiftSpace", 2, PHY_METRIC_SYMMETRIC);
    phy_component_basis *basis = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_basis_create(
            space, "coordinate", 2u, coordinates, NULL, &basis),
        PHY_OK);
    const phy_index_space *spaces[2] = {space, space};
    phy_abstract_tensor_head *head =
        make_head(&f, "LiftHead", spaces, 2u);
    const uint16_t transpose[2] = {1u, 0u};
    PHY_CHECK_EQ_INT(
        phy_tensor_head_add_symmetry(head, transpose, 1), PHY_OK);
    phy_component_basis *bases[2] = {basis, basis};

    phy_component_tensor *lifted = NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_import_legacy(
            source, head, bases, NULL, &lifted),
        PHY_OK);
    PHY_CHECK(lifted != NULL);
    PHY_CHECK_EQ_INT(phy_component_tensor_entry_count(lifted), 3);
    uint32_t query[2] = {1u, 0u};
    phy_ir_ref value = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_get(lifted, query, &value), PHY_OK);
    check_rational(&f, value, 2, 1);
    phy_component_tensor_destroy(lifted);

    /*
     * A stronger abstract symmetry must be proved by every dense component.
     * Failure leaves the output null and the source untouched.
     */
    phy_tensor *mismatch = NULL;
    PHY_CHECK_EQ_INT(
        phy_tensor_create(chart, "mismatch", 2u, lower, &mismatch),
        PHY_OK);
    const int64_t mismatch_values[4] = {1, 2, 9, 3};
    for (size_t i = 0u; i < 4u; ++i) {
        PHY_CHECK_EQ_INT(
            phy_tensor_set(
                mismatch, source_indices[i],
                phy_ir_integer(f.ir, mismatch_values[i])),
            PHY_OK);
    }
    lifted = (phy_component_tensor *)(uintptr_t)1u;
    PHY_CHECK_EQ_INT(
        phy_component_tensor_import_legacy(
            mismatch, head, bases, NULL, &lifted),
        PHY_ERR_ASSUMPTION);
    PHY_CHECK(lifted == NULL);
    phy_tensor_component component = {0};
    PHY_CHECK_EQ_INT(
        phy_tensor_get(mismatch, source_indices[2], &component), PHY_OK);
    PHY_CHECK_EQ_INT(component.ref, phy_ir_integer(f.ir, 9));

    phy_component_basis_destroy(basis);
    phy_tensor_destroy(mismatch);
    phy_tensor_destroy(source);
    phy_chart_destroy(chart);
    fixture_close(&f);
}

int main(void)
{
    PHY_TEST_CASE(test_binding_is_idempotent_and_coherent);
    PHY_TEST_CASE(test_exact_contraction_free_order_and_pruning);
    PHY_TEST_CASE(test_trace_sign_scalars_and_context);
    PHY_TEST_CASE(test_only_dummy_axes_are_enumerated);
    PHY_TEST_CASE(test_young_expression_component_bridge);
    PHY_TEST_CASE(test_resource_failures_are_transactional);
    PHY_TEST_CASE(test_legacy_component_lift_is_exact_and_transactional);
    return PHY_TEST_REPORT("component_bridge");
}
