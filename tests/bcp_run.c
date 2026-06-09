// bcp_run.c
#include "cnf.h"
#include "bcp.h"
#include "trail.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Test cases ──────────────────────────────────────────────────────────

static int test_run_already_sat(void) {
    printf("=== test_run_already_sat ===\n");
    // 2 vars, 2 clauses, both satisfied by pre-assignments.
    //   C0: ( x1 OR  x2)    pre: x1=T  -> sat
    //   C1: (-x1 OR  x2)    pre: x2=T  -> sat
    int32_t clauses[] = { 1, 2,  -1, 2 };
    size_t lengths[] = { 2, 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 2);

    // Pre-assign x1=T and x2=T.
    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(2));
    bcp_queue_reset(&q);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_SAT);
    if (!ok) printf("    FAIL: expected SAT, got %s\n", status_name(s));

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_already_unsat(void) {
    printf("=== test_run_already_unsat ===\n");
    // 2 vars, 2 clauses. Pre-assign so one clause becomes fully falsified.
    //   C0: ( x1 OR  x2)
    //   C1: (-x1)            after x1=T -> fully falsified
    int32_t clauses[] = { 1, 2,  -1 };
    size_t lengths[] = { 2, 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 2);

    // Pre-assign x1=T. This will make C1 unit during prop_one,
    // and prop_one will return UNSAT itself. To create an "already UNSAT
    // on entry" state for bcp_run, we need to set up the counters such
    // that a clause has (0, 0) without going through prop_one's UNSAT
    // detection. Easiest: use a clause that's already a unit, and
    // pre-assign to falsify the only literal.
    //
    // Replace approach: use C1 = (x1) and pre-assign x1=F.
    // (Restart the test with that formula.)
    teardown_state(&f, &a, &q, &t);

    int32_t clauses2[] = { 1, 2,  1 };
    size_t lengths2[] = { 2, 1 };
    setup_state(&f, &a, &q, &t, clauses2, lengths2, 2, 2);

    // Pre-assign x1=F. This propagates: C0 becomes unit (forces x2),
    // C1 becomes (0, 0) -> UNSAT in prop_one. We need to short-circuit
    // and set state by hand instead.
    //
    // Manual approach: set x1=F directly without prop_one detecting UNSAT.
    a.values[1] = -1;
    a.lit_status[encode_lit(1)]  = 0x00;
    a.lit_status[encode_lit(-1)] = 0x01;
    // Manually update counters to reflect this. Both clauses contain x1.
    //   C0 (x1, x2): x1 false -> num_unassigned-- only.
    //   C1 (x1):     x1 false -> num_unassigned--, becomes (0, 0).
    a.sat_una[0]--;
    a.sat_una[1]--;
    // C1 should now be (0, 0).

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %s\n", status_name(s));

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_no_units(void) {
    printf("=== test_run_no_units ===\n");
    // 3 vars, 2 clauses. No unit clauses, no conflicts. Nothing should
    // happen. Result is UNDETERMINED.
    //   C0: ( x1 OR  x2 OR  x3)
    //   C1: (-x1 OR -x2 OR -x3)
    int32_t clauses[] = { 1, 2, 3,  -1, -2, -3 };
    size_t lengths[] = { 3, 3 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 3, 2);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_UNDETERMINED) {
        printf("    FAIL: expected UNDETERMINED, got %s\n", status_name(s));
        ok = 0;
    }
    // No variable should have been assigned.
    for (size_t v = 1; v <= f.num_vars; v++) {
        if (a.values[v] != 0) {
            printf("    FAIL: x%zu should be UNSET, is %d\n", v, a.values[v]);
            ok = 0;
        }
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_single_unit(void) {
    printf("=== test_run_single_unit ===\n");
    // 2 vars, 2 clauses. One unit, one not.
    //   C0: ( x1)              unit -> force x1
    //   C1: ( x1 OR  x2)       becomes satisfied after x1=T
    // After bcp_run: x1=T, all clauses satisfied -> SAT.
    int32_t clauses[] = { 1,  1, 2 };
    size_t lengths[] = { 1, 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 2);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_SAT) {
        printf("    FAIL: expected SAT, got %s\n", status_name(s));
        ok = 0;
    }
    if (a.values[1] != 1) {
        printf("    FAIL: x1 not TRUE\n"); ok = 0;
    }
    // x2 should still be UNSET — never forced.
    if (a.values[2] != 0) {
        printf("    FAIL: x2 should be UNSET, is %d\n", a.values[2]); ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_chain(void) {
    printf("=== test_run_chain ===\n");
    // 4 vars, 4 clauses. Chain of forced assignments.
    //   C0: ( x1)               unit -> x1=T
    //   C1: (-x1 OR  x2)        becomes unit after x1=T -> x2=T
    //   C2: (-x2 OR  x3)        becomes unit after x2=T -> x3=T
    //   C3: (-x3 OR  x4)        becomes unit after x3=T -> x4=T
    // After bcp_run: all four variables TRUE, all clauses satisfied -> SAT.
    int32_t clauses[] = { 1,  -1, 2,  -2, 3,  -3, 4 };
    size_t lengths[] = { 1, 2, 2, 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 4);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_SAT) {
        printf("    FAIL: expected SAT, got %s\n", status_name(s));
        ok = 0;
    }
    for (size_t v = 1; v <= 4; v++) {
        if (a.values[v] != 1) {
            printf("    FAIL: x%zu expected TRUE, got %d\n", v, a.values[v]);
            ok = 0;
        }
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_chain_to_conflict(void) {
    printf("=== test_run_chain_to_conflict ===\n");
    // 4 vars, 4 clauses. Chain that ends in contradiction.
    //   C0: ( x1)               -> x1=T
    //   C1: (-x1 OR  x2)        -> x2=T
    //   C2: (-x2 OR  x3)        -> x3=T
    //   C3: (-x3)               but this clause forces x3=F. Conflict.
    int32_t clauses[] = { 1,  -1, 2,  -2, 3,  -3 };
    size_t lengths[] = { 1, 2, 2, 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 4);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %s\n", status_name(s));

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_branching_propagation(void) {
    printf("=== test_run_branching_propagation ===\n");
    // 6 vars, 6 clauses. Two independent unit chains.
    //   Chain A: C0 (x1), C1 (-x1 OR x2), C2 (-x2 OR x3)
    //   Chain B: C3 (x4), C4 (-x4 OR x5), C5 (-x5 OR x6)
    // After bcp_run: x1..x6 all TRUE, SAT.
    int32_t clauses[] = {
         1,
        -1, 2,
        -2, 3,
         4,
        -4, 5,
        -5, 6,
    };
    size_t lengths[] = { 1, 2, 2, 1, 2, 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 6, 6);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_SAT) {
        printf("    FAIL: expected SAT, got %s\n", status_name(s));
        ok = 0;
    }
    for (size_t v = 1; v <= 6; v++) {
        if (a.values[v] != 1) {
            printf("    FAIL: x%zu expected TRUE, got %d\n", v, a.values[v]);
            ok = 0;
        }
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_run_unsat_via_simultaneous_units(void) {
    printf("=== test_run_unsat_via_simultaneous_units ===\n");
    // 1 var, 2 clauses. Two unit clauses contradicting each other.
    //   C0: ( x1)
    //   C1: (-x1)
    // bcp_run's initial scan should find both as units, push +x1 and -x1,
    // and the second prop_one call detects the conflict. UNSAT.
    int32_t clauses[] = { 1,  -1 };
    size_t lengths[] = { 1, 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 1, 2);

    enum bcp_status s = bcp_run(&f, &a, &q, &t);
    printf("    status: %s\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %s\n", status_name(s));

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void) {
    int all_ok = 1;
    if (!test_run_already_sat())                   all_ok = 0;
    if (!test_run_already_unsat())                 all_ok = 0;
    if (!test_run_no_units())                      all_ok = 0;
    if (!test_run_single_unit())                   all_ok = 0;
    if (!test_run_chain())                         all_ok = 0;
    if (!test_run_chain_to_conflict())             all_ok = 0;
    if (!test_run_branching_propagation())         all_ok = 0;
    if (!test_run_unsat_via_simultaneous_units())  all_ok = 0;

    printf("bcp_run overall: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}