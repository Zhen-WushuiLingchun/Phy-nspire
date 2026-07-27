# Yang--Mills symbolic layer

`include/phy/yang_mills.h` and `src/qft/yang_mills.c` implement the first
bounded gauge-theory layer on the same exact scalar CAS, finite Lie algebra,
and differential forms used by GR. It is native C for Ndless; it does not call
Mathematica, Giac, Python, or a floating-point library.

## Implemented model

A `phy_lie_form` is a dense vector of ordinary differential forms in one fixed
Lie-algebra basis. With

```text
[T_a,T_b] = f[a,b,c] T_c
```

the graded bracket is

```text
[alpha,beta]^c = f[a,b,c] alpha^a wedge beta^b .
```

The convention used by every public operation is:

```text
D_A omega = d omega + g [A,omega]
F_A       = dA + (g/2) [A,A]
delta A   = D_A alpha
delta F   = g [F,alpha]
```

The factor `1/2` is explicit because the dense color sum contains both
`(a,b)` and `(b,a)`. In coordinate components this gives

```text
F^a_mu_nu =
    partial_mu A^a_nu - partial_nu A^a_mu
    + g f[b,c,a] A^b_mu A^c_nu .
```

The layer can:

- create/copy/add/scale Lie-algebra-valued forms;
- take their exact bracket-wedge and exterior derivative;
- compute `D_A`, `F_A`, infinitesimal variations, and the explicit Bianchi
  residual `D_A F_A`;
- prove that residual zero when its scalar components lie in the CAS decision
  class;
- form
  `L = -1/2 h_ab F^a wedge star_g(F^b)` with either a caller-supplied
  invariant bilinear form or the algebra's Killing form.

The `NULL` bilinear default is deliberately the mathematical Killing form,
not a hidden physics trace normalization. In this library's real
anti-Hermitian compact basis it is negative definite; for example, `SU(2)`
has `K_ab=-2 delta_ab`. Consequently the default changes the overall sign
relative to conventions that take a positive representation trace. Callers
matching a particular Yang--Mills action should pass that invariant trace
form explicitly.

`star_g` is the general coordinate-metric Hodge dual. It uses the exact tensor
inverse and determinant, so curved/non-diagonal coordinates retain
`sqrt(|det g|)` symbolically instead of sampling a sign or decimal value.

## Acceptance cases

`tests/test_yang_mills.c` covers:

- `U(1)`: `A=x dy`, exact `F=dx wedge dy`, vanishing nonlinear term and
  Bianchi residual;
- an explicit `U(1)` quadratic density with a representation trace form;
- `SU(2)`: `A^1=dx`, `A^2=dy`, `A^3=dz`, producing all three non-Abelian
  curvature components with their exact signs;
- an `SU(2)` Bianchi residual and a nonzero infinitesimal gauge variation;
- the `SU(2)` Killing-default density, including its compact-basis sign;
- a non-Abelian density through a genuinely non-diagonal coordinate metric;
- typed rejection of a form with the wrong degree.

The underlying Lie suite separately proves antisymmetry and every Jacobi
component at algebra construction for `U(1)`, `SU(2)`, `SO(3)`, `SU(3)`, and
`SO(1,3)`.

## Resource boundary

The algebra dimension is at most eight and the manifold dimension at most
four. A bracket-wedge has at most `8*8*6` scalar component products per output
color, and the general Hodge raise has at most `4^p` ordered terms per
component. All failures are typed, partially built results are destroyed, and
no floating-point fallback exists.

The Bianchi API returns `PHY_ERR_DOMAIN` in dimension below three because this
form representation has no object for the identically zero degree-3 space.

## Not implemented yet

This is the classical connection/curvature foundation, not a full QCD or
perturbative gauge package. Gauge fixing, Faddeev--Popov ghosts, BRST,
three-/four-gauge-boson Feynman rules, polarization sums, Ward identities,
general graph generation, loop reduction, and renormalization are still
future layers. The notebook parser, command palette, and stateful evaluator
materialize and compute typed
`GaugeConnection`, `FieldStrength`, `CovariantD`, `GaugeVariation`, and
`YangMillsLagrangian` objects over named manifold/algebra/form environments.

## Comparison oracles

The implementation is independent and bounded. These upstream projects remain
semantic/golden-test references:

- [SageManifolds differential forms](https://doc.sagemath.org/html/en/reference/manifolds/sage/manifolds/differentiable/diff_form.html)
  for wedge, exterior derivative, interior product, and Hodge conventions;
- [Sage structure-coefficient Lie algebras](https://doc.sagemath.org/html/en/reference/algebras/sage/algebras/lie_algebras/structure_coefficients.html)
  for finite exact Lie-algebra identities;
- [Cadabra](https://kpeeters.github.io/cadabra2/index.html) for
  field-theory/index/property notation;
- [xAct/xTerior](https://www.xact.es/) for Wolfram-style differential forms
  and covariant exterior calculus;
- [FeynCalc](https://feyncalc.github.io/) for later perturbative QFT object
  conventions and golden workflows;
- [FORM](https://form-dev.github.io/form-docs/stable/manual/) for later
  large-expression and color/Dirac stress oracles.

Their desktop runtimes are not linked into the calculator binary.
