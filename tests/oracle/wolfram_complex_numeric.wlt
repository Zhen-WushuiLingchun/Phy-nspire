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
