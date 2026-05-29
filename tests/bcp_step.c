// bcp_step.c
#include "cnf.h"
#include "bcp.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Test cases ──────────────────────────────────────────────────────────

static int test_simple_satisfy(void) {
    printf("=== test_simple_satisfy ===\n");
    // 2 vars, 1 clause: (x1 OR x2)
    // Assign x1=T. Clause becomes satisfied. No new units.
    int32_t clauses[]  = { 1, 2 };
    size_t lengths[]  = { 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 1);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }
    if (a.values[1] != 1) { printf("    FAIL: x1 not assigned TRUE\n"); ok = 0; }
    if (a.num_satisfied[0] != 1) { printf("    FAIL: C0 sat count != 1\n"); ok = 0; }
    if (a.num_unassigned[0] != 1) { printf("    FAIL: C0 unassigned count != 1\n"); ok = 0; }
    if (q.tail != q.head) { printf("    FAIL: queue should be empty\n"); ok = 0; }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_falsify_only(void) {
    printf("=== test_falsify_only ===\n");
    // 2 vars, 1 clause: (-x1 OR x2)
    // Assign x1=T. Literal -x1 becomes false; clause has x2 unassigned.
    // num_satisfied=0, num_unassigned=1 → unit clause! x2 should be enqueued.
    int32_t clauses[]  = { -1, 2 };
    size_t lengths[]  = { 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 1);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }
    if (a.num_satisfied[0] != 0) { printf("    FAIL: C0 sat count != 0\n"); ok = 0; }
    if (a.num_unassigned[0] != 1) { printf("    FAIL: C0 unassigned count != 1\n"); ok = 0; }
    if (q.tail - q.head != 1) {
        printf("    FAIL: expected 1 unit in queue, got %zu\n", q.tail - q.head);
        ok = 0;
    } else if (q.buf[q.head] != encode_lit(2)) {
        printf("    FAIL: expected +x2 in queue, got %d\n",
               decode_lit(q.buf[q.head]));
        ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_immediate_conflict(void) {
    printf("=== test_immediate_conflict ===\n");
    // 1 var, 1 clause: (-x1)
    // Assign x1=T. The clause's only literal becomes false → conflict.
    int32_t clauses[] = { -1 };
    size_t lengths[] = { 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 1, 1);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_STEP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %d\n", s);

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_already_assigned_match(void) {
    printf("=== test_already_assigned_match ===\n");
    // 1 var, 1 clause: (x1)
    // Pre-assign x1=T, then call propagate with +x1. Should be a no-op.
    int32_t clauses[] = { 1 };
    size_t lengths[] = { 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 1, 1);

    // Manually assign x1=T first
    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    int32_t pre_sat = a.num_satisfied[0];
    int32_t pre_unassigned = a.num_unassigned[0];

    // Reset queue, then re-apply same literal — should be a no-op.
    bcp_queue_reset(&q);
    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }
    if (a.num_satisfied[0] != pre_sat) {
        printf("    FAIL: sat count changed (%d -> %d)\n", pre_sat, a.num_satisfied[0]);
        ok = 0;
    }
    if (a.num_unassigned[0] != pre_unassigned) {
        printf("    FAIL: unassigned count changed (%d -> %d)\n",
               pre_unassigned, a.num_unassigned[0]);
        ok = 0;
    }
    if (q.tail != q.head) { printf("    FAIL: queue should be empty\n"); ok = 0; }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_already_assigned_conflict(void) {
    printf("=== test_already_assigned_conflict ===\n");
    // 1 var, 1 clause: (x1)
    // Pre-assign x1=T, then try to propagate -x1. Should return UNSAT.
    int32_t clauses[] = { 1 };
    size_t lengths[] = { 1 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 1, 1);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    bcp_queue_reset(&q);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(-1));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_STEP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %d\n", s);

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_multiple_units_discovered(void) {
    printf("=== test_multiple_units_discovered ===\n");
    // 4 vars, 3 clauses:
    //   C0: (-x1 OR x2)         after x1=T, this becomes a unit forcing x2
    //   C1: (-x1 OR x3)         after x1=T, this becomes a unit forcing x3
    //   C2: (-x1 OR x4 OR x2)   after x1=T, becomes 2-literal, NOT unit
    int32_t clauses[]  = { -1, 2,  -1, 3,  -1, 4, 2 };
    size_t lengths[]  = {  2,      2,      3       };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 3);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }

    // Both x2 and x3 should be in the queue. Order doesn't matter — accept
    // either ordering by collecting and checking the set.
    int saw_x2 = 0, saw_x3 = 0;
    size_t qlen = q.tail - q.head;
    if (qlen != 2) {
        printf("    FAIL: expected 2 units in queue, got %zu\n", qlen);
        ok = 0;
    } else {
        for (size_t i = q.head; i < q.tail; i++) {
            int32_t d = decode_lit(q.buf[i]);
            if (d == 2) saw_x2 = 1;
            else if (d == 3) saw_x3 = 1;
            else { printf("    FAIL: unexpected literal %d in queue\n", d); ok = 0; }
        }
        if (!saw_x2) { printf("    FAIL: x2 not in queue\n"); ok = 0; }
        if (!saw_x3) { printf("    FAIL: x3 not in queue\n"); ok = 0; }
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_no_change_on_unrelated_clause(void) {
    printf("=== test_no_change_on_unrelated_clause ===\n");
    // 3 vars, 2 clauses:
    //   C0: (x1 OR x2)
    //   C1: (x2 OR x3)     does NOT contain x1 or -x1
    // Assigning x1=T should affect C0 but leave C1 unchanged.
    int32_t clauses[] = { 1, 2,  2, 3 };
    size_t lengths[] = { 2,     2    };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 3, 2);

    int32_t c1_sat_before = a.num_satisfied[1];
    int32_t c1_un_before = a.num_unassigned[1];

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (a.num_satisfied[1] != c1_sat_before) {
        printf("    FAIL: C1 sat changed (%d -> %d)\n",
               c1_sat_before, a.num_satisfied[1]);
        ok = 0;
    }
    if (a.num_unassigned[1] != c1_un_before) {
        printf("    FAIL: C1 unassigned changed (%d -> %d)\n",
               c1_un_before, a.num_unassigned[1]);
        ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_chain_unit_via_negation(void) {
    printf("=== test_chain_unit_via_negation ===\n");
    // 5 vars, 4 clauses. Assigning x1=T falsifies -x1 in three clauses,
    // each of which has exactly one other literal that is unassigned.
    // Expected queue after one step: [x2, x3, x4] in some order.
    //
    //   C0: (-x1 OR x2)         -> unit forcing x2
    //   C1: (-x1 OR x3)         -> unit forcing x3
    //   C2: (-x1 OR x4)         -> unit forcing x4
    //   C3: (x5 OR x2 OR x3)    -> not touched by x1
    int32_t clauses[] = {
        -1, 2,
        -1, 3,
        -1, 4,
         5, 2, 3,
    };
    size_t lengths[] = { 2, 2, 2, 3 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 5, 4);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }

    // Expect exactly 3 units: x2, x3, x4 (any order).
    int saw[6] = {0};
    size_t qlen = q.tail - q.head;
    if (qlen != 3) {
        printf("    FAIL: expected 3 units, got %zu\n", qlen);
        ok = 0;
    }
    for (size_t i = q.head; i < q.tail; i++) {
        int32_t d = decode_lit(q.buf[i]);
        if (d >= 1 && d <= 5) saw[d]++;
        else { printf("    FAIL: unexpected lit %d\n", d); ok = 0; }
    }
    if (!saw[2] || !saw[3] || !saw[4]) {
        printf("    FAIL: expected x2, x3, x4 (saw %d %d %d)\n",
               saw[2], saw[3], saw[4]);
        ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_mixed_satisfy_and_unit(void) {
    printf("=== test_mixed_satisfy_and_unit ===\n");
    // 5 vars, 4 clauses. Assigning x1=T:
    //   C0: (x1 OR x2)            satisfied (x1 true) -> NOT enqueued
    //   C1: (-x1 OR x3)           unit forcing x3     -> enqueue x3
    //   C2: (x1 OR -x4 OR x5)     satisfied (x1 true) -> NOT enqueued
    //   C3: (-x1 OR -x5)          unit forcing -x5    -> enqueue -x5
    int32_t clauses[] = {
         1,  2,
        -1,  3,
         1, -4,  5,
        -1, -5,
    };
    size_t lengths[] = { 2, 2, 3, 2 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 5, 4);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }

    size_t qlen = q.tail - q.head;
    if (qlen != 2) {
        printf("    FAIL: expected 2 units, got %zu\n", qlen);
        ok = 0;
    }
    int saw_x3 = 0, saw_neg_x5 = 0;
    for (size_t i = q.head; i < q.tail; i++) {
        int32_t d = decode_lit(q.buf[i]);
        if      (d ==  3) saw_x3 = 1;
        else if (d == -5) saw_neg_x5 = 1;
        else { printf("    FAIL: unexpected lit %d\n", d); ok = 0; }
    }
    if (!saw_x3)     { printf("    FAIL: missing x3\n");     ok = 0; }
    if (!saw_neg_x5) { printf("    FAIL: missing -x5\n");    ok = 0; }

    // Counter spot-checks for the satisfied clauses:
    //   C0 (x1, x2): x1 became true (sat++); x2 still unassigned. Expect (1, 1).
    //   C2 (x1, -x4, x5): x1 became true. -x4 and x5 unassigned. Expect (1, 2).
    if (a.num_satisfied[0] != 1 || a.num_unassigned[0] != 1) {
        printf("    FAIL: C0 expected (1,1), got (%d,%d)\n",
               a.num_satisfied[0], a.num_unassigned[0]); ok = 0;
    }
    if (a.num_satisfied[2] != 1 || a.num_unassigned[2] != 2) {
        printf("    FAIL: C2 expected (1,2), got (%d,%d)\n",
               a.num_satisfied[2], a.num_unassigned[2]); ok = 0;
    }
    // Falsification clauses:
    //   C1 (-x1, x3): -x1 became false. x3 still unassigned. Expect (0, 1).
    //   C3 (-x1, -x5): -x1 became false. -x5 still unassigned. Expect (0, 1).
    if (a.num_satisfied[1] != 0 || a.num_unassigned[1] != 1) {
        printf("    FAIL: C1 expected (0,1), got (%d,%d)\n",
               a.num_satisfied[1], a.num_unassigned[1]); ok = 0;
    }
    if (a.num_satisfied[3] != 0 || a.num_unassigned[3] != 1) {
        printf("    FAIL: C3 expected (0,1), got (%d,%d)\n",
               a.num_satisfied[3], a.num_unassigned[3]); ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_unit_with_partially_assigned(void) {
    printf("=== test_unit_with_partially_assigned ===\n");
    // 5 vars, 1 clause. Pre-assign x2=F and x3=F manually, then propagate
    // x1=T which falsifies -x1. The clause (-x1 OR x2 OR x3 OR x4) now has:
    //   -x1 false, x2 false, x3 false, x4 unassigned -> unit forcing x4.
    // This stresses the "find the lone unassigned literal" search when
    // most of the clause is already evaluated.
    int32_t clauses[] = { -1, 2, 3, 4 };
    size_t lengths[] = { 4 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 5, 1);

    // Pre-assign x2=F and x3=F via two propagations of the negative literals.
    // (We're using bcp_prop_one to set up state, since it's the only path
    // that correctly updates lit_status and counters.)
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-2));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-3));

    // After those, C0's counters should be (0, 2) since two literals are
    // now falsified (-x1 still unassigned, x4 still unassigned, x2 and x3
    // false).
    // Wait — re-check: clause is (-x1, x2, x3, x4). After x2=F and x3=F,
    // -x1 is unassigned, x2 is false (literal +x2 false → contributes 0),
    // x3 is false (+x3 false), x4 unassigned. So num_satisfied=0,
    // num_unassigned=2 (the -x1 and x4 literals).
    if (a.num_satisfied[0] != 0 || a.num_unassigned[0] != 2) {
        printf("    setup FAIL: expected C0 (0,2), got (%d,%d)\n",
               a.num_satisfied[0], a.num_unassigned[0]);
    }

    // Reset queue, then propagate x1=T. This falsifies -x1, leaving x4
    // as the lone unassigned literal -> queue should contain x4.
    bcp_queue_reset(&q);
    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }
    size_t qlen = q.tail - q.head;
    if (qlen != 1) {
        printf("    FAIL: expected 1 unit, got %zu\n", qlen);
        ok = 0;
    } else {
        int32_t d = decode_lit(q.buf[q.head]);
        if (d != 4) {
            printf("    FAIL: expected x4, got %d\n", d);
            ok = 0;
        }
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_conflict_after_propagation(void) {
    printf("=== test_conflict_after_propagation ===\n");
    // 5 vars, 2 clauses. Set things up so propagating x1=T makes one
    // clause go to (0, 0) — a conflict.
    //   C0: (-x1 OR x2)           after x1=T, unit forces x2
    //   C1: (-x1 OR -x2)          after x1=T, falsifies -x1, then -x2
    //                             becomes only literal. But x2 is still
    //                             UNSET when we propagate x1, so C1's
    //                             state after x1=T is (0, 1) — it will
    //                             enqueue -x2. NOT a conflict yet.
    //
    // To actually force a conflict in one step, we need the clause to
    // already be falsified except for -x1. So pre-assign so that all
    // other literals are false.
    //
    //   Clause: (-x1 OR x2 OR x3)
    //   Pre-assign x2=F (literal x2 false), x3=F (literal x3 false).
    //   Now propagate x1=T. -x1 becomes false.
    //   Clause counters become (0, 0) -> UNSAT.
    int32_t clauses[] = { -1, 2, 3 };
    size_t lengths[] = { 3 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 5, 1);

    // Pre-assign x2=F and x3=F.
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-2));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-3));
    bcp_queue_reset(&q);

    // Now C0 should have num_satisfied=0, num_unassigned=1 (only -x1 left).
    // Propagating x1=T falsifies -x1 -> num_unassigned=0 -> UNSAT.
    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = (s == BCP_STEP_UNSAT);
    if (!ok) printf("    FAIL: expected UNSAT, got %d\n", s);

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_cascading_propagation(void) {
    printf("=== test_cascading_propagation ===\n");
    // 9 variables, 7 clauses. Designed so that propagating x1=T
    // simultaneously affects 4 clauses (some via +x1, some via -x1):
    //
    //   C0: ( x1 OR  x2 OR  x3)         x1 satisfies         -> NO unit
    //   C1: (-x1 OR  x4)                -x1 false, x4 unset  -> unit, push +x4
    //   C2: ( x1 OR -x5 OR  x6)         x1 satisfies         -> NO unit
    //   C3: (-x1 OR -x6 OR  x7)         -x1 false, both unset-> NO unit (2 unassigned)
    //   C4: (-x1 OR  x8)                -x1 false, x8 unset  -> unit, push +x8
    //   C5: ( x2 OR  x5 OR  x9)         no x1                -> unchanged
    //   C6: (-x4 OR -x8 OR  x9)         no x1                -> unchanged
    //
    // Expected after one bcp_prop_one(+x1):
    //   - x1 = TRUE
    //   - Queue contains exactly {+x4, +x8}
    //   - C0: (1, 2)   x1 satisfies, x2 and x3 unassigned
    //   - C1: (0, 1)   -x1 false, x4 unassigned
    //   - C2: (1, 2)   x1 satisfies, -x5 and x6 unassigned
    //   - C3: (0, 2)   -x1 false, -x6 and x7 unassigned
    //   - C4: (0, 1)   -x1 false, x8 unassigned
    //   - C5: (0, 3)   unchanged from init
    //   - C6: (0, 3)   unchanged from init
    int32_t clauses[] = {
         1,  2,  3,
        -1,  4,
         1, -5,  6,
        -1, -6,  7,
        -1,  8,
         2,  5,  9,
        -4, -8,  9,
    };
    size_t lengths[] = { 3, 2, 3, 3, 2, 3, 3 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 9, 7);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }

    // Per-clause counter expectations
    struct { int sat, unassigned; } expected[] = {
        {1, 2}, {0, 1}, {1, 2}, {0, 2}, {0, 1}, {0, 3}, {0, 3}
    };
    for (size_t c = 0; c < f.num_clauses; c++) {
        if (a.num_satisfied[c] != expected[c].sat ||
            a.num_unassigned[c] != expected[c].unassigned) {
            printf("    FAIL: C%zu expected (%d,%d), got (%d,%d)\n",
                   c, expected[c].sat, expected[c].unassigned,
                   a.num_satisfied[c], a.num_unassigned[c]);
            ok = 0;
        }
    }

    // Queue: exactly +x4 and +x8 (any order)
    size_t qlen = q.tail - q.head;
    if (qlen != 2) {
        printf("    FAIL: expected 2 in queue, got %zu\n", qlen);
        ok = 0;
    }
    int saw_x4 = 0, saw_x8 = 0;
    for (size_t i = q.head; i < q.tail; i++) {
        int32_t d = decode_lit(q.buf[i]);
        if (d == 4)      saw_x4 = 1;
        else if (d == 8) saw_x8 = 1;
        else { printf("    FAIL: unexpected lit %d in queue\n", d); ok = 0; }
    }
    if (!saw_x4) { printf("    FAIL: missing +x4\n"); ok = 0; }
    if (!saw_x8) { printf("    FAIL: missing +x8\n"); ok = 0; }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_dense_with_partial_setup(void) {
    printf("=== test_dense_with_partial_setup ===\n");
    // 9 variables, 6 clauses. Pre-assign several variables, then propagate
    // one more and check that everything settles correctly. Stresses the
    // interaction between many existing assignments and a new propagation.
    //
    // Clauses:
    //   C0: ( x1 OR  x2 OR  x3 OR  x4)
    //   C1: (-x1 OR -x2 OR  x5)
    //   C2: ( x3 OR  x6 OR -x7)
    //   C3: (-x4 OR  x8 OR -x9)
    //   C4: ( x2 OR -x5 OR  x6 OR  x7)
    //   C5: (-x3 OR -x6 OR  x8 OR  x9)
    //
    // Pre-assign: x2=F, x3=F, x6=F, x7=T  (four pre-assignments)
    //   After these, we expect (working through each clause):
    //     C0 ( x1, x2=F, x3=F, x4): (0, 2)
    //     C1 (-x1, -x2=T, x5):     (1, 2)    -x2 satisfies it
    //     C2 ( x3=F, x6=F, -x7=F): (0, 0) <-- already UNSAT!
    //
    //   Wait — that means the pre-assignment chain itself triggers UNSAT.
    //   That's not what we want for this test. Adjust: change x7=T to x7=F.
    //
    // Re-pre-assign: x2=F, x3=F, x6=F, x7=F
    //   C0 ( x1, x2=F, x3=F, x4):       (0, 2)
    //   C1 (-x1, -x2=T, x5):            (1, 2)   sat by -x2
    //   C2 ( x3=F, x6=F, -x7=T):        (1, 0)   sat by -x7
    //   C3 (-x4, x8, -x9):              (0, 3)   untouched
    //   C4 ( x2=F, -x5, x6=F, x7=F):    (0, 1)   only -x5 unassigned -> UNIT
    //
    //   So even before our final propagation, x7=F propagation should have
    //   queued -x5 already. That's expected; we only check final state.
    //
    // Now propagate x4=T:
    //   C0 ( x1, x2=F, x3=F, +x4):  +x4 satisfies   (1, 1)
    //   C3 (-x4, x8, -x9):          -x4 falsified   (0, 2)
    //
    // Final state expectations after x4=T (assuming all cascading
    // propagations from x7=F have NOT happened — we only call prop_one
    // once for x4=T after the pre-setup, queue starts empty):
    //   C0: (1, 1)        x4 just satisfied; x1 still unassigned
    //   C1: (1, 2)        unchanged
    //   C2: (1, 0)        unchanged
    //   C3: (0, 2)        -x4 just falsified; x8, -x9 unassigned
    //   C4: (0, 1)        unchanged (still unit on -x5 but already in queue
    //                     unless we reset)
    //   C5: (0, 4)        unchanged
    //
    // Queue after final prop_one(+x4): empty (no NEW units from x4=T's step).
    //   C0 (1,1) is satisfied so not unit.
    //   C3 (0,2) has 2 unassigned, not unit.
    int32_t clauses[] = {
         1,  2,  3,  4,
        -1, -2,  5,
         3,  6, -7,
        -4,  8, -9,
         2, -5,  6,  7,
        -3, -6,  8,  9,
    };
    size_t lengths[] = { 4, 3, 3, 3, 4, 4 };
    Formula f;
    Assignment a;
    bcp_queue q;
    Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 9, 6);

    // Pre-assignments via prop_one
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-2));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-3));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-6));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-7));

    // Show intermediate state for sanity
    printf("    after pre-setup:\n");
    print_state(&f, &a, &q, &t);

    // Reset queue so we only see what x4=T produces
    bcp_queue_reset(&q);

    enum bcp_step_status s = bcp_prop_one(&f, &a, &q, &t, encode_lit(4));
    printf("    after propagating +x4:\n");
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_STEP_OK) { printf("    FAIL: status not OK\n"); ok = 0; }

    // Variable assignments
    if (a.values[2] != VAR_FALSE) { printf("    FAIL: x2 not FALSE\n"); ok = 0; }
    if (a.values[3] != VAR_FALSE) { printf("    FAIL: x3 not FALSE\n"); ok = 0; }
    if (a.values[4] != VAR_TRUE)  { printf("    FAIL: x4 not TRUE\n");  ok = 0; }
    if (a.values[6] != VAR_FALSE) { printf("    FAIL: x6 not FALSE\n"); ok = 0; }
    if (a.values[7] != VAR_FALSE) { printf("    FAIL: x7 not FALSE\n"); ok = 0; }

    // Per-clause counters
    struct { int sat, unassigned; } expected[] = {
        {1, 1},   // C0
        {1, 2},   // C1
        {1, 0},   // C2
        {0, 2},   // C3
        {0, 1},   // C4
        {2, 2},   // C5
    };
    for (size_t c = 0; c < f.num_clauses; c++) {
        if (a.num_satisfied[c] != expected[c].sat ||
            a.num_unassigned[c] != expected[c].unassigned) {
            printf("    FAIL: C%zu expected (%d,%d), got (%d,%d)\n",
                   c, expected[c].sat, expected[c].unassigned,
                   a.num_satisfied[c], a.num_unassigned[c]);
            ok = 0;
        }
    }

    // The +x4 propagation alone shouldn't queue anything new:
    // C0 becomes (1,1) — satisfied, not unit.
    // C3 becomes (0,2) — 2 unassigned, not unit.
    size_t qlen = q.tail - q.head;
    if (qlen != 0) {
        printf("    FAIL: expected empty queue from +x4 step, got %zu items\n", qlen);
        for (size_t i = q.head; i < q.tail; i++) {
            printf("      queue has %d\n", decode_lit(q.buf[i]));
        }
        ok = 0;
    }

    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void) {
    int all_ok = 1;
    if (!test_simple_satisfy())                 all_ok = 0;
    if (!test_falsify_only())                   all_ok = 0;
    if (!test_immediate_conflict())             all_ok = 0;
    if (!test_already_assigned_match())         all_ok = 0;
    if (!test_already_assigned_conflict())      all_ok = 0;
    if (!test_multiple_units_discovered())      all_ok = 0;
    if (!test_no_change_on_unrelated_clause())  all_ok = 0;
    if (!test_chain_unit_via_negation())        all_ok = 0;
    if (!test_mixed_satisfy_and_unit())         all_ok = 0;
    if (!test_unit_with_partially_assigned())   all_ok = 0;
    if (!test_conflict_after_propagation())     all_ok = 0;
    if (!test_cascading_propagation())          all_ok = 0;
    if (!test_dense_with_partial_setup())       all_ok = 0;

    printf("bcp_step overall: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}