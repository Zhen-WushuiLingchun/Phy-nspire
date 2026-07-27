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
    {TOUR_INPUT, "Integrate[3*x^2+Cos[x]+Exp[x],x]", NULL},
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

    {TOUR_MARKDOWN, "Exterior calculus",
     "$$d^2=0,\\quad \\mathcal{L}_v=d\\iota_v+\\iota_vd$$"},
    {TOUR_INPUT, "a=DifferentialForm[M,1,{0,Sin[theta]}]", NULL},
    {TOUR_INPUT, "ExteriorD[a]", NULL},
    {TOUR_INPUT, "v=VectorField[M,{1,0}]", NULL},
    {TOUR_INPUT, "InteriorProduct[a,v]", NULL},
    {TOUR_INPUT, "LieDerivative[a,v]", NULL},
    {TOUR_INPUT, "Volume[M]", NULL},
    {TOUR_INPUT, "Degree[a]", NULL},
    {TOUR_INPUT, "Dimension[M]", NULL},
    {TOUR_INPUT, "Rank[T]", NULL},

    {TOUR_MARKDOWN, "Metric geometry and GR",
     "$$\\Gamma^{\\rho}{}_{\\mu\\nu},\\quad R_{\\mu\\nu},\\quad "
     "G_{\\mu\\nu}$$"},
    {TOUR_INPUT, "g=Metric[M,{{1,0},{0,Sin[theta]^2}}]", NULL},
    {TOUR_INPUT, "HodgeStar[a,g]", NULL},
    {TOUR_INPUT, "c=Curvature[g]", NULL},
    {TOUR_INPUT, "InverseMetric[c]", NULL},
    {TOUR_INPUT, "Christoffel[c]", NULL},
    {TOUR_INPUT, "RiemannMixed[c]", NULL},
    {TOUR_INPUT, "Riemann[c]", NULL},
    {TOUR_INPUT, "Ricci[c]", NULL},
    {TOUR_INPUT, "RicciScalar[c]", NULL},
    {TOUR_INPUT, "Einstein[c]", NULL},
    {TOUR_INPUT, "Kretschmann[c]", NULL},
    {TOUR_INPUT, "ZeroQ[Weyl[c]]", NULL},
    {TOUR_INPUT, "WeylSquared[c]", NULL},
    {TOUR_INPUT, "GeodesicAcceleration[c,v]", NULL},

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
     "$$\\mathcal{L}=\\frac12(\\partial\\phi)^2-\\frac12m^2\\phi^2"
     "-\\frac{\\lambda}{4!}\\phi^4$$"},
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

    {TOUR_MARKDOWN, "Exact decisions and resource bounds",
     "$$L=I-V+1,\\quad \\omega=DL-2I,\\quad "
     "w=\\frac{\\lambda^V}{S}$$"},
    {TOUR_INPUT, "ZeroQ[CovariantDerivative[Ricci[c],c]]", NULL},
    {TOUR_INPUT, "MemoryStatus[]", NULL},
};

static int fail_status(const char *operation, phy_status status)
{
    (void)fprintf(
        stderr, "%s: %s\n", operation, phy_status_name(status));
    return 1;
}

int main(int argc, char **argv)
{
    const char *path =
        argc > 1 ? argv[1] : "examples/phy-nspire-cas-tour.tns";
    if (phy_platform_init() != PHY_OK) {
        return fail_status("platform init", PHY_ERR_BACKEND);
    }

    int result = 1;
    phy_notebook *notebook = phy_notebook_create();
    if (notebook == NULL) {
        (void)fail_status("notebook create", PHY_ERR_OUT_OF_MEMORY);
        goto done;
    }
    for (size_t index = 0u;
         index < sizeof kTour / sizeof kTour[0]; ++index) {
        const phy_status status =
            kTour[index].kind == TOUR_MARKDOWN
                ? phy_notebook_add_markdown(
                      notebook, kTour[index].primary,
                      kTour[index].secondary, NULL)
                : phy_notebook_add_input(
                      notebook, kTour[index].primary, NULL);
        if (status != PHY_OK) {
            (void)fprintf(stderr, "tour cell %zu: ", index);
            (void)fail_status("insert", status);
            goto close_notebook;
        }
    }

    {
        const phy_status status = phy_notebook_evaluate_all(notebook);
        if (status != PHY_OK) {
            (void)fail_status("evaluate tour", status);
            for (size_t index = 0u;
                 index < phy_notebook_cell_count(notebook); ++index) {
                phy_notebook_cell_view view;
                if (phy_notebook_cell(notebook, index, &view) &&
                    view.kind == PHY_NOTEBOOK_CELL_INPUT &&
                    view.status != PHY_OK) {
                    (void)fprintf(
                        stderr, "  input %zu: %s -> %s\n", index,
                        view.primary, phy_status_name(view.status));
                }
            }
            goto close_notebook;
        }
    }

    size_t bytes = 0u;
    phy_status status =
        phy_notebook_serialize(notebook, NULL, 0u, &bytes);
    if (status != PHY_ERR_INVALID_ARGUMENT || bytes == 0u) {
        (void)fail_status("size notebook", status);
        goto close_notebook;
    }
    uint8_t *document = malloc(bytes);
    if (document == NULL) {
        (void)fail_status("allocate document", PHY_ERR_OUT_OF_MEMORY);
        goto close_notebook;
    }
    status = phy_notebook_serialize(notebook, document, bytes, &bytes);
    if (status != PHY_OK) {
        free(document);
        (void)fail_status("serialize notebook", status);
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
            "reopen notebook",
            status != PHY_OK ? status : PHY_ERR_CORRUPT_DOCUMENT);
        goto close_notebook;
    }
    status = phy_notebook_evaluate_all(reopened);
    phy_notebook_destroy(reopened);
    if (status != PHY_OK) {
        free(document);
        (void)fail_status("replay reopened notebook", status);
        goto close_notebook;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(document);
        (void)fprintf(stderr, "open output: %s\n", path);
        goto close_notebook;
    }
    const size_t written = fwrite(document, 1u, bytes, file);
    const int closed = fclose(file);
    free(document);
    if (written != bytes || closed != 0) {
        (void)fprintf(stderr, "write output: %s\n", path);
        goto close_notebook;
    }
    (void)printf(
        "CAS tour: %zu cells, %zu bytes, all inputs passed -> %s\n",
        phy_notebook_cell_count(notebook), bytes, path);
    result = 0;

close_notebook:
    phy_notebook_destroy(notebook);
done:
    phy_platform_shutdown();
    return result;
}
