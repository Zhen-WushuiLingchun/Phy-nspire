#include "phy/palette.h"

#define ARRAY_COUNT(items) (sizeof(items) / sizeof((items)[0]))

typedef struct {
    const char *name;
    const phy_palette_entry *entries;
    size_t count;
} palette_category;

static const phy_palette_entry kCasAlgebra[] = {
    {"Simplify[...]", "Simplify[]", 9u},
    {"FullSimplify[...]", "FullSimplify[]", 13u},
    {"Expand[...]", "Expand[]", 7u},
    {"Together[...]", "Together[]", 9u},
    {"Numerator[...]", "Numerator[]", 10u},
    {"Denominator[...]", "Denominator[]", 12u},
};

static const phy_palette_entry kCasFunctions[] = {
    {"Sin[...]", "Sin[]", 4u},
    {"Cos[...]", "Cos[]", 4u},
    {"Tan[...]", "Tan[]", 4u},
    {"Exp[...]", "Exp[]", 4u},
    {"Log[...]", "Log[]", 4u},
    {"Sqrt[...]", "Sqrt[]", 5u},
    {"Rational[..., ...]", "Rational[,]", 9u},
};

static const phy_palette_entry kCasCalculus[] = {
    {"D[..., x]", "D[, x]", 2u},
    {"D[..., x, y]", "D[, x, y]", 2u},
    {"Integrate[..., x]", "Integrate[, x]", 10u},
    {"Power[..., ...]", "Power[,]", 6u},
    {"Plus[..., ...]", "Plus[,]", 5u},
    {"Times[..., ...]", "Times[,]", 6u},
    {"Equal[..., ...]", "Equal[,]", 6u},
    {"List {...}", "{}", 1u},
};

static const phy_palette_entry kCasPhysics[] = {
    {"Upper Lorentz index", "Up[,Lorentz]", 3u},
    {"Lower Lorentz index", "Down[,Lorentz]", 5u},
    {"Rank-2 tensor",
     "Tensor[g,Down[mu,Lorentz],Down[nu,Lorentz]]", 7u},
    {"Indexed operator", "Operator[Gamma,Up[mu,Lorentz]]", 9u},
    {"Noncommutative product", "NonCommutativeMultiply[,]",
     sizeof("NonCommutativeMultiply[") - 1u},
    {"Exterior product", "Wedge[,]", sizeof("Wedge[") - 1u},
};

/*
 * These snippets are the argument shapes src/eval actually dispatches on, not
 * illustrative spellings. A palette that inserts something the evaluator
 * rejects is worse than no palette; tests/test_palette.c parses every one of
 * them.
 */
static const phy_palette_entry kCasGeometry[] = {
    {"Define manifold", "M = Manifold[{t,x,y,z},Lorentzian]",
     sizeof("M = Manifold[{") - 1u},
    {"Differential form", "a = DifferentialForm[M,1,{0,0,0,0}]",
     sizeof("a = DifferentialForm[") - 1u},
    {"Coordinate metric", "g = Metric[M,{{1,0},{0,1}}]",
     sizeof("g = Metric[") - 1u},
    {"Vector field", "v = VectorField[M,{1,0}]",
     sizeof("v = VectorField[") - 1u},
    {"Exterior derivative", "ExteriorD[a]", sizeof("ExteriorD[") - 1u},
    {"Interior product", "InteriorProduct[a,v]",
     sizeof("InteriorProduct[") - 1u},
    {"Hodge dual (metric)", "HodgeStar[a,g]",
     sizeof("HodgeStar[") - 1u},
    {"Form wedge product", "Wedge[a,b]", sizeof("Wedge[") - 1u},
    {"Volume form", "Volume[M,g]", sizeof("Volume[") - 1u},
    {"Component of object", "Component[a,0]", sizeof("Component[") - 1u},
    {"Vanishing decision", "ZeroQ[a]", sizeof("ZeroQ[") - 1u},
};

static const phy_palette_entry kCasLieQft[] = {
    {"Built-in Lie group", "G = LieGroup[SU2]",
     sizeof("G = LieGroup[") - 1u},
    {"Lie algebra of group", "su = LieAlgebra[G]",
     sizeof("su = LieAlgebra[") - 1u},
    {"Basis generator", "Generator[su,0]", sizeof("Generator[") - 1u},
    {"Lie bracket", "LieBracket[X,Y]", sizeof("LieBracket[") - 1u},
    {"Structure constant", "StructureConstant[su,0,1,2]",
     sizeof("StructureConstant[") - 1u},
    {"Killing form", "Killing[su,0,0]", sizeof("Killing[") - 1u},
    {"Gauge connection", "A = GaugeConnection[su,M,{{0,0},{0,0},{0,0}}]",
     sizeof("A = GaugeConnection[") - 1u},
    {"Field strength", "F = FieldStrength[A,g]",
     sizeof("F = FieldStrength[") - 1u},
    {"Covariant derivative", "CovariantD[A,w,g]",
     sizeof("CovariantD[") - 1u},
    {"Gauge variation", "GaugeVariation[A,al,g]",
     sizeof("GaugeVariation[") - 1u},
    {"Bianchi residual", "ZeroQ[Bianchi[A,g]]",
     sizeof("ZeroQ[Bianchi[") - 1u},
    {"Yang-Mills density", "YangMillsLagrangian[F,g]",
     sizeof("YangMillsLagrangian[") - 1u},
    {"Curvature bundle", "c = Curvature[g]", sizeof("c = Curvature[") - 1u},
    {"Ricci scalar", "RicciScalar[c]", sizeof("RicciScalar[") - 1u},
};

static const palette_category kCasCategories[] = {
    {"Algebra", kCasAlgebra, ARRAY_COUNT(kCasAlgebra)},
    {"Functions", kCasFunctions, ARRAY_COUNT(kCasFunctions)},
    {"Calculus/Syntax", kCasCalculus, ARRAY_COUNT(kCasCalculus)},
    {"Tensor/Indices", kCasPhysics, ARRAY_COUNT(kCasPhysics)},
    {"Differential Geometry", kCasGeometry, ARRAY_COUNT(kCasGeometry)},
    {"Lie/QFT Objects", kCasLieQft, ARRAY_COUNT(kCasLieQft)},
};

static const phy_palette_entry kLatexLayout[] = {
    {"Inline math $...$", "$$", 1u},
    {"Display math $$...$$", "$$$$", 2u},
    {"Fraction", "\\frac{}{}", 6u},
    {"Square root", "\\sqrt{}", 6u},
    {"Superscript", "^{}", 2u},
    {"Subscript", "_{}", 2u},
    {"Auto parentheses", "\\left(\\right)", 6u},
};

static const phy_palette_entry kLatexCalculus[] = {
    {"Integral", "\\int_{}^{}", 6u},
    {"Sum", "\\sum_{}^{}", 6u},
    {"Product", "\\prod_{}^{}", 7u},
    {"Partial derivative", "\\partial", 8u},
    {"Nabla", "\\nabla", 6u},
    {"Infinity", "\\infty", 6u},
    {"Differential dot", "\\cdot", 5u},
};

static const phy_palette_entry kLatexGreekA[] = {
    {"alpha", "\\alpha", 6u},
    {"beta", "\\beta", 5u},
    {"gamma", "\\gamma", 6u},
    {"delta", "\\delta", 6u},
    {"epsilon", "\\epsilon", 8u},
    {"theta", "\\theta", 6u},
    {"lambda", "\\lambda", 7u},
};

static const phy_palette_entry kLatexGreekB[] = {
    {"mu", "\\mu", 3u},
    {"nu", "\\nu", 3u},
    {"rho", "\\rho", 4u},
    {"sigma", "\\sigma", 6u},
    {"phi", "\\phi", 4u},
    {"psi", "\\psi", 4u},
    {"omega", "\\omega", 6u},
};

static const phy_palette_entry kLatexStyle[] = {
    {"Vector accent", "\\vec{}", 5u},
    {"Hat accent", "\\hat{}", 5u},
    {"Bar accent", "\\bar{}", 5u},
    {"Bold", "\\mathbf{}", 8u},
    {"Blackboard", "\\mathbb{}", 8u},
    {"Calligraphic", "\\mathcal{}", 9u},
    {"Roman", "\\mathrm{}", 8u},
};

static const phy_palette_entry kLatexMatrices[] = {
    {"Matrix 2x2", "\\begin{matrix} & \\\\ & \\end{matrix}", 15u},
    {"Parenthesized 2x2",
     "\\begin{pmatrix} & \\\\ & \\end{pmatrix}", 16u},
    {"Bracketed 2x2",
     "\\begin{bmatrix} & \\\\ & \\end{bmatrix}", 16u},
    {"Cases 2x2", "\\begin{cases} & \\\\ & \\end{cases}", 14u},
    {"Aligned 2x2", "\\begin{aligned} & \\\\ & \\end{aligned}", 16u},
};

static const palette_category kLatexCategories[] = {
    {"Layout", kLatexLayout, ARRAY_COUNT(kLatexLayout)},
    {"Calculus", kLatexCalculus, ARRAY_COUNT(kLatexCalculus)},
    {"Greek a-lambda", kLatexGreekA, ARRAY_COUNT(kLatexGreekA)},
    {"Greek mu-omega", kLatexGreekB, ARRAY_COUNT(kLatexGreekB)},
    {"Style/Accent", kLatexStyle, ARRAY_COUNT(kLatexStyle)},
    {"Matrices", kLatexMatrices, ARRAY_COUNT(kLatexMatrices)},
};

static const palette_category *categories(phy_palette_kind kind,
                                          size_t *out_count)
{
    if (kind == PHY_PALETTE_CAS) {
        *out_count = ARRAY_COUNT(kCasCategories);
        return kCasCategories;
    }
    if (kind == PHY_PALETTE_LATEX) {
        *out_count = ARRAY_COUNT(kLatexCategories);
        return kLatexCategories;
    }
    *out_count = 0u;
    return NULL;
}

size_t phy_palette_category_count(phy_palette_kind kind)
{
    size_t count = 0u;
    (void)categories(kind, &count);
    return count;
}

const char *phy_palette_category_name(phy_palette_kind kind,
                                      size_t category)
{
    size_t count = 0u;
    const palette_category *items = categories(kind, &count);
    return items != NULL && category < count ? items[category].name : NULL;
}

size_t phy_palette_entry_count(phy_palette_kind kind, size_t category)
{
    size_t count = 0u;
    const palette_category *items = categories(kind, &count);
    return items != NULL && category < count ? items[category].count : 0u;
}

bool phy_palette_get(phy_palette_kind kind, size_t category, size_t entry,
                     phy_palette_entry *out_entry)
{
    if (out_entry == NULL) {
        return false;
    }
    size_t count = 0u;
    const palette_category *items = categories(kind, &count);
    if (items == NULL || category >= count ||
        entry >= items[category].count) {
        return false;
    }
    *out_entry = items[category].entries[entry];
    return true;
}
