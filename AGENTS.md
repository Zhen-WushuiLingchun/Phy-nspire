# Phy-nspire agent contract

This file applies to the entire repository.

## Mathematical evidence

- Preserve the separation between exact symbolic IR/CAS, certified interval
  numerics, and optional development oracles.  A floating approximation must
  never participate in exact zero/equality decisions.
- Mathematica/Wolfram Language is an independent development oracle, not a
  runtime dependency and not a substitute for a proof in the native kernel.
- For changes to algebra, calculus, special functions, algebraic numbers,
  tensors, GR, or QFT, try the Wolfram MCP `WolframLanguageContext` tool first
  and then use `WolframLanguageEvaluator` in one reused session.  If the
  context tool has an internal server failure, record that failure and proceed
  only after the attempt; do not pretend semantic context was retrieved.
- Prefer exact oracle operations such as `FullSimplify` with explicit
  assumptions, `Together`, `Cancel`, `D`, `Integrate`, `FunctionDomain`,
  `Reduce`, `RootReduce`, `MinimalPolynomial`, and exact `Solve`.  State all
  real/positive/integer and branch assumptions in the query.
- Principal branches must be reviewed explicitly.  In particular, do not use
  `Log[Exp[z]] == z`, `Sqrt[z^2] == z`, or inverse-function cancellation
  outside a domain on which Wolfram and the native contract prove it.
- High-precision Wolfram values may generate or cross-check golden intervals,
  but the native implementation must still construct an outward-rounded
  enclosure and pass residual/domain checks.  Decimal agreement alone is not
  certification.
- Keep durable oracle expressions and expected results under `tests/oracle/`
  or `research/oracle/`.  Host tests must remain reproducible without Wolfram.
- The current branch oracle is
  `tests/oracle/wolfram_complex_numeric.wlt`; run it through the Wolfram MCP
  `TestReport` tool after branch/special-function changes. On 2026-08-10 the
  semantic-context tool failed internally twice, while evaluator sessions and
  `TestReport` were usable. Preserve that evidence boundary rather than
  skipping the context attempt or treating its failure as an evaluator failure.

## Resource and acceptance boundaries

- Every public numeric operation is bounded, transactional on failure, and
  covered by allocation-failure tests.  Unsupported domains return a typed
  error instead of an unevaluated result that looks successful.
- After mathematical-kernel changes run the focused unit tests, the complete
  Release suite, ASan/UBSan/leak suite, and the relevant clean Ndless ARM link
  probes.  Host/link evidence is not physical calculator acceptance.
- Do not upload to a calculator unless the user explicitly requests that gate;
  recheck the current artifact, USB enumeration, remote target, and readback
  hash before any device write.
