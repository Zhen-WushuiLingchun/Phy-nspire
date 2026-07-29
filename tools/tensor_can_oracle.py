#!/usr/bin/env python3
"""Reproduce the checked SymPy tensor_can fixtures used by the C tests.

This script is an external oracle, not part of the calculator build.  Keep its
inputs literal: changing them changes the mathematical fixture and must be
reviewed together with tests/test_abstract_tensor.c.
"""

from __future__ import annotations

import json

import sympy
from sympy.combinatorics import Permutation
from sympy.combinatorics.tensor_can import canonicalize, get_symmetric_group_sgs


def main() -> None:
    base_2a, gens_2a = get_symmetric_group_sgs(2, 1)
    a_type = (base_2a, gens_2a, 1, 0)
    b_type = (base_2a, gens_2a, 2, 0)
    encoded = [1, 3, 0, 5, 4, 2, 6, 7]
    actual = canonicalize(
        Permutation(encoded), range(6), 0, a_type, b_type
    )
    if actual != 0:
        raise SystemExit(f"SymPy tensor_can oracle drifted: {actual!r}")
    print(
        json.dumps(
            {
                "oracle": "sympy.combinatorics.tensor_can.canonicalize",
                "sympy_version": sympy.__version__,
                "case": "antisymmetric_A_B_commuting_zero",
                "g": encoded,
                "dummies": list(range(6)),
                "metric_symmetry": 0,
                "canonical": 0,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
