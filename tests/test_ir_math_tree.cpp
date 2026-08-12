#include <string>

#include "ir_math_tree.h"
#include "phy/ir.h"
#include "phy_test.h"

namespace {

using nmarkdown::MathNodeId;
using nmarkdown::MathNodeKind;
using nmarkdown::MathTree;
using nmarkdown::MathVariant;

MathTree build_tree(phy_ir_context *ir, const char *source)
{
    phy_ir_ref expression = PHY_IR_NULL;
    PHY_CHECK_EQ_INT(
        phy_ir_read(ir, source, &expression, nullptr), PHY_OK);
    MathTree tree;
    std::string diagnostic;
    PHY_CHECK(phy_build_ir_math_tree(ir, expression, tree, diagnostic));
    PHY_CHECK(diagnostic.empty());
    PHY_CHECK(tree.root != nmarkdown::kInvalidMathNode);
    return tree;
}

bool contains(const MathTree& tree, MathNodeKind kind)
{
    for (const nmarkdown::MathNode& node : tree.nodes) {
        if (node.kind == kind) {
            return true;
        }
    }
    return false;
}

bool contains_text(const MathTree& tree, const char *text)
{
    for (const nmarkdown::MathNode& node : tree.nodes) {
        if ((node.kind == MathNodeKind::Symbol ||
             node.kind == MathNodeKind::Text) &&
            tree.text(node) == text) {
            return true;
        }
    }
    return false;
}

const nmarkdown::MathNode *first(
    const MathTree& tree, MathNodeKind kind)
{
    for (const nmarkdown::MathNode& node : tree.nodes) {
        if (node.kind == kind) {
            return &node;
        }
    }
    return nullptr;
}

MathNodeId child(
    const MathTree& tree, const nmarkdown::MathNode& node, std::size_t index)
{
    if (index >= node.child_count ||
        node.first_child + index >= tree.children.size()) {
        return nmarkdown::kInvalidMathNode;
    }
    return tree.children[node.first_child + index];
}

void test_reciprocal_powers_have_radical_nodes()
{
    phy_ir_context *ir = phy_ir_context_create(nullptr);
    PHY_CHECK(ir != nullptr);

    MathTree tree = build_tree(ir, "(* 6 (^ 2 (rat 1 2)))");
    const nmarkdown::MathNode *radical =
        first(tree, MathNodeKind::Radical);
    PHY_CHECK(radical != nullptr);
    PHY_CHECK_EQ_INT(radical != nullptr ? radical->child_count : 0u, 1u);

    tree = build_tree(ir, "(^ x (rat 1 3))");
    radical = first(tree, MathNodeKind::Radical);
    PHY_CHECK(radical != nullptr);
    PHY_CHECK_EQ_INT(radical != nullptr ? radical->child_count : 0u, 2u);

    tree = build_tree(ir, "(^ x (rat -1 2))");
    PHY_CHECK_EQ_INT(
        tree.nodes[tree.root].kind, MathNodeKind::Fraction);
    radical = first(tree, MathNodeKind::Radical);
    PHY_CHECK(radical != nullptr);

    tree = build_tree(ir, "(^ x (rat 3 2))");
    PHY_CHECK(!contains(tree, MathNodeKind::Radical));
    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Scripts);

    phy_ir_context_destroy(ir);
}

void test_imaginary_unit_is_upright_lowercase()
{
    phy_ir_context *ir = phy_ir_context_create(nullptr);
    PHY_CHECK(ir != nullptr);
    const MathTree tree = build_tree(ir, "I");

    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Styled);
    PHY_CHECK_EQ_INT(
        tree.nodes[tree.root].aux,
        static_cast<unsigned>(MathVariant::Roman));
    const MathNodeId symbol_id = child(tree, tree.nodes[tree.root], 0u);
    PHY_CHECK(symbol_id != nmarkdown::kInvalidMathNode);
    if (symbol_id != nmarkdown::kInvalidMathNode) {
        const nmarkdown::MathNode& symbol = tree.nodes[symbol_id];
        PHY_CHECK_EQ_INT(symbol.kind, MathNodeKind::Symbol);
        PHY_CHECK(tree.text(symbol) == "i");
    }

    phy_ir_context_destroy(ir);
}

void test_discrete_functions_use_mathematical_notation()
{
    phy_ir_context *ir = phy_ir_context_create(nullptr);
    PHY_CHECK(ir != nullptr);

    MathTree tree = build_tree(ir, "(fn factorial (+ 1 x))");
    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Row);
    PHY_CHECK(contains_text(tree, "!"));
    PHY_CHECK(!contains_text(tree, "factorial"));

    tree = build_tree(ir, "(fn pochhammer a n)");
    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Scripts);
    PHY_CHECK(!contains_text(tree, "pochhammer"));

    tree = build_tree(ir, "(fn binomial n k)");
    PHY_CHECK(contains_text(tree, "Binomial"));
    PHY_CHECK(!contains_text(tree, "binomial"));

    tree = build_tree(ir, "(fn Around (rat 3 2) (rat 1 100))");
    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Row);
    PHY_CHECK(contains_text(tree, u8"±"));
    PHY_CHECK(!contains_text(tree, "Around"));

    tree = build_tree(
        ir,
        "(fn ComplexAround (fn Around 1 (rat 1 100)) "
        "(fn Around 2 (rat 1 200)))");
    PHY_CHECK_EQ_INT(tree.nodes[tree.root].kind, MathNodeKind::Row);
    PHY_CHECK(contains_text(tree, u8"±"));
    PHY_CHECK(contains_text(tree, "i"));
    PHY_CHECK(!contains_text(tree, "ComplexAround"));

    tree = build_tree(
        ir,
        "(fn Around (rat 4 3) (rat 1 100000000000))");
    PHY_CHECK(contains_text(tree, "1.3333333333"));
    /* Radius is rounded outward and gains one ulp for midpoint truncation. */
    PHY_CHECK(contains_text(tree, "0.0000000002"));
    PHY_CHECK(!contains_text(tree, "100000000000"));

    tree = build_tree(
        ir,
        "(fn ComplexAround (fn Around (rat 4 3) 0) "
        "(fn Around (rat -7 5) (rat 1 100000000000)))");
    PHY_CHECK(contains_text(tree, "1.4000000000"));
    PHY_CHECK(contains_text(tree, u8"−"));
    PHY_CHECK(contains_text(tree, "i"));
    PHY_CHECK(!contains_text(tree, "-7"));

    tree = build_tree(
        ir,
        "(fn Around (rat 1 3) "
        "(rat 1 1000000000000000000000000000000) 20)");
    PHY_CHECK(contains_text(tree, "0.33333333333333333333"));
    PHY_CHECK(!contains_text(tree, "Around"));

    tree = build_tree(
        ir,
        "(fn Around "
        "(rat 1 100000000000000000000000000000000000000000000000000) "
        "(rat 1 10000000000000000000000000000000000000000000000000000000000000000000000) "
        "20)");
    PHY_CHECK(contains_text(tree, "1.0000000000000000000e-50"));
    PHY_CHECK(!contains_text(tree, "0.0000000000"));

    tree = build_tree(
        ir,
        "(fn Around "
        "100000000000000000000000000000000000000000000000000 "
        "0 20)");
    PHY_CHECK(contains_text(tree, "1.0000000000000000000e50"));
    PHY_CHECK(!contains_text(tree, "100000000000000000000000000000"));

    phy_ir_context_destroy(ir);
}

}  // namespace

int main()
{
    PHY_TEST_CASE(test_reciprocal_powers_have_radical_nodes);
    PHY_TEST_CASE(test_imaginary_unit_is_upright_lowercase);
    PHY_TEST_CASE(test_discrete_functions_use_mathematical_notation);
    return PHY_TEST_REPORT("test_ir_math_tree");
}
