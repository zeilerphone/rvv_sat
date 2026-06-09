#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <stddef.h> // size_t
#include <stdlib.h>
#include <string.h>

#include "cnf.h"
#include "trail.h"
#include "bcp.h"
#include "solver.h"

// Print a Formula in human-readable form, showing both the forward
// (clause -> literals) and transposed (literal -> clauses) views.
void formula_print(const Formula *f);

// Print just the forward view (compact).
void formula_print_clauses(const Formula *f);

// Print the transposed view (compact).
void formula_print_transpose(const Formula *f);

/* ===== bcp debugger helper functions ===== */

void build_formula_manual(Formula *f, size_t num_vars, size_t num_clauses,
    const int32_t *dimacs_clauses, const size_t *clause_lengths );
void print_state(const Formula *f, const Assignment *a, 
    const bcp_queue *q, const Trail *t);
const char *status_name(enum bcp_status s);
void setup_state(Formula *f, Assignment *a, bcp_queue *q, 
    Trail *t, const int32_t *clauses, const size_t *lengths,
    size_t num_vars, size_t num_clauses);
void teardown_state(Formula *f, Assignment *a, bcp_queue *q, Trail *t);
void preassign(Formula *f, Assignment *a, bcp_queue *q, Trail *t,
    const int32_t *dimacs_lits, size_t n);


// Capture full Assignment state so we can verify a rewind restored it exactly.
typedef struct {
    int32_t  *values;
    uint8_t  *lit_status;
    int32_t  *sat_una;
    size_t    num_vars;
    size_t    num_clauses;
} state_snapshot;

void snapshot_take(state_snapshot *s, const Formula *f, const Assignment *a);
void snapshot_free(state_snapshot *s);
int  snapshot_compare(const state_snapshot *s, const Assignment *a);


int verify_sat(const Formula *f, const Assignment *a);

#endif // DEBUG_H