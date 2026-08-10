/*
 * Build the reader-facing CAS/physics tour with the same notebook, parser,
 * evaluator, renderer-facing IR, and document codec used by the calculator.
 *
 * Generation is also an acceptance test: every Math cell is evaluated before
 * the document is written, and one failing cell makes the tool fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phy/formula.h"
#include "phy/notebook.h"
#include "phy/platform.h"

typedef enum {
    TOUR_MARKDOWN = 0,
    TOUR_INPUT
} tour_cell_kind;

typedef struct {
    tour_cell_kind kind;
    const char *primary;
    const char *secondary;
} tour_cell;

static const tour_cell kTour[] = {
    {TOUR_MARKDOWN, "Phy-nspire CAS tour",
     "Markdown flows like a paper: prose wraps at word boundaries, "
     "inline math such as $G_{\\mu\\nu}=8\\pi T_{\\mu\\nu}$ or "
     "$S=\\int d^4x\\sqrt{-g}R$ sits in the line at its own height, "
     "wide display equations shrink and then pan under the arrow keys, "
     "and every result below stays an exact rational all the way to "
     "the pixels."},

    {TOUR_MARKDOWN, "Exact scalar CAS",
     "$$\\frac{1}{3}+\\frac{1}{6}=\\frac{1}{2},\\quad "
     "\\sin^2x+\\cos^2x=1$$"},
    {TOUR_INPUT, "1/3+1/6", NULL},
    {TOUR_INPUT, "Simplify[2*x+3*x-x]", NULL},
    {TOUR_INPUT, "FullSimplify[Sin[x]^2+Cos[x]^2]", NULL},
    {TOUR_INPUT, "Expand[(x+y)^3]", NULL},
    {TOUR_INPUT, "Together[1/x+1/y]", NULL},
    {TOUR_INPUT, "Numerator[(x+y)/(x*y)]", NULL},
    {TOUR_INPUT, "Denominator[(x+y)/(x*y)]", NULL},
    {TOUR_INPUT, "D[Sin[x]^2+Tan[x]+Exp[x]+Log[x],x]", NULL},
    {TOUR_INPUT, "Integrate[x^2*Sin[x],x]", NULL},
    {TOUR_INPUT, "Sin[Pi/6]+Cos[Pi/3]+Tan[Pi/4]", NULL},
    {TOUR_INPUT, "Sqrt[72]", NULL},
    {TOUR_INPUT, "D[ArcTan[x]+Sinh[x]+Erf[x],x]", NULL},
    {TOUR_INPUT, "Integrate[1/(1+x^2)+Exp[-x^2],x]", NULL},
    {TOUR_INPUT,
     "Cancel[(x^31+x^30*y+x*y^30+y^31)"
     "/(x^31+2*x^30*y+x*y^30+2*y^31)]",
     NULL},
    {TOUR_INPUT, "Factor[x^4-1]", NULL},
    {TOUR_INPUT, "Apart[1/(x^2-1)]", NULL},
    {TOUR_MARKDOWN, "Exact complex arithmetic",
     "$$I^2=-1,\\quad \\overline{3+4I}=3-4I,\\quad |3+4I|=5$$"
     " The real and imaginary parts use the same bounded arbitrary-precision "
     "exact domain as rational arithmetic."},
    {TOUR_INPUT, "(1+2*I)*(3-4*I)", NULL},
    {TOUR_INPUT, "Re[3+4*I]+Im[3+4*I]", NULL},
    {TOUR_INPUT, "Conjugate[3+4*I]", NULL},
    {TOUR_INPUT, "Abs[3+4*I]", NULL},
    {TOUR_MARKDOWN, "Certified complex numerics",
     "Every displayed rectangle is an outward rational enclosure. "
     "Complex special functions use bounded series or Stirling remainder "
     "proofs, while higher-degree roots are published only after exact "
     "Pellet--Rouche one-root certificates:"
     "$$\\mathrm{erf}(1+i),\\quad "
     "\\Gamma\\left(\\frac{1}{3}+\\frac{i}{4}\\right),\\quad "
     "x^5-x-1=0.$$"},
    {TOUR_INPUT, "N[Erf[1+I],10]", NULL},
    {TOUR_INPUT, "N[Gamma[1/3+I/4],10]", NULL},
    {TOUR_INPUT, "N[LogGamma[1/3+I/4],10]", NULL},
    {TOUR_INPUT, "N[Digamma[1/3+I/4],10]", NULL},
    {TOUR_INPUT, "NSolve[x^4+1==0,x]", NULL},
    {TOUR_INPUT, "NSolve[x^5-x-1==0,x]", NULL},
    {TOUR_MARKDOWN, "Exact series",
     "$$e^x\\sin x=x+x^2+\\frac{x^3}{3}"
     "-\\frac{x^5}{30}+O(x^6)$$"},
    {TOUR_INPUT, "Series[Exp[x]*Sin[x],{x,0,7}]", NULL},
    {TOUR_INPUT, "Series[1/(x^2*(1+x)),{x,0,4}]", NULL},
    {TOUR_INPUT,
     "Normal[Series[(1+x)^(1/2),{x,0,5}]]", NULL},
    {TOUR_MARKDOWN, "Exact limits",
     "$$\\lim_{x\\to0}\\frac{\\sin x}{x}=1,\\quad "
     "\\lim_{x\\to\\infty}\\frac{3x^4+1}{2x^4-x}=\\frac{3}{2}$$"},
    {TOUR_INPUT, "Limit[(x^2-1)/(x-1),{x,1}]", NULL},
    {TOUR_INPUT, "Limit[Sin[x]/x,{x,0}]", NULL},
    {TOUR_INPUT, "Limit[(1-Cos[x])/x^2,{x,0}]", NULL},
    {TOUR_INPUT, "Limit[1/x,{x,0,FromAbove}]", NULL},
    {TOUR_INPUT, "Limit[1/x,{x,0,FromBelow}]", NULL},
    {TOUR_INPUT,
     "Limit[(3*x^4+1)/(2*x^4-x),{x,Infinity}]", NULL},
    {TOUR_MARKDOWN, "Exact polynomial equations",
     "$$3x-2=0\\quad x=\\frac{2}{3},\\qquad "
     "x^2-2=0\\quad x=\\pm\\sqrt{2}$$"
     " Higher-degree factors proved all-real stay exact as ordered "
     "$\\operatorname{Root}(\\{a_0,\\ldots,a_n\\},k)$ certificates."},
    {TOUR_INPUT, "Solve[3*x-2==0,x]", NULL},
    {TOUR_INPUT, "Solve[x^2-2==0,x]", NULL},
    {TOUR_INPUT, "Solve[x^2+2*x+5==0,x]", NULL},
    {TOUR_INPUT, "Solve[x^3-3*x+1==0,x]", NULL},
    {TOUR_INPUT,
     "Solve[{x+y+z==6,2x-y+z==3,x+2y-z==2},{x,y,z}]",
     NULL},
    {TOUR_INPUT, "Gamma[6]+LogGamma[2]+Erfc[0]", NULL},
    {TOUR_INPUT, "Factorial[50]", NULL},
    {TOUR_INPUT, "Pochhammer[x,5]", NULL},
    {TOUR_INPUT, "Binomial[100,50]", NULL},
    {TOUR_INPUT, "D[Factorial[x],x]", NULL},
    {TOUR_INPUT, "EquivalentQ[Sin[x]^2+Cos[x]^2,1]", NULL},

    {TOUR_MARKDOWN, "Manifolds and component tensors",
     "$$T^{\\mu}{}_{\\nu},\\quad "
     "T^{\\mu}{}_{\\nu\\rho}\\quad (0\\leq r\\leq4)$$"},
    {TOUR_INPUT, "M=Manifold[{theta,phi},Riemannian]", NULL},
    {TOUR_INPUT, "S0=ComponentTensor[M,{},sigma]", NULL},
    {TOUR_INPUT, "T=ComponentTensor[M,{Down,Up},{{1,2},{3,4}}]", NULL},
    {TOUR_INPUT, "Component[T,1,0]", NULL},
    {TOUR_INPUT,
     "Q=ComponentTensor[M,{Down,Up,Down},"
     "{{{1,0},{0,1}},{{2,0},{0,2}}}]",
     NULL},
    {TOUR_INPUT, "Component[Q,1,0,0]", NULL},
    {TOUR_INPUT, "P=Manifold[{z},Euclidean]", NULL},
    {TOUR_INPUT,
     "R4=ComponentTensor[P,{Up,Down,Up,Down},{{{{q}}}}]", NULL},
    {TOUR_INPUT, "L=Manifold[{tcoord,xcoord},Lorentzian]", NULL},
    {TOUR_INPUT,
     "X=Manifold[{r1,r2},{-1,1},Negative]", NULL},
    {TOUR_INPUT,
     "U=Manifold[{c0},Euclidean,Unoriented]", NULL},
    {TOUR_INPUT, "Tensor[H,Down[i],Up[j]]", NULL},

    {TOUR_MARKDOWN, "Abstract-index tensors",
     "$$A_{ab}=-A_{ba},\\qquad "
     "A_{ab}S^{ab}=0$$"
     " Index spaces and tensor heads are coordinate-free. Their rank and "
     "dimension are runtime metadata, while signed slot symmetries, dummy "
     "renaming, metric flips, and identical-factor exchange share one exact "
     "canonicalizer."},
    {TOUR_INPUT, "Va=IndexSpace[4,SymmetricMetric]", NULL},
    {TOUR_INPUT,
     "Aa=TensorHead[{Va,Va},Antisymmetric]", NULL},
    {TOUR_INPUT, "Sa=TensorHead[{Va,Va},Symmetric]", NULL},
    {TOUR_INPUT, "Ta=TensorHead[{Va,Va},Commuting]", NULL},
    {TOUR_INPUT,
     "YoungProject[Ta[Down[i],Down[j]],{{1,2}}]", NULL},
    {TOUR_INPUT,
     "TensorCanonicalize[Aa[Down[j],Down[i]]]", NULL},
    {TOUR_INPUT,
     "TensorCanonicalize["
     "Aa[Down[i],Down[j]]*Sa[Up[i],Up[j]]]",
     NULL},
    {TOUR_INPUT,
     "R5=TensorHead[{Va,Va,Va,Va,Va},Commuting,"
     "{Symmetry[{2,1,3,4,5},-1]}]",
     NULL},
    {TOUR_INPUT,
     "TensorCanonicalize["
     "R5[Down[j],Down[i],Down[k],Down[l],Down[m]]]",
     NULL},
    {TOUR_INPUT, "Rank[R5]", NULL},
    {TOUR_INPUT,
     "RY=TensorHead[{Va,Va,Va,Va},Commuting,"
     "{Symmetry[{2,1,3,4},-1],Symmetry[{1,2,4,3},-1],"
     "Symmetry[{3,4,1,2},1]}]",
     NULL},
    {TOUR_INPUT,
     "YoungDeclare[RY,{{1,3},{2,4}},RowLast]", NULL},
    {TOUR_INPUT, "YoungDimension[{{1,3},{2,4}},4]", NULL},
    {TOUR_INPUT,
     "YoungReduce["
     "RY[Down[a1],Down[b1],Down[c1],Down[d1]]"
     "+RY[Down[a1],Down[c1],Down[d1],Down[b1]]"
     "+RY[Down[a1],Down[d1],Down[b1],Down[c1]]]",
     NULL},

    {TOUR_MARKDOWN, "Exact dynamic linear algebra",
     "$$A=\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix},\\quad "
     "\\det A=-2,\\quad A^{-1}\\in\\mathbb{Q}^{2\\times2}$$"
     " Vector and matrix dimensions are runtime values. Elimination, inverse, "
     "rank, products, and linear solves stay in the exact scalar domain."},
    {TOUR_INPUT, "Dot[Vector[{1,2,3}],Vector[{4,5,6}]]", NULL},
    {TOUR_INPUT, "lm=Matrix[{{1,2},{3,4}}]", NULL},
    {TOUR_INPUT, "Determinant[lm]", NULL},
    {TOUR_INPUT, "Inverse[lm]", NULL},
    {TOUR_INPUT, "LinearSolve[Matrix[{{2,1},{1,-1}}],Vector[{5,1}]]",
     NULL},

    {TOUR_MARKDOWN, "Abstract tensors, components, maps and atlases",
     "$$A_{ij}\\longmapsto A_{01}=a,\\qquad "
     "(u,v)=(x+y,x-y)$$"
     " ComponentValue is the explicit bridge: an abstract tensor never "
     "allocates a dense component cube until bases and free coordinates are "
     "provided. Verified chart transitions share the same exact Jacobian."},
    {TOUR_INPUT, "Vb=IndexSpace[2,SymmetricMetric]", NULL},
    {TOUR_INPUT, "Hb=TensorHead[{Vb,Vb},Antisymmetric]", NULL},
    {TOUR_INPUT, "bxy=ComponentBasis[Vb,{xb,yb}]", NULL},
    {TOUR_INPUT, "buv=ComponentBasis[Vb,{ub,vb}]", NULL},
    {TOUR_INPUT,
     "Hbc=TensorComponents[Hb,{bxy,bxy},{Down,Down},{{{0,1},h}}]",
     NULL},
    {TOUR_INPUT,
     "ComponentValue[Hb[Down[ia],Down[ja]],{Hbc},{0,1}]",
     NULL},
    {TOUR_INPUT,
     "btr=BasisTransition[bxy,buv,{xb+yb,xb-yb},"
     "{(ub+vb)/2,(ub-vb)/2}]",
     NULL},
    {TOUR_INPUT, "Jacobian[btr]", NULL},
    {TOUR_INPUT, "bat=Atlas[{bxy,buv}]", NULL},
    {TOUR_INPUT,
     "bat=AtlasAddTransition[bat,bxy,buv,{xb+yb,xb-yb},"
     "{(ub+vb)/2,(ub-vb)/2}]",
     NULL},
    {TOUR_INPUT, "AtlasVerify[bat]", NULL},
    {TOUR_INPUT, "ClearAll[]", NULL},
    {TOUR_INPUT, "M=Manifold[{theta,phi},Riemannian]", NULL},

    {TOUR_MARKDOWN, "Exterior calculus",
     "$$d^2=0,\\quad \\mathcal{L}_v=d\\iota_v+\\iota_vd$$"},
    {TOUR_INPUT, "a=DifferentialForm[M,1,{0,Sin[theta]}]", NULL},
    {TOUR_INPUT, "ExteriorD[a]", NULL},
    {TOUR_INPUT, "ExteriorD[ExteriorD[a]]", NULL},
    {TOUR_INPUT, "v=VectorField[M,{1,0}]", NULL},
    {TOUR_INPUT, "InteriorProduct[a,v]", NULL},
    {TOUR_INPUT, "LieDerivative[a,v]", NULL},
    {TOUR_INPUT, "Volume[M]", NULL},
    {TOUR_INPUT, "Degree[a]", NULL},
    {TOUR_INPUT, "Dimension[M]", NULL},

    {TOUR_MARKDOWN, "Metric geometry and GR",
     "$$\\Gamma^{\\rho}{}_{\\mu\\nu},\\quad R_{\\mu\\nu},\\quad "
     "G_{\\mu\\nu}$$"},
    {TOUR_INPUT, "g=Metric[M,{{1,0},{0,Sin[theta]^2}}]", NULL},
    {TOUR_INPUT, "HodgeStar[a,g]", NULL},
    {TOUR_INPUT, "c=Curvature[g]", NULL},
    {TOUR_INPUT, "Vgr=IndexSpace[2,SymmetricMetric]", NULL},
    {TOUR_INPUT, "egr=ComponentBasis[Vgr,2]", NULL},
    {TOUR_INPUT,
     "Rgr=TensorHead[{Vgr,Vgr,Vgr,Vgr},Commuting,"
     "{Symmetry[{2,1,3,4},-1],Symmetry[{1,2,4,3},-1],"
     "Symmetry[{3,4,1,2},1]}]",
     NULL},
    {TOUR_INPUT, "InverseMetric[c]", NULL},
    {TOUR_INPUT, "Christoffel[c]", NULL},
    {TOUR_INPUT, "RiemannMixed[c]", NULL},
    {TOUR_INPUT,
     "Rgrc=ComponentLift[Riemann[c],Rgr,{egr,egr,egr,egr}]",
     NULL},
    {TOUR_INPUT,
     "ComponentValue["
     "Rgr[Down[i],Down[j],Down[k],Down[l]],"
     "{Rgrc},{0,1,0,1}]",
     NULL},
    {TOUR_INPUT, "Ricci[c]", NULL},
    {TOUR_INPUT, "RicciScalar[c]", NULL},
    {TOUR_INPUT, "Einstein[c]", NULL},
    {TOUR_INPUT, "Kretschmann[c]", NULL},
    {TOUR_INPUT, "ZeroQ[Weyl[c]]", NULL},
    {TOUR_INPUT, "WeylSquared[c]", NULL},
    {TOUR_INPUT, "GeodesicAcceleration[c,v]", NULL},
    {TOUR_INPUT,
     "gr=GRComponents[c,{Weyl,RiemannUpper}]", NULL},
    {TOUR_INPUT, "Rh=GRHead[gr,Riemann]", NULL},
    {TOUR_INPUT, "Rhc=GRTensor[gr,Riemann]", NULL},
    {TOUR_INPUT,
     "YoungReduce["
     "Rh[Down[ga],Down[gb],Down[gc],Down[gd]]"
     "+Rh[Down[ga],Down[gc],Down[gd],Down[gb]]"
     "+Rh[Down[ga],Down[gd],Down[gb],Down[gc]]]",
     NULL},
    {TOUR_INPUT,
     "ComponentValue["
     "Rh[Down[ga],Down[gb],Down[gc],Down[gd]],"
     "{Rhc},{0,1,0,1}]",
     NULL},

    {TOUR_MARKDOWN, "Lie algebra and Yang-Mills",
     "$$F=dA+\\frac{g}{2}[A,A],\\quad D_AF=0$$"},
    {TOUR_INPUT, "N=Manifold[{u1,u2,u3},Euclidean]", NULL},
    {TOUR_INPUT, "G=LieGroup[SU2]", NULL},
    {TOUR_INPUT, "su=LieAlgebra[G]", NULL},
    {TOUR_INPUT, "e=LieElement[su,{2,0,k}]", NULL},
    {TOUR_INPUT,
     "LieBracket[Generator[su,0],Generator[su,1]]", NULL},
    {TOUR_INPUT, "StructureConstant[su,0,1,2]", NULL},
    {TOUR_INPUT, "Killing[su,0,0]", NULL},
    {TOUR_INPUT,
     "A=GaugeConnection[su,N,{{1,0,0},{0,1,0},{0,0,1}}]", NULL},
    {TOUR_INPUT, "F=FieldStrength[A,kappa]", NULL},
    {TOUR_INPUT, "al=LieForm[su,N,0,{{c1},{c2},{c3}}]", NULL},
    {TOUR_INPUT, "CovariantD[A,al,kappa]", NULL},
    {TOUR_INPUT, "GaugeVariation[A,al,kappa]", NULL},
    {TOUR_INPUT, "ZeroQ[Bianchi[A,kappa]]", NULL},
    {TOUR_INPUT, "ColorComponent[F,0]", NULL},
    {TOUR_INPUT, "gm=Metric[N,{{1,0,0},{0,1,0},{0,0,1}}]", NULL},
    {TOUR_INPUT,
     "YangMillsLagrangian[F,gm,{{1,0,0},{0,1,0},{0,0,1}}]", NULL},

    {TOUR_MARKDOWN, "Scalar QFT, Dirac and colour",
     "$$\\mathcal{L}=\\frac{1}{2}(\\partial\\phi)^2"
     "-\\frac{1}{2}m^2\\phi^2-\\frac{\\lambda}{4!}\\phi^4$$"
     " Lorentz, spinor, adjoint-colour and fundamental-colour indices are "
     "different typed spaces. The shared component picture binds exact "
     "Minkowski and built-in SU(3) invariant tensors to the same abstract "
     "heads used by tensor canonicalization."},
    {TOUR_INPUT, "qft=QFTSystem[3]", NULL},
    {TOUR_INPUT, "Lq=QFTSpace[qft,Lorentz]", NULL},
    {TOUR_INPUT, "Cq=QFTSpace[qft,ColorAdjoint]", NULL},
    {TOUR_INPUT, "Dimension[Cq]", NULL},
    {TOUR_INPUT, "etaD=QFTHead[qft,MinkowskiMetric]", NULL},
    {TOUR_INPUT, "etaDc=QFTTensor[qft,MinkowskiMetric]", NULL},
    {TOUR_INPUT, "etaU=QFTHead[qft,MinkowskiInverse]", NULL},
    {TOUR_INPUT, "etaUc=QFTTensor[qft,MinkowskiInverse]", NULL},
    {TOUR_INPUT,
     "ComponentValue["
     "etaD[Down[qm],Down[qn]]*etaU[Up[qn],Up[qr]],"
     "{etaDc,etaUc},{1,1}]",
     NULL},
    {TOUR_INPUT, "fQ=QFTHead[qft,SUNF]", NULL},
    {TOUR_INPUT, "fQc=QFTTensor[qft,SUNF]", NULL},
    {TOUR_INPUT,
     "ComponentValue[fQ[Up[qa],Up[qb],Up[qc]],"
     "{fQc},{0,1,2}]",
     NULL},
    {TOUR_INPUT, "FQ=QFTHead[qft,FieldStrength]", NULL},
    {TOUR_INPUT,
     "TensorCanonicalize["
     "FQ[Up[qa],Down[qm],Down[qn]]"
     "+FQ[Up[qa],Down[qn],Down[qm]]]",
     NULL},
    {TOUR_INPUT, "Phi4Lagrangian[phi,m,lambda,4]", NULL},
    {TOUR_INPUT, "Phi4EOM[phi,m,lambda,4]", NULL},
    {TOUR_INPUT, "Phi4Diagrams[phi,m,lambda,4,s,t,u]", NULL},
    {TOUR_INPUT,
     "Phi4Graph[phi,m,lambda,4,{0,1},{{0,3},{3,0}}]", NULL},
    {TOUR_INPUT,
     "Phi4Renormalization[phi,m,lambda,4,epsilon,MSBar]", NULL},
    {TOUR_INPUT,
     "Phi4Counterterm[phi,m,lambda,4,epsilon,MS]", NULL},
    {TOUR_INPUT,
     "DiracTrace[{Up[mu,Lorentz],Down[mu,Lorentz]}]", NULL},
    {TOUR_INPUT,
     "MandelstamReduce[LorentzDot[p,q],{p,q,r,k},"
     "{m1,m2,m3,m4},Peskin]",
     NULL},
    {TOUR_INPUT, "SUNDelta[ca,cb,Nc]", NULL},
    {TOUR_INPUT, "SUNF[ca,cb,cc,Nc]", NULL},
    {TOUR_INPUT, "SUND[ca,cb,cc,Nc]", NULL},
    {TOUR_INPUT, "SUNT[ca,Nc]", NULL},
    {TOUR_INPUT, "SUNTrace[{ca,cb,cc},Nc]", NULL},
    {TOUR_INPUT, "SUNCommutator[ca,cb,Nc]", NULL},
    {TOUR_INPUT,
     "SUNDeltaContract[ca,cb,SUNT[cb,Nc],Nc]", NULL},
    {TOUR_INPUT, "SUNCF[Nc]", NULL},
    {TOUR_INPUT, "SUNCA[Nc]", NULL},
    {TOUR_INPUT, "SUNFComponent[3,1,2,3]", NULL},
    {TOUR_INPUT, "SUNExpandCasimirs[C_F+C_A,Nc]", NULL},
    {TOUR_INPUT, "SUNFundamentalCasimir[Nc]", NULL},
    {TOUR_INPUT, "SUNAdjointCasimir[ca,cb,Nc]", NULL},
    {TOUR_INPUT, "ClearAll[]", NULL},

    {TOUR_MARKDOWN, "Deep symbolic stress: Schwarzschild",
     "$$R=0,\\qquad R_{abcd}R^{abcd}=\\frac{12r_s^2}{r^6}$$"},
    {TOUR_INPUT, "S2=Manifold[{tq,rq,thq,phq},Lorentzian]", NULL},
    {TOUR_INPUT,
     "gs=Metric[S2,{{-(1-rs/rq),0,0,0},{0,1/(1-rs/rq),0,0},"
     "{0,0,rq^2,0},{0,0,0,rq^2*Sin[thq]^2}}]",
     NULL},
    {TOUR_INPUT, "cs=Curvature[gs]", NULL},
    {TOUR_INPUT, "RicciScalar[cs]", NULL},
    {TOUR_INPUT, "Kretschmann[cs]", NULL},
    {TOUR_INPUT, "WeylSquared[cs]", NULL},
    {TOUR_INPUT, "Einstein[cs]", NULL},
    {TOUR_INPUT, "ZeroQ[CovariantDerivative[Einstein[cs],cs]]", NULL},
    {TOUR_INPUT,
     "DiracTrace[{Up[mu,Lorentz],Up[nu,Lorentz],Up[rho,Lorentz],"
     "Up[sigma,Lorentz],Up[eta,Lorentz],Up[chi,Lorentz]}]",
     NULL},
    {TOUR_INPUT,
     "DiracTrace[{Up[i1,Lorentz],Up[i2,Lorentz],Up[i3,Lorentz],"
     "Up[i4,Lorentz],Up[i5,Lorentz],Up[i6,Lorentz],Up[i7,Lorentz],"
     "Up[i8,Lorentz]}]",
     NULL},

    {TOUR_MARKDOWN, "Exact decisions and resource bounds",
     "$$L=I-V+1,\\quad \\omega=DL-2I,\\quad "
     "w=\\frac{\\lambda^V}{S}$$"},
    {TOUR_INPUT, "ZeroQ[CovariantDerivative[Ricci[cs],cs]]", NULL},
    {TOUR_INPUT, "MemoryStatus[]", NULL},
};

static int fail_status(const char *operation, phy_status status)
{
    (void)fprintf(
        stderr, "%s: %s\n", operation, phy_status_name(status));
    return 1;
}

static size_t tour_source_cell_count(void)
{
    return sizeof kTour / sizeof kTour[0];
}

static size_t tour_input_count(void)
{
    size_t count = 0u;
    for (size_t index = 0u; index < tour_source_cell_count(); ++index) {
        if (kTour[index].kind == TOUR_INPUT) {
            count++;
        }
    }
    return count;
}

/*
 * A $$..$$ body that fails to lay out degrades to raw LaTeX text on the
 * calculator, so the generator refuses to ship one. Width is no longer a
 * failure: the notebook shrinks an over-wide display formula and pans what
 * still does not fit.
 */
static phy_status validate_markdown_formulas(void)
{
    phy_status status = phy_formula_initialize();
    if (status != PHY_OK) {
        return status;
    }
    for (size_t index = 0u; index < tour_source_cell_count(); ++index) {
        if (kTour[index].kind != TOUR_MARKDOWN ||
            kTour[index].secondary == NULL) {
            continue;
        }
        const char *body = kTour[index].secondary;
        const size_t length = strlen(body);
        phy_formula_metrics metrics;
        if (length >= 4u && body[0] == '$' && body[1] == '$' &&
            body[length - 1u] == '$' && body[length - 2u] == '$') {
            status = phy_formula_measure_latex(
                body + 2, length - 4u, PHY_FORMULA_STYLE_DISPLAY, 17, 0,
                &metrics);
            if (status != PHY_OK || !metrics.valid) {
                char diagnostic[128];
                (void)phy_formula_last_diagnostic(diagnostic,
                                                  sizeof diagnostic);
                (void)fprintf(stderr, "markdown cell %zu: %s (%s)\n",
                              index,
                              status == PHY_OK ? "parse recovered"
                                               : phy_status_name(status),
                              diagnostic);
                return status == PHY_OK ? PHY_ERR_PARSE : status;
            }
            continue;
        }
        /*
         * Inline math flows through the same typesetter, one $..$ token
         * at a time, exactly as the notebook's word-and-formula flow
         * will scan it on the calculator.
         */
        for (const char *cursor = body; *cursor != '\0';) {
            if (cursor[0] != '$' || cursor[1] == '$') {
                cursor++;
                continue;
            }
            const char *close = strchr(cursor + 1, '$');
            if (close == NULL) {
                break;
            }
            status = phy_formula_measure_latex(
                cursor + 1, (size_t)(close - cursor - 1),
                PHY_FORMULA_STYLE_TEXT, 13, 0, &metrics);
            if (status != PHY_OK || !metrics.valid) {
                char diagnostic[128];
                (void)phy_formula_last_diagnostic(diagnostic,
                                                  sizeof diagnostic);
                (void)fprintf(stderr,
                              "markdown cell %zu inline: %s (%s)\n",
                              index,
                              status == PHY_OK ? "parse recovered"
                                               : phy_status_name(status),
                              diagnostic);
                return status == PHY_OK ? PHY_ERR_PARSE : status;
            }
            cursor = close + 1;
        }
    }
    return PHY_OK;
}

static phy_status populate_tour(phy_notebook *notebook)
{
    for (size_t index = 0u; index < tour_source_cell_count(); ++index) {
        const phy_status status =
            kTour[index].kind == TOUR_MARKDOWN
                ? phy_notebook_add_markdown(
                      notebook, kTour[index].primary,
                      kTour[index].secondary, NULL)
                : phy_notebook_add_input(
                      notebook, kTour[index].primary, NULL);
        if (status != PHY_OK) {
            (void)fprintf(stderr, "tour cell %zu: ", index);
            return status;
        }
    }
    return PHY_OK;
}

static void report_failed_inputs(const phy_notebook *notebook)
{
    for (size_t index = 0u;
         index < phy_notebook_cell_count(notebook); ++index) {
        phy_notebook_cell_view view;
        if (phy_notebook_cell(notebook, index, &view) &&
            view.kind == PHY_NOTEBOOK_CELL_INPUT &&
            view.status != PHY_OK) {
            (void)fprintf(stderr, "  input %zu: %s -> %s\n", index,
                          view.primary, phy_status_name(view.status));
        }
    }
}

static phy_status serialize_document(const phy_notebook *notebook,
                                     uint8_t **out_document,
                                     size_t *out_bytes)
{
    *out_document = NULL;
    *out_bytes = 0u;
    phy_status status =
        phy_notebook_serialize(notebook, NULL, 0u, out_bytes);
    if (status != PHY_ERR_INVALID_ARGUMENT || *out_bytes == 0u) {
        return status == PHY_OK ? PHY_ERR_CORRUPT_DOCUMENT : status;
    }
    uint8_t *document = malloc(*out_bytes);
    if (document == NULL) {
        return PHY_ERR_OUT_OF_MEMORY;
    }
    status =
        phy_notebook_serialize(notebook, document, *out_bytes, out_bytes);
    if (status != PHY_OK) {
        free(document);
        return status;
    }
    *out_document = document;
    return PHY_OK;
}

int main(int argc, char **argv)
{
    const char *path =
        argc > 1 ? argv[1] : "examples/phy-nspire-cas-tour.tns";
    if (phy_platform_init() != PHY_OK) {
        return fail_status("platform init", PHY_ERR_BACKEND);
    }

    int result = 1;
    {
        const phy_status markdown_status = validate_markdown_formulas();
        if (markdown_status != PHY_OK) {
            (void)fail_status("markdown formulas", markdown_status);
            phy_platform_shutdown();
            return 1;
        }
    }
    phy_notebook *notebook = phy_notebook_create();
    if (notebook == NULL) {
        (void)fail_status("notebook create", PHY_ERR_OUT_OF_MEMORY);
        goto done;
    }
    phy_status status = populate_tour(notebook);
    if (status != PHY_OK) {
        (void)fail_status("insert validation tour", status);
        goto close_notebook;
    }

    status = phy_notebook_evaluate_all(notebook);
    if (status != PHY_OK) {
        (void)fail_status("evaluate validation tour", status);
        report_failed_inputs(notebook);
        goto close_notebook;
    }

    size_t bytes = 0u;
    uint8_t *document = NULL;
    status = serialize_document(notebook, &document, &bytes);
    if (status != PHY_OK) {
        (void)fail_status("serialize validation tour", status);
        goto close_notebook;
    }

    phy_notebook *reopened = NULL;
    status = phy_notebook_deserialize(document, bytes, &reopened);
    if (status != PHY_OK || reopened == NULL ||
        phy_notebook_cell_count(reopened) !=
            phy_notebook_cell_count(notebook)) {
        phy_notebook_destroy(reopened);
        free(document);
        (void)fail_status(
            "reopen validation tour",
            status != PHY_OK ? status : PHY_ERR_CORRUPT_DOCUMENT);
        goto close_notebook;
    }
    status = phy_notebook_evaluate_all(reopened);
    phy_notebook_destroy(reopened);
    reopened = NULL;
    free(document);
    document = NULL;
    if (status != PHY_OK) {
        (void)fail_status("replay validation tour", status);
        goto close_notebook;
    }

    /*
     * The distributable document intentionally contains source cells only.
     * Persisting every cached input IR tree and output tree makes opening the
     * expanded validation document rebuild the entire physics
     * session at once. The cached artifact round-trips on the host, but the
     * byte-identical file was reported as corrupt by a CX II at open time. The
     * eager IR/heap reconstruction is the platform-specific part of that path.
     *
     * Keeping the source cells gives the calculator a cheap, deterministic
     * open path and makes the tour an honest device exercise: the reader runs
     * the Math cells to produce their outputs. The fully evaluated round trip
     * above remains the generation gate, so no CAS coverage is lost.
     */
    phy_notebook_destroy(notebook);
    notebook = phy_notebook_create();
    if (notebook == NULL) {
        (void)fail_status("source notebook create", PHY_ERR_OUT_OF_MEMORY);
        goto done;
    }
    status = populate_tour(notebook);
    if (status != PHY_OK) {
        (void)fail_status("insert source tour", status);
        goto close_notebook;
    }
    status = serialize_document(notebook, &document, &bytes);
    if (status != PHY_OK) {
        (void)fail_status("serialize source tour", status);
        goto close_notebook;
    }
    status = phy_notebook_deserialize(document, bytes, &reopened);
    if (status != PHY_OK || reopened == NULL ||
        phy_notebook_cell_count(reopened) != tour_source_cell_count()) {
        phy_notebook_destroy(reopened);
        reopened = NULL;
        free(document);
        document = NULL;
        (void)fail_status(
            "reopen source tour",
            status != PHY_OK ? status : PHY_ERR_CORRUPT_DOCUMENT);
        goto close_notebook;
    }
    status = phy_notebook_evaluate_all(reopened);
    if (status != PHY_OK) {
        report_failed_inputs(reopened);
    }
    phy_notebook_destroy(reopened);
    reopened = NULL;
    if (status != PHY_OK) {
        free(document);
        document = NULL;
        (void)fail_status("run reopened source tour", status);
        goto close_notebook;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(document);
        document = NULL;
        (void)fprintf(stderr, "open output: %s\n", path);
        goto close_notebook;
    }
    const size_t written = fwrite(document, 1u, bytes, file);
    const int closed = fclose(file);
    free(document);
    document = NULL;
    if (written != bytes || closed != 0) {
        (void)fprintf(stderr, "write output: %s\n", path);
        goto close_notebook;
    }
    (void)printf(
        "CAS tour: %zu source cells, %zu inputs validated, %zu bytes -> %s\n",
        tour_source_cell_count(), tour_input_count(), bytes, path);
    result = 0;

close_notebook:
    phy_notebook_destroy(notebook);
done:
    phy_platform_shutdown();
    return result;
}
