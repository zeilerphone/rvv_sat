// solver.c
#include "solver.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

// Counters populated by bcp_rvv.c (zero in scalar builds — declared weak-style
// via extern so the scalar build links without them).
extern uint64_t g_outer_true  __attribute__((weak));
extern uint64_t g_outer_false __attribute__((weak));
extern uint64_t g_kloop_total __attribute__((weak));
extern uint64_t g_prop_calls  __attribute__((weak));
extern uint64_t g_kloop_true   __attribute__((weak));
extern uint64_t g_kloop_false  __attribute__((weak));
extern uint64_t g_kenter_false __attribute__((weak));

static uint64_t g_bcp_instret      = 0;
static uint64_t g_decision_instret = 0;
static uint64_t g_backtrack_instret = 0;

int32_t pick_unassigned(const Formula *f, const Assignment *a){
    for (size_t v = 1; v <= f->num_vars; v++) {
        if (a->values[v] == VAR_UNSET) return (int32_t)v;
    }
    return -1;   // all variables assigned
}

static enum sat_result dpll(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){
    uint64_t _i0;

    // pick new variable and mark it on the trail
    _i0 = read_instret();
    int32_t v = pick_unassigned(f, a);
    g_decision_instret += read_instret() - _i0;
    assert(v != -1);
    size_t mark = trail_mark(t);

    // try TRUE
    bcp_queue_push(q, encode_lit(v));
    _i0 = read_instret();
    enum bcp_status status = bcp_drain(f, a, q, t);
    g_bcp_instret += read_instret() - _i0;
    if(status == BCP_SAT) return SAT;
    if(status == BCP_UNDETERMINED && dpll(f, a, q, t) == SAT) return SAT;
    _i0 = read_instret();
    trail_unwind(t, mark, f, a);
    g_backtrack_instret += read_instret() - _i0;
    bcp_queue_reset(q);

    // try FALSE
    bcp_queue_push(q, encode_lit(-v));
    _i0 = read_instret();
    status = bcp_drain(f, a, q, t);
    g_bcp_instret += read_instret() - _i0;
    if(status == BCP_SAT) return SAT;
    if(status == BCP_UNDETERMINED && dpll(f, a, q, t) == SAT) return SAT;
    _i0 = read_instret();
    trail_unwind(t, mark, f, a);
    g_backtrack_instret += read_instret() - _i0;
    bcp_queue_reset(q);

    return UNSAT;
}

enum sat_result solve(const Formula *f, Assignment *a){
    g_bcp_instret = g_decision_instret = g_backtrack_instret = 0;

    bcp_queue q;
    Trail t;
    bcp_queue_init(&q, bcp_queue_required_capacity(f));
    trail_init(&t, f->num_vars);

    uint64_t _i0;
    bcp_init(f, a);
    _i0 = read_instret();
    enum bcp_status status = bcp_run(f, a, &q, &t);
    g_bcp_instret += read_instret() - _i0;

    enum sat_result r;
    if(status == BCP_SAT)           r = SAT;
    else if(status == BCP_UNSAT)    r = UNSAT;
    else                            r = dpll(f, a, &q, &t);

    bcp_queue_free(&q);
    trail_free(&t);

    fprintf(stderr, "c phase bcp       instret: %llu\n", (unsigned long long)g_bcp_instret);
    fprintf(stderr, "c phase decision  instret: %llu\n", (unsigned long long)g_decision_instret);
    fprintf(stderr, "c phase backtrack instret: %llu\n", (unsigned long long)g_backtrack_instret);
    if (&g_outer_false != NULL)
        fprintf(stderr,
                "c bcp counters: prop_calls=%llu outer_true=%llu outer_false=%llu"
                " kenter_false=%llu kloop_false=%llu kloop_true=%llu kloop_total=%llu\n",
                (unsigned long long)g_prop_calls,
                (unsigned long long)g_outer_true, (unsigned long long)g_outer_false,
                (unsigned long long)g_kenter_false,
                (unsigned long long)g_kloop_false, (unsigned long long)g_kloop_true,
                (unsigned long long)g_kloop_total);

    return r;
}