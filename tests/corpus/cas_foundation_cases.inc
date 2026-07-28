/*
 * Reader-facing F4/F5 capability matrix.
 *
 * Include with:
 *   PHY_FOUNDATION_CASE(id, area, source, expected_status, semantic_class)
 *
 * This matrix pins reader admission. Once a head is admitted, individual
 * semantic subclasses may still return a typed evaluator error; those
 * positive and negative controls live in test_eval.c.
 */
PHY_FOUNDATION_CASE(
    series_polynomial, "Series", "Series[(1+x)^5,{x,0,3}]",
    PHY_OK, "truncated_exact_series")
PHY_FOUNDATION_CASE(
    series_laurent, "Series", "Series[1/(x^2(1+x)),{x,0,4}]",
    PHY_OK, "truncated_exact_laurent_series")
PHY_FOUNDATION_CASE(
    series_analytic, "Series", "Series[Exp[x] Sin[x],{x,0,7}]",
    PHY_OK, "composed_analytic_series")
PHY_FOUNDATION_CASE(
    limit_removable, "Limit", "Limit[(x^2-1)/(x-1),{x,1}]",
    PHY_OK, "finite_exact_limit")
PHY_FOUNDATION_CASE(
    limit_one_sided_pole, "Limit",
    "Limit[1/x,{x,0,FromAbove}]", PHY_OK,
    "directed_infinite_limit")
PHY_FOUNDATION_CASE(
    limit_infinity, "Limit", "Limit[(3x^4+1)/(2x^4-x),{x,Infinity}]",
    PHY_OK, "rational_infinity_limit")
PHY_FOUNDATION_CASE(
    solve_linear, "Solve", "Solve[3x-2==0,x]",
    PHY_OK, "exact_rational_solution")
PHY_FOUNDATION_CASE(
    solve_algebraic, "Solve", "Solve[x^5-x-1==0,x]",
    PHY_OK, "certified_algebraic_roots")
PHY_FOUNDATION_CASE(
    solve_denominator_exclusion, "Solve",
    "Solve[(x^2-1)/(x-1)==0,x]", PHY_OK,
    "conditional_exact_solution")
PHY_FOUNDATION_CASE(
    nsolve_numeric_layer, "NSolve", "NSolve[x^5-x-1==0,x]",
    PHY_ERR_UNSUPPORTED, "future_ball_arithmetic")
PHY_FOUNDATION_CASE(
    reduce_logic, "Reduce", "Reduce[x^2-1==0,x]",
    PHY_ERR_UNSUPPORTED, "future_semialgebraic_logic")
