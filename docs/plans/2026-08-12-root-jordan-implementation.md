# Root/Jordan implementation plan

1. Add a CAS-internal exact algebraic-expression bridge and hook it into
   function rebuilding, addition, multiplication, integer powers, conjugation,
   and zero/equivalence decisions.
2. Add C linear-algebra APIs for eigenspaces, generalized eigenspaces,
   eigenvectors, and Jordan decomposition.  Verify every Jordan result by
   exact matrix equality.
3. Register the reader heads, evaluator dispatch, notebook command palette,
   source completion list, tour cells, and documentation.
4. Add unit/evaluator/persistence tests and a Wolfram `.wlt` oracle corpus.
5. Run Release, full CTest, ASan/UBSan/leak, and ARM probes; then commit and
   push.
6. Recheck the built artifacts and device state, atomically upload the program
   and tour, remove only obsolete upload/previous artifacts, and compare
   readback hashes.
