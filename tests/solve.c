#include "cnf.h"
#include "bcp.h"
#include "trail.h"
#include "solver.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Run solve() on a small hand-crafted formula, check the returned status
// matches expectation, and if SAT, verify the assignment.
static int run_case(const char *label,
                    size_t num_vars, size_t num_clauses,
                    const int32_t *clauses, const size_t *lengths,
                    enum sat_result expected) {
    printf("=== %s ===\n", label);

    Formula f;
    Assignment a;
    build_formula_manual(&f, num_vars, num_clauses, clauses, lengths);
    assignment_init(&a, f.num_vars, f.num_clauses);

    enum sat_result r = solve(&f, &a);
    printf("    result: %s (expected: %s)\n",
           r == SAT ? "SAT" : "UNSAT",
           expected == SAT ? "SAT" : "UNSAT");

    int ok = (r == expected);
    if (!ok) printf("    FAIL: status mismatch\n");

    if (r == SAT) {
        // Print the assignment so failures are easier to diagnose.
        printf("    assignment: ");
        for (size_t v = 1; v <= f.num_vars; v++) {
            printf("x%zu=%s ", v,
                   a.values[v] == VAR_TRUE  ? "T" :
                   a.values[v] == VAR_FALSE ? "F" : "?");
        }
        printf("\n");
        if (!verify_sat(&f, &a)) {
            printf("    FAIL: assignment does not satisfy formula\n");
            ok = 0;
        }
    }

    assignment_free(&a);
    formula_free(&f);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

// ─── Trivial cases ───────────────────────────────────────────────────────

static int test_trivial_sat(void) {
    // Single positive literal in a single clause.
    //   (x1)  -> SAT, x1=T
    int32_t clauses[] = { 1 };
    size_t lengths[] = { 1 };
    return run_case("trivial_sat", 1, 1, clauses, lengths, SAT);
}

static int test_trivial_unsat(void) {
    //   (x1) AND (-x1)  -> UNSAT
    int32_t clauses[] = { 1,  -1 };
    size_t lengths[] = { 1, 1 };
    return run_case("trivial_unsat", 1, 2, clauses, lengths, UNSAT);
}

static int test_two_vars_sat(void) {
    //   (x1 OR x2) AND (-x1 OR x2)  -> SAT (x2=T satisfies both)
    int32_t clauses[] = { 1, 2,  -1, 2 };
    size_t lengths[] = { 2, 2 };
    return run_case("two_vars_sat", 2, 2, clauses, lengths, SAT);
}

static int test_two_vars_unsat(void) {
    //   (x1) AND (x2) AND (-x1 OR -x2)  -> UNSAT
    int32_t clauses[] = { 1,  2,  -1, -2 };
    size_t lengths[] = { 1, 1, 2 };
    return run_case("two_vars_unsat", 2, 3, clauses, lengths, UNSAT);
}

// ─── Forcing backtracking ───────────────────────────────────────────────

static int test_requires_branch_one_side(void) {
    // Solver picks x1 first. If it tries x1=T, the formula becomes UNSAT
    // immediately (because of -x1 unit clause). Must backtrack and try
    // x1=F, which works.
    //
    //   ( x1 OR  x2)
    //   (-x1)              forces x1=F
    //   (x2)               then forces x2=T
    //
    // After backtracking: x1=F, x2=T satisfies all clauses.
    int32_t clauses[] = { 1, 2,  -1,  2 };
    size_t lengths[] = { 2, 1, 1 };
    return run_case("requires_branch_one_side", 2, 3, clauses, lengths, SAT);
}

static int test_deep_chain(void) {
    // Forced chain of length 5. Solver picks x1, tries TRUE, propagates
    // through everything, gets SAT without needing to backtrack.
    //   (x1)
    //   (-x1 OR x2)
    //   (-x2 OR x3)
    //   (-x3 OR x4)
    //   (-x4 OR x5)
    int32_t clauses[] = { 1,  -1, 2,  -2, 3,  -3, 4,  -4, 5 };
    size_t lengths[] = { 1, 2, 2, 2, 2 };
    return run_case("deep_chain", 5, 5, clauses, lengths, SAT);
}

static int test_chain_with_backtrack(void) {
    // Same chain as above, but with a contradiction at the end. Solver
    // must explore both polarities of x1 (and possibly other vars) before
    // declaring UNSAT.
    //   (x1 OR x2)
    //   (-x1 OR x3)
    //   (-x2 OR -x3)
    //   (-x1 OR -x3)
    //   (x2 OR x3)
    int32_t clauses[] = { 1, 2,  -1, 3,  -2, -3,  -1, -3,  2, 3 };
    size_t lengths[] = { 2, 2, 2, 2, 2 };
    // Hand-trace: this should be satisfiable. Let me work through it.
    //   x1=T, x2=anything, x3=T: C0 sat (x1), C1 sat (x3), C2 needs ~x2 or ~x3 -> x2=F. C3 needs ~x1 or ~x3 — both false. UNSAT under x1=T,x3=T.
    //   x1=T, x3=F: C1 needs ~x1 or x3 — both false. UNSAT.
    //   So x1=T fails.
    //   x1=F: C0 needs x2=T. C4 needs x2 or x3 — x2=T sat. C2 needs ~x2 or ~x3 -> x3=F. C3 needs ~x1 (T) or ~x3 — sat.
    //   So x1=F, x2=T, x3=F satisfies. SAT.
    return run_case("chain_with_backtrack", 3, 5, clauses, lengths, SAT);
}

static int test_pigeonhole_2_into_1(void) {
    // 2 pigeons into 1 hole: each pigeon must occupy the hole, but no
    // two pigeons can share. Encoded:
    //   x_ij = pigeon i is in hole j.
    //   For 2 pigeons, 1 hole: x11 + x21 = both must be true (each pigeon
    //     must be somewhere), but -x11 OR -x21 (no sharing).
    //   Clauses:
    //     (x11)          pigeon 1 must be in hole 1
    //     (x21)          pigeon 2 must be in hole 1
    //     (-x11 OR -x21) not both
    //   -> UNSAT
    int32_t clauses[] = { 1,  2,  -1, -2 };
    size_t lengths[] = { 1, 1, 2 };
    return run_case("pigeonhole_2_into_1", 2, 3, clauses, lengths, UNSAT);
}

static int test_pigeonhole_3_into_2(void) {
    // 3 pigeons into 2 holes. Variables:
    //   x1 = p1 in h1, x2 = p1 in h2
    //   x3 = p2 in h1, x4 = p2 in h2
    //   x5 = p3 in h1, x6 = p3 in h2
    // Constraints:
    //   Each pigeon in some hole: (x1 OR x2), (x3 OR x4), (x5 OR x6)
    //   No hole has two pigeons:
    //     hole 1: -x1 OR -x3,  -x1 OR -x5,  -x3 OR -x5
    //     hole 2: -x2 OR -x4,  -x2 OR -x6,  -x4 OR -x6
    //   -> UNSAT
    int32_t clauses[] = {
         1,  2,
         3,  4,
         5,  6,
        -1, -3,
        -1, -5,
        -3, -5,
        -2, -4,
        -2, -6,
        -4, -6,
    };
    size_t lengths[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2 };
    return run_case("pigeonhole_3_into_2", 6, 9, clauses, lengths, UNSAT);
}

// ─── Larger random-ish hand-crafted instances ───────────────────────────

static int test_5var_satisfiable(void) {
    // 5 variables, 7 clauses, satisfiable. Picked so that no unit
    // propagation can solve it from the start — the solver has to branch.
    //
    //   ( x1 OR  x2 OR  x3)
    //   (-x1 OR  x4)
    //   (-x2 OR  x4 OR  x5)
    //   (-x3 OR -x4)
    //   ( x1 OR -x5)
    //   (-x1 OR -x4 OR  x5)
    //   ( x2 OR -x3 OR  x5)
    int32_t clauses[] = {
         1,  2,  3,
        -1,  4,
        -2,  4,  5,
        -3, -4,
         1, -5,
        -1, -4,  5,
         2, -3,  5,
    };
    size_t lengths[] = { 3, 2, 3, 2, 2, 3, 3 };
    return run_case("5var_satisfiable", 5, 7, clauses, lengths, SAT);
}

static int test_5var_unsatisfiable(void) {
    // 5 variables. All 8 possible 3-clause combinations over {x1,x2,x3}
    // forbid each combination → UNSAT on just x1,x2,x3 alone.
    int32_t clauses[] = {
         1,  2,  3,
         1,  2, -3,
         1, -2,  3,
         1, -2, -3,
        -1,  2,  3,
        -1,  2, -3,
        -1, -2,  3,
        -1, -2, -3,
    };
    size_t lengths[] = { 3, 3, 3, 3, 3, 3, 3, 3 };
    return run_case("5var_unsatisfiable", 5, 8, clauses, lengths, UNSAT);
}

// ─── Edge cases ─────────────────────────────────────────────────────────

static int test_empty_formula(void) {
    // Zero clauses, some variables. Vacuously SAT.
    int32_t clauses[1] = {0};      // unused; lengths[] is empty
    size_t lengths[1] = {0};      // unused
    (void)clauses; (void)lengths;

    printf("=== empty_formula ===\n");
    Formula f;
    Assignment a;
    memset(&f, 0, sizeof(f));
    f.num_vars = 3;
    f.num_clauses = 0;
    f.num_lit_occurances = 0;
    f.lit_col = malloc(1 * sizeof(int32_t));        // placeholder, never read
    f.clause_row_off = malloc(1 * sizeof(int32_t));
    f.clause_row_off[0] = 0;
    f.lit_row_off = calloc(2 * f.num_vars + 1, sizeof(int32_t));
    f.clause_col = malloc(1 * sizeof(int32_t));

    assignment_init(&a, f.num_vars, f.num_clauses);

    enum sat_result r = solve(&f, &a);
    printf("    result: %s (expected: SAT)\n", r == SAT ? "SAT" : "UNSAT");

    int ok = (r == SAT);
    if (!ok) printf("    FAIL\n");

    assignment_free(&a);
    formula_free(&f);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_single_var_both_polarities(void) {
    // One variable, two singleton clauses with opposite polarity.
    //   (x1) AND (-x1)  -> UNSAT (same as trivial_unsat, kept for clarity)
    int32_t clauses[] = { 1,  -1 };
    size_t lengths[] = { 1, 1 };
    return run_case("single_var_both_polarities", 1, 2, clauses, lengths, UNSAT);
}

static int test_all_satisfied_initially(void) {
    // Two clauses that are both satisfied if every variable is true.
    // Even before any decisions, the solver should be able to find this.
    //   ( x1 OR  x2)
    //   ( x2 OR  x3)
    int32_t clauses[] = { 1, 2,  2, 3 };
    size_t lengths[] = { 2, 2 };
    return run_case("non_unit_satisfiable", 3, 2, clauses, lengths, SAT);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(void) {
    int all_ok = 1;
    if (!test_trivial_sat())                  all_ok = 0;
    if (!test_trivial_unsat())                all_ok = 0;
    if (!test_two_vars_sat())                 all_ok = 0;
    if (!test_two_vars_unsat())               all_ok = 0;
    if (!test_requires_branch_one_side())     all_ok = 0;
    if (!test_deep_chain())                   all_ok = 0;
    if (!test_chain_with_backtrack())         all_ok = 0;
    if (!test_pigeonhole_2_into_1())          all_ok = 0;
    if (!test_pigeonhole_3_into_2())          all_ok = 0;
    if (!test_5var_satisfiable())             all_ok = 0;
    if (!test_5var_unsatisfiable())           all_ok = 0;
    if (!test_empty_formula())                all_ok = 0;
    if (!test_single_var_both_polarities())   all_ok = 0;
    if (!test_all_satisfied_initially())      all_ok = 0;

    printf("solver overall: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}