(* Development oracle for the canonical complex algebraic tranche.
   The calculator implementation has no Wolfram runtime dependency. *)

VerificationTest[
  MinimalPolynomial[Sqrt[2] + I, x],
  9 - 2 x^2 + x^4,
  TestID -> "resultant-sum-minimal-polynomial"
]

VerificationTest[
  Table[Sign[Im[N[Root[-2 + #^3 &, k], 40]]], {k, 1, 3}],
  {0, -1, 1},
  TestID -> "root-order-real-then-conjugate-pair"
]

VerificationTest[
  Table[RootReduce[Conjugate[Root[-2 + #^3 &, k]]], {k, 1, 3}],
  {Root[-2 + #^3 &, 1], Root[-2 + #^3 &, 3],
   Root[-2 + #^3 &, 2]},
  TestID -> "exact-conjugate-root-identity"
]

VerificationTest[
  RootReduce[(Sqrt[2] + I) + (Sqrt[2] - I)],
  2 Sqrt[2],
  TestID -> "resultant-addition-closure"
]

VerificationTest[
  RootReduce[(Sqrt[2] + I) (Sqrt[2] - I)],
  3,
  TestID -> "resultant-multiplication-closure"
]

VerificationTest[
  CharacteristicPolynomial[{{1, 2}, {3, 4}}, x],
  x^2 - 5 x - 2,
  TestID -> "characteristic-polynomial"
]

VerificationTest[
  Sort[RootReduce /@ Eigenvalues[{{0, 0, 2}, {1, 0, 0}, {0, 1, 0}}]],
  Table[Root[-2 + #^3 &, k], {k, 1, 3}],
  TestID -> "complex-algebraic-companion-eigenvalues"
]

VerificationTest[
  Eigenvalues[{{2, 1}, {0, 2}}],
  {2, 2},
  TestID -> "eigenvalue-algebraic-multiplicity"
]
