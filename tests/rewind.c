#include "cnf.h"
#include "bcp.h"
#include "trail.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Tests for bcp_rwnd_one ──────────────────────────────────────────────

static int test_rwnd_simple_roundtrip(void) {
    printf("=== test_rwnd_simple_roundtrip ===\n");
    // Propagate x1=T on a small formula, snapshot before/after, then
    // rewind and verify state matches the pre-propagation snapshot.
    //
    // Formula: 3 vars, 3 clauses
    //   C0: ( x1 OR  x2)
    //   C1: (-x1 OR  x3)
    //   C2: ( x2 OR -x3)
    int32_t clauses[] = { 1, 2,  -1, 3,  2, -3 };
    size_t lengths[] = { 2, 2, 2 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 3, 3);

    state_snapshot before;
    snapshot_take(&before, &f, &a);
    printf("    before propagation:\n");
    print_state(&f, &a, &q, &t);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    printf("    after propagation:\n");
    print_state(&f, &a, &q, &t);

    // Now rewind variable 1
    bcp_rwnd_one(&f, &a, 1);
    printf("    after rewind:\n");
    print_state(&f, &a, &q, &t);

    int ok = snapshot_compare(&before, &a);
    if (!ok) printf("    FAIL: state did not match snapshot after rewind\n");

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_after_falsification(void) {
    printf("=== test_rwnd_after_falsification ===\n");
    // Propagate x1=F (so -x1 becomes the satisfied literal, +x1 the
    // falsified one). Verify rewind restores correctly. Different code
    // path than _simple because the polarity is reversed.
    int32_t clauses[] = { 1, 2,  -1, 3,  2, -3 };
    size_t lengths[] = { 2, 2, 2 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 3, 3);

    state_snapshot before;
    snapshot_take(&before, &f, &a);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(-1));
    printf("    after propagating -x1:\n");
    print_state(&f, &a, &q, &t);

    bcp_rwnd_one(&f, &a, 1);
    printf("    after rewind:\n");
    print_state(&f, &a, &q, &t);

    int ok = snapshot_compare(&before, &a);
    if (!ok) printf("    FAIL\n");

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_chain_lifo(void) {
    printf("=== test_rwnd_chain_lifo ===\n");
    // Propagate three independent variables in sequence, snapshot at each
    // stage, then rewind them in LIFO order and verify each intermediate
    // snapshot is restored exactly.
    int32_t clauses[] = { 1, 2, 3, 4 };
    size_t lengths[] = { 4 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 1);

    state_snapshot s0; snapshot_take(&s0, &f, &a);
    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    state_snapshot s1; snapshot_take(&s1, &f, &a);
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-2));
    state_snapshot s2; snapshot_take(&s2, &f, &a);
    bcp_prop_one(&f, &a, &q, &t, encode_lit(3));
    printf("    after all 3 propagations:\n");
    print_state(&f, &a, &q, &t);

    int ok = 1;

    // Rewind x3, expect state to match s2
    bcp_rwnd_one(&f, &a, 3);
    if (!snapshot_compare(&s2, &a)) {
        printf("    FAIL after rewind x3\n"); ok = 0;
    }

    // Rewind x2 (which was assigned FALSE), expect state to match s1
    bcp_rwnd_one(&f, &a, 2);
    if (!snapshot_compare(&s1, &a)) {
        printf("    FAIL after rewind x2\n"); ok = 0;
    }

    // Rewind x1, expect state to match s0
    bcp_rwnd_one(&f, &a, 1);
    if (!snapshot_compare(&s0, &a)) {
        printf("    FAIL after rewind x1\n"); ok = 0;
    }

    snapshot_free(&s0); snapshot_free(&s1); snapshot_free(&s2);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_with_propagation_cascade(void) {
    printf("=== test_rwnd_with_propagation_cascade ===\n");
    // bcp_prop_one may push additional units onto the queue. Drain them,
    // creating a cascade of assignments. Then rewind everything and
    // verify the original state is restored. Tests that rewind handles
    // cascade chains via repeated bcp_rwnd_one calls.
    //
    //   C0: (-x1 OR  x2)         after x1=T: unit forcing x2
    //   C1: (-x2 OR  x3)         after x2=T: unit forcing x3
    //   C2: (-x3 OR  x4)         after x3=T: unit forcing x4
    int32_t clauses[] = { -1, 2,  -2, 3,  -3, 4 };
    size_t lengths[] = { 2, 2, 2 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 3);

    state_snapshot before;
    snapshot_take(&before, &f, &a);

    // Run a full BCP starting with x1=T
    bcp_queue_push(&q, encode_lit(1));
    enum bcp_status status = bcp_drain(&f, &a, &q, &t);
    printf("    after cascade (status=%s):\n", status_name(status));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (status != BCP_SAT && status != BCP_UNDETERMINED) {
        printf("    setup FAIL: drain returned %s (expected SAT or UNDETERMINED)\n",
           status_name(status));
        ok = 0;
    }

    // Trail should now contain x1, x2, x3, x4 in order. Rewind them all
    // in LIFO order — that's exactly what trail_unwind(t, 0, f, a) does.
    trail_unwind(&t, 0, &f, &a);
    printf("    after trail_unwind to 0:\n");
    print_state(&f, &a, &q, &t);

    if (!snapshot_compare(&before, &a)) {
        printf("    FAIL: state did not match snapshot after unwind\n");
        ok = 0;
    }
    if (t.top != 0) {
        printf("    FAIL: trail top should be 0, is %zu\n", t.top);
        ok = 0;
    }

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

// ─── Tests for Trail mechanics ───────────────────────────────────────────

static int test_trail_push_pop_basic(void) {
    printf("=== test_trail_push_pop_basic ===\n");
    Trail t;
    trail_init(&t, 10);

    int ok = 1;
    if (t.top != 0) { printf("    FAIL: initial top != 0\n"); ok = 0; }

    trail_push(&t, 5);
    trail_push(&t, 7);
    trail_push(&t, 2);

    if (t.top != 3) { printf("    FAIL: top after 3 pushes != 3\n"); ok = 0; }
    if (t.vars[0] != 5) { printf("    FAIL: vars[0] != 5\n"); ok = 0; }
    if (t.vars[1] != 7) { printf("    FAIL: vars[1] != 7\n"); ok = 0; }
    if (t.vars[2] != 2) { printf("    FAIL: vars[2] != 2\n"); ok = 0; }

    trail_free(&t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_trail_mark_and_partial_unwind(void) {
    printf("=== test_trail_mark_and_partial_unwind ===\n");
    // Push a few vars on a real formula, mark, push more, then unwind
    // to the mark and verify the first batch is still there and the
    // second batch is gone (state-wise too).
    int32_t clauses[] = { 1, 2, 3, 4, 5 };
    size_t lengths[] = { 5 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 5, 1);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(2));

    state_snapshot at_mark;
    snapshot_take(&at_mark, &f, &a);
    size_t mark = trail_mark(&t);
    printf("    after first 2 propagations, mark=%zu, trail top=%zu\n",
           mark, t.top);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(3));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-4));
    printf("    after 2 more propagations, trail top=%zu\n", t.top);
    print_state(&f, &a, &q, &t);

    // Unwind back to mark
    trail_unwind(&t, mark, &f, &a);
    printf("    after unwind to mark, trail top=%zu\n", t.top);
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (t.top != mark) {
        printf("    FAIL: trail top should be %zu, is %zu\n", mark, t.top);
        ok = 0;
    }
    if (!snapshot_compare(&at_mark, &a)) {
        printf("    FAIL: state at mark not restored\n");
        ok = 0;
    }

    snapshot_free(&at_mark);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_trail_unwind_to_zero(void) {
    printf("=== test_trail_unwind_to_zero ===\n");
    // Push several, unwind all the way to 0, verify trail is empty
    // and state is back to initial.
    int32_t clauses[] = { 1, 2, 3, 4 };
    size_t lengths[] = { 4 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 4, 1);

    state_snapshot initial;
    snapshot_take(&initial, &f, &a);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-2));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(3));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(-4));

    trail_unwind(&t, 0, &f, &a);

    int ok = 1;
    if (t.top != 0) { printf("    FAIL: top != 0\n"); ok = 0; }
    if (!snapshot_compare(&initial, &a)) {
        printf("    FAIL: state not restored to initial\n");
        ok = 0;
    }

    snapshot_free(&initial);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_trail_unwind_noop_at_top(void) {
    printf("=== test_trail_unwind_noop_at_top ===\n");
    // Unwinding to the current top should be a no-op (no variables popped).
    int32_t clauses[] = { 1, 2 };
    size_t lengths[] = { 2 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 2, 1);

    bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    bcp_prop_one(&f, &a, &q, &t, encode_lit(2));

    state_snapshot after_pushes;
    snapshot_take(&after_pushes, &f, &a);
    size_t mark = trail_mark(&t);

    trail_unwind(&t, mark, &f, &a);

    int ok = 1;
    if (t.top != mark) { printf("    FAIL: top changed\n"); ok = 0; }
    if (!snapshot_compare(&after_pushes, &a)) {
        printf("    FAIL: state changed\n");
        ok = 0;
    }

    snapshot_free(&after_pushes);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_unit_at_each_k_position(void) {
    printf("=== test_rwnd_unit_at_each_k_position ===\n");
    // Three clauses, each ending up as a unit, with the lone unassigned
    // literal at a different k position in each clause's dense layout.
    // Tests that the k-loop accumulator correctly identifies the
    // unassigned literal regardless of where in the clause it sits.
    //
    // After propagating +x1 (which is in every clause as +x1):
    //   C0: ( x1 OR  x2 OR  x3)   — wait, +x1 SATISFIES C0, not what we want
    //
    // Better: set up so that propagating one variable falsifies a different
    // literal in each clause, leaving exactly one unassigned at varying k.
    //
    //   C0: (-x1 OR  x2 OR -x4)   pre-assign x4=T, then prop +x1 → C0 unit on +x2 at k=1
    //   C1: (-x3 OR -x1 OR  x4)   pre-assign x3=T, prop +x1 → wait this still satisfies via +x4=T
    //
    // Let me redo: pre-assign some vars to FALSE so they falsify their
    // positive occurrences, then propagate a variable that falsifies the
    // remaining negative literal in each clause.
    //
    //   pre: x2=F, x3=F, x5=F
    //   C0: ( x2 OR -x1 OR  x4)   k=0 falsified by x2=F; -x1 falsified by x1=T;
    //                             k=2 (+x4) is the lone unset → unit on +x4
    //   C1: (-x1 OR  x3 OR  x6)   -x1 falsified by x1=T; x3 falsified by x3=F;
    //                             k=2 (+x6) is the lone unset → unit on +x6
    //   C2: (-x1 OR  x5 OR  x7)   similar; k=2 (+x7) is the unset
    //
    // Hmm, this puts every unit at k=2. Let me vary it explicitly.
    //
    //   pre: x2=F, x3=F, x5=F, x6=F
    //   C0: ( x8 OR -x1 OR  x2)   prop +x1 falsifies -x1 (k=1); x2 falsified (k=2);
    //                             unset is +x8 at k=0 → unit on +x8
    //   C1: ( x3 OR  x9 OR -x1)   prop +x1 falsifies -x1 (k=2); x3 falsified (k=0);
    //                             unset is +x9 at k=1 → unit on +x9
    //   C2: ( x5 OR  x6 OR -x1)   prop +x1 falsifies -x1 (k=2); x5, x6 falsified (k=0,1);
    //                             num_unassigned hits 0 → conflict
    //
    // Drop C2 — we want unit detection, not conflict:
    //   C2: (-x1 OR  x5 OR x10)   prop +x1 falsifies -x1 (k=0); x5 falsified (k=1);
    //                             unset is +x10 at k=2 → unit on +x10

    int32_t clauses[] = {
         8, -1,  2,    // C0: unit on +x8 at k=0
         3,  9, -1,    // C1: unit on +x9 at k=1
        -1,  5, 10,    // C2: unit on +x10 at k=2
    };
    size_t lengths[] = { 3, 3, 3 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 10, 3);

    // Pre-assign x2=F, x3=F, x5=F so each clause has exactly one unset
    // non-(-x1) literal at a known k position.
    int32_t preassign_lits[3] = {-2, -3, -5};
    size_t preassign_len = 3;
    preassign(&f, &a, &q, &t, preassign_lits, preassign_len);

    state_snapshot before;
    snapshot_take(&before, &f, &a);
    size_t mark = trail_mark(&t);

    bcp_queue_push(&q, encode_lit(1));
    enum bcp_status s = bcp_drain(&f, &a, &q, &t);
    printf("    after propagating +x1 (status=%s):\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    // After +x1 cascade: should propagate +x8, +x9, +x10 as units, all sat.
    // Trail since mark: x1, x8, x9, x10 (order may vary).
    if (s != BCP_SAT && s != BCP_UNDETERMINED) {
        printf("    setup FAIL: drain returned %s\n", status_name(s));
        ok = 0;
    }

    trail_unwind(&t, mark, &f, &a);
    printf("    after rewind to mark:\n");
    print_state(&f, &a, &q, &t);

    if (!snapshot_compare(&before, &a)) {
        printf("    FAIL: state did not match snapshot after rewind\n");
        ok = 0;
    }

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_full_vector_of_units(void) {
    printf("=== test_rwnd_full_vector_of_units ===\n");
    // Force a single bcp_prop_one call to produce many simultaneous units,
    // exceeding one stripmine iteration if VL is small. This tests:
    //   - vcompress with many set lanes
    //   - queue push with count > 1 per stripe
    //   - rewind correctly reversing a wide swath of counter updates
    //
    // Strategy: variable x1 appears positively in 8 different clauses,
    // each of which has exactly 2 other literals that are pre-falsified.
    // After propagating +x1, all 8 clauses become satisfied (not units —
    // hmm, satisfied means they're not units).
    //
    // Reverse strategy: -x1 appears in 8 clauses, each pre-falsified down
    // to a single unset literal. Propagating +x1 falsifies -x1 in all,
    // making them all units simultaneously.
    //
    // For clause i in 0..7: (-x1 OR -x_{i+2} OR  x_{i+10})
    // Pre-assign x2..x9 = TRUE (so -x_{i+2} is falsified).
    // Propagate +x1 → all 8 clauses become units forcing +x_{10..17}.

    int32_t clauses[] = {
        -1, -2, 10,
        -1, -3, 11,
        -1, -4, 12,
        -1, -5, 13,
        -1, -6, 14,
        -1, -7, 15,
        -1, -8, 16,
        -1, -9, 17,
    };
    size_t lengths[] = { 3, 3, 3, 3, 3, 3, 3, 3 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 17, 8);

    // Pre-assign x2..x9 = T
    int32_t preassign_lits[8] = {2, 3, 4, 5, 6, 7, 8, 9};
    size_t preassign_len = 8;
    preassign(&f, &a, &q, &t, preassign_lits, preassign_len);

    state_snapshot before;
    snapshot_take(&before, &f, &a);
    size_t mark = trail_mark(&t);

    bcp_queue_push(&q, encode_lit(1));
    enum bcp_status s = bcp_drain(&f, &a, &q, &t);
    printf("    after wide unit propagation (status=%s):\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_SAT && s != BCP_UNDETERMINED) {
        printf("    setup FAIL: drain returned %s\n", status_name(s));
        ok = 0;
    }

    // All of x10..x17 should have been forced TRUE by the cascade.
    for (int32_t v = 10; v <= 17; v++) {
        if (a.values[v] != VAR_TRUE) {
            printf("    FAIL: x%d expected TRUE, got %d\n", v, a.values[v]);
            ok = 0;
        }
    }

    trail_unwind(&t, mark, &f, &a);
    printf("    after rewind to mark:\n");
    print_state(&f, &a, &q, &t);

    if (!snapshot_compare(&before, &a)) {
        printf("    FAIL: state did not match snapshot after rewind\n");
        ok = 0;
    }
    // After rewind, x10..x17 should be UNSET again
    for (int32_t v = 10; v <= 17; v++) {
        if (a.values[v] != VAR_UNSET) {
            printf("    FAIL: x%d should be UNSET after rewind, got %d\n", v, a.values[v]);
            ok = 0;
        }
    }

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_partial_unit_partial_sat(void) {
    printf("=== test_rwnd_partial_unit_partial_sat ===\n");
    // Mix of clauses in one stripmine iteration: some become satisfied,
    // some become units, some are unaffected. Tests that masks correctly
    // distinguish these cases and the queue push only includes units.
    //
    //   pre: x4=F
    //   C0: (-x1 OR  x4 OR  x5)   prop +x1: -x1 falsified, x4 falsified,
    //                             unit on +x5
    //   C1: ( x1 OR  x2)          prop +x1: satisfied (sat_mask set, NOT a unit)
    //   C2: (-x1 OR  x6 OR  x7)   prop +x1: -x1 falsified, x6, x7 unset
    //                             num_unassigned=2, NOT a unit
    //   C3: (-x1 OR  x4 OR  x8)   prop +x1: -x1 falsified, x4 falsified,
    //                             unit on +x8

    int32_t clauses[] = {
        -1,  4,  5,    // C0: unit on +x5
         1,  2,        // C1: satisfied (skip via sat_mask)
        -1,  6,  7,    // C2: not a unit (2 unassigned)
        -1,  4,  8,    // C3: unit on +x8
    };
    size_t lengths[] = { 3, 2, 3, 3 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 8, 4);

    bcp_queue_push(&q, encode_lit(-4));
    bcp_drain(&f, &a, &q, &t);

    state_snapshot before;
    snapshot_take(&before, &f, &a);
    size_t mark = trail_mark(&t);

    bcp_queue_push(&q, encode_lit(1));
    enum bcp_status s = bcp_drain(&f, &a, &q, &t);
    printf("    after mixed propagation (status=%s):\n", status_name(s));
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (s != BCP_SAT && s != BCP_UNDETERMINED) {
        printf("    setup FAIL: drain returned %s\n", status_name(s));
        ok = 0;
    }

    // After +x1: x5, x8 should both be forced TRUE; x6, x7 still UNSET
    if (a.values[5] != VAR_TRUE) { printf("    FAIL: x5 not TRUE\n"); ok = 0; }
    if (a.values[8] != VAR_TRUE) { printf("    FAIL: x8 not TRUE\n"); ok = 0; }
    if (a.values[6] != VAR_UNSET) { printf("    FAIL: x6 should be UNSET\n"); ok = 0; }
    if (a.values[7] != VAR_UNSET) { printf("    FAIL: x7 should be UNSET\n"); ok = 0; }

    trail_unwind(&t, mark, &f, &a);
    printf("    after rewind to mark:\n");
    print_state(&f, &a, &q, &t);

    if (!snapshot_compare(&before, &a)) {
        printf("    FAIL: state did not match snapshot after rewind\n");
        ok = 0;
    }

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int test_rwnd_conflict_with_partial_units(void) {
    printf("=== test_rwnd_conflict_with_partial_units ===\n");
    // A propagation that triggers a conflict in one clause while ALSO
    // identifying units in others, all in the same stripe. Tests that:
    //   - conflict_found is set without short-circuiting
    //   - other clauses still get their counter updates
    //   - units identified in non-conflicting clauses still push correctly
    //   - rewind cleanly reverses everything despite the mid-stripe conflict
    //
    //   pre: x2=F, x3=F, x5=F, x7=T (so +x7 satisfies, but -x7 doesn't)
    //   C0: (-x1 OR  x2 OR  x3)   prop +x1: -x1 falsified, x2 false, x3 false,
    //                             num_unassigned=0, num_sat=0 → CONFLICT
    //   C1: (-x1 OR  x5 OR  x8)   prop +x1: -x1 false, x5 false, x8 unset
    //                             → unit on +x8
    //   C2: ( x7 OR -x1)          prop +x1: satisfied via x7=T (skipped via sat_mask)
    //   C3: (-x1 OR  x4 OR  x9)   prop +x1: -x1 false, x4 unset, x9 unset
    //                             num_unassigned=2, NOT a unit

    int32_t clauses[] = {
        -1,  2,  3,    // C0: CONFLICT
        -1,  5,  8,    // C1: unit on +x8
         7, -1,        // C2: already satisfied
        -1,  4,  9,    // C3: not a unit
    };
    size_t lengths[] = { 3, 3, 2, 3 };

    Formula f; Assignment a; bcp_queue q; Trail t;
    setup_state(&f, &a, &q, &t, clauses, lengths, 9, 4);

    // Pre-setup: x2=F, x3=F, x5=F, x7=T
    int32_t preassign_lits[4] = {-2, -3, -5, 7};
    size_t preassign_len = 4;
    preassign(&f, &a, &q, &t, preassign_lits, preassign_len);

    bcp_queue_push(&q, encode_lit(-2));
    bcp_queue_push(&q, encode_lit(-3));
    bcp_queue_push(&q, encode_lit(-5));
    bcp_queue_push(&q, encode_lit(7));
    bcp_drain(&f, &a, &q, &t);

    state_snapshot before;
    snapshot_take(&before, &f, &a);
    size_t mark = trail_mark(&t);

    enum bcp_step_status sps = bcp_prop_one(&f, &a, &q, &t, encode_lit(1));
    printf("    after prop_one(+x1) (returned=%s):\n",
           sps == BCP_STEP_UNSAT ? "UNSAT" : "OK");
    print_state(&f, &a, &q, &t);

    int ok = 1;
    if (sps != BCP_STEP_UNSAT) {
        printf("    FAIL: expected BCP_STEP_UNSAT from conflict\n");
        ok = 0;
    }

    // Despite the conflict, counters for ALL affected clauses should have
    // been updated. Specifically C1 and C3 should have num_unassigned
    // decremented even though we hit conflict on C0.
    // C0: was (0,1) → (0,0)
    // C1: was (0,2) → (0,1)
    // C3: was (0,3) → (0,2)
    if (a.num_unassigned[0] != 0) {
        printf("    FAIL: C0 unassigned should be 0 (conflict), got %d\n",
               a.num_unassigned[0]);
        ok = 0;
    }
    if (a.num_unassigned[1] != 1) {
        printf("    FAIL: C1 unassigned should be 1, got %d\n",
               a.num_unassigned[1]);
        ok = 0;
    }
    if (a.num_unassigned[3] != 2) {
        printf("    FAIL: C3 unassigned should be 2, got %d\n",
               a.num_unassigned[3]);
        ok = 0;
    }

    // Critical: rewind must reverse ALL updates, including those that
    // happened after conflict_found was set.
    trail_unwind(&t, mark, &f, &a);
    printf("    after rewind to mark:\n");
    print_state(&f, &a, &q, &t);

    if (!snapshot_compare(&before, &a)) {
        printf("    FAIL: state did not match snapshot after rewind\n");
        printf("    (this would happen if prop_one early-returned on conflict)\n");
        ok = 0;
    }

    snapshot_free(&before);
    teardown_state(&f, &a, &q, &t);
    printf("    %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(void) {
    int all_ok = 1;
    if (!test_rwnd_simple_roundtrip())              all_ok = 0;
    if (!test_rwnd_after_falsification())           all_ok = 0;
    if (!test_rwnd_chain_lifo())                    all_ok = 0;
    if (!test_rwnd_with_propagation_cascade())      all_ok = 0;
    if (!test_trail_push_pop_basic())               all_ok = 0;
    if (!test_trail_mark_and_partial_unwind())      all_ok = 0;
    if (!test_trail_unwind_to_zero())               all_ok = 0;
    if (!test_trail_unwind_noop_at_top())           all_ok = 0;
    if (!test_rwnd_unit_at_each_k_position())       all_ok = 0;
    if (!test_rwnd_full_vector_of_units())          all_ok = 0;
    if (!test_rwnd_partial_unit_partial_sat())      all_ok = 0;
    if (!test_rwnd_conflict_with_partial_units())   all_ok = 0;

    printf("rewind overall: %s\n", all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}