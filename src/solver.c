// solver.c
#include "solver.h"

#include <assert.h>

int32_t pick_unassigned(const Formula *f, const Assignment *a){
    for (size_t v = 1; v <= f->num_vars; v++) {
        if (a->values[v] == VAR_UNSET) return (int32_t)v;
    }
    return -1;   // all variables assigned
}

static enum sat_result dpll(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){
    // pick new variable and mark it on the trail
    int32_t v = pick_unassigned(f, a);
    assert(v != -1);
    size_t mark = trail_mark(t);

    // try TRUE
    bcp_queue_push(q, encode_lit(v));
    enum bcp_status status = bcp_drain(f, a, q, t);
    if(status == BCP_SAT) return SAT;
    if(status == BCP_UNDETERMINED && dpll(f, a, q, t) == SAT) return SAT;
    trail_unwind(t, mark, f, a);
    bcp_queue_reset(q);

    // try FALSE
    bcp_queue_push(q, encode_lit(-v));
    status = bcp_drain(f, a, q, t);
    if(status == BCP_SAT) return SAT;
    if(status == BCP_UNDETERMINED && dpll(f, a, q, t) == SAT) return SAT;
    trail_unwind(t, mark, f, a);
    bcp_queue_reset(q);

    return UNSAT;
}

enum sat_result solve(const Formula *f, Assignment *a){
    bcp_queue q;
    Trail t;
    bcp_queue_init(&q, bcp_queue_required_capacity(f));
    trail_init(&t, f->num_vars);

    bcp_init(f, a);                                 // initialize per-clause counters
    enum bcp_status status = bcp_run(f, a, &q, &t); // initialize BCP
    enum sat_result r;
    if(status == BCP_SAT)           r = SAT;
    else if(status == BCP_UNSAT)    r = UNSAT;
    else                            r = dpll(f, a, &q, &t);

    bcp_queue_free(&q);
    trail_free(&t);
    return r;
}