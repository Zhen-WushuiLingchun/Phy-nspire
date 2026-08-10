(*
  Development-only Wolfram oracle for exact branch and identity checks.
  The native test suite does not require Wolfram.  Numerical values here are
  cross-checks only; native outward enclosures and residual checks remain the
  acceptance criterion.
*)

VerificationTest[
  FullSimplify[Sin[x]^2 + Cos[x]^2, Assumptions -> Element[x, Reals]],
  1,
  TestID -> "trigonometric-pythagorean"
]

VerificationTest[
  FullSimplify[Cosh[x]^2 - Sinh[x]^2, Assumptions -> Element[x, Reals]],
  1,
  TestID -> "hyperbolic-pythagorean"
]

VerificationTest[
  D[{ArcSin[x], ArcCos[x], ArcTan[x]}, x],
  {1/Sqrt[1 - x^2], -1/Sqrt[1 - x^2], 1/(1 + x^2)},
  TestID -> "inverse-trigonometric-derivatives"
]

VerificationTest[
  D[{ArcSinh[x], ArcCosh[x], ArcTanh[x]}, x],
  {1/Sqrt[1 + x^2], 1/(Sqrt[-1 + x] Sqrt[1 + x]), 1/(1 - x^2)},
  TestID -> "inverse-hyperbolic-derivatives"
]

VerificationTest[
  FullSimplify[
    {ArcSin[Sin[x]], ArcTan[Tan[x]], ArcTanh[Tanh[x]]},
    Assumptions -> Element[x, Reals] && -Pi/2 < x < Pi/2
  ],
  {x, x, x},
  TestID -> "inverse-cancellation-on-principal-real-domain"
]

VerificationTest[
  ComplexExpand[
    {Log[-1], Sqrt[-1], ArcSin[2], ArcCosh[1/2], ArcTanh[2]}
  ],
  {I Pi, I, Pi/2 - I Log[2 + Sqrt[3]], I Pi/3,
   Log[3]/2 - I Pi/2},
  TestID -> "principal-complex-branches"
]

VerificationTest[
  FunctionExpand[{Gamma[5/2], PolyGamma[0, 1/2], Pochhammer[a, -2]}],
  {3 Sqrt[Pi]/4, -EulerGamma - Log[4], 1/((-2 + a) (-1 + a))},
  TestID -> "special-function-exact-recurrences"
]

VerificationTest[
  D[{Erf[x], Erfc[x]}, x],
  {2 E^(-x^2)/Sqrt[Pi], -2 E^(-x^2)/Sqrt[Pi]},
  TestID -> "error-function-derivatives"
]

VerificationTest[
  Integrate[Exp[-x^2], x],
  Sqrt[Pi] Erf[x]/2,
  TestID -> "gaussian-antiderivative"
]

VerificationTest[
  Sort[x /. Solve[x^2 + 1 == 0, x]],
  {-I, I},
  TestID -> "complex-quadratic-pure-imaginary"
]

VerificationTest[
  Sort[x /. Solve[x^2 - 2 x + 5 == 0, x]],
  {1 - 2 I, 1 + 2 I},
  TestID -> "complex-quadratic-shifted"
]

VerificationTest[
  And @@ ((MinimalPolynomial[#, t] === 1 + t + t^4) & /@
      (x /. Solve[x^4 + x + 1 == 0, x])),
  True,
  TestID -> "quartic-complex-root-minimal-polynomial"
]

VerificationTest[
  Max[Abs[
    N[{Erf[1 + I], Gamma[1/3 + I/4], LogGamma[1/3 + I/4],
       PolyGamma[0, 1/3 + I/4]}, 55] -
      {1.3161512816979476448802710802436703690277065292520164 +
         0.1904534692378346862841088619691624424377773097507147 I,
       1.6191843931152449547086980273864253389528415243595934 -
         1.2924161395422262849178721734235210029473491194207131 I,
       0.7283877795263569498597500791301504659943119445665332 -
         0.6736360631622727128601093672175258729804115813350632 I,
      -2.0179324938910103843602868661656700038220389241277520 +
         1.7083870710016193591243432468276773227186884818533641 I}
  ]] < 10^-48,
  True,
  TestID -> "complex-special-function-high-precision-values"
]

VerificationTest[
  FullSimplify[
    {Gamma[z + 1]/Gamma[z],
     PolyGamma[0, z + 1] - PolyGamma[0, z]},
    Assumptions -> Re[z] > 0
  ],
  {z, 1/z},
  TestID -> "gamma-digamma-complex-recurrences"
]

VerificationTest[
  Max[Abs[
    N[{Gamma[-1/2 + I/4], LogGamma[-1/2 + I/4],
       PolyGamma[0, -1/2 + I/4]}, 50] -
      {-2.7547269757896257348641135104089168202124484743202 -
         0.0310004163754133890422866511639654776094838927370 I,
       1.0133816533627673935537953741440033835322822796316 -
         3.1303395936331459363833694919496765114497807503794 I,
       0.0618387442907642550181889581873681551922205312056 +
         1.8301191246287899840696057437288670904788730718373 I}
  ]] < 10^-46,
  True,
  TestID -> "complex-gamma-family-negative-half-plane"
]

VerificationTest[
  With[{roots = x /. NSolve[x^5 - x - 1 == 0, x,
                              WorkingPrecision -> 60]},
    Length[roots] == 5 &&
      Max[Abs[N[#^5 - # - 1, 50] & /@ roots]] < 10^-45
  ],
  True,
  TestID -> "quintic-general-complex-root-count-and-residual"
]

VerificationTest[
  With[{roots = x /. NSolve[x^4 + 1 == 0, x,
                              WorkingPrecision -> 60]},
    Length[roots] == 4 &&
      Max[Abs[N[#^4 + 1, 50] & /@ roots]] < 10^-45
  ],
  True,
  TestID -> "symmetric-quartic-complex-root-count-and-residual"
]

VerificationTest[
  FullSimplify[
    Exp[Log[x]],
    Assumptions -> Element[x, Reals] && x > 0
  ],
  x,
  TestID -> "exp-log-positive-real"
]

VerificationTest[
  FullSimplify[Sqrt[x^2], Assumptions -> Element[x, Reals]],
  Abs[x],
  TestID -> "sqrt-square-real-absolute-value"
]
