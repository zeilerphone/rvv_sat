#include "debug.h"

// formula print helpers 
void formula_print_clauses(const Formula *f) {
    printf("Forward CSR (clause -> literals):\n");
    printf("  num_clauses=%zu  num_vars=%zu  num_lit_occurances=%zu\n",
           f->num_clauses, f->num_vars, f->num_lit_occurances);
    for (size_t i = 0; i < f->num_clauses; i++) {
        int32_t start = f->clause_row_off[i];
        int32_t end   = f->clause_row_off[i + 1];
        printf("  C%zu (len=%d): ", i, end - start);
        for (int32_t k = start; k < end; k++) {
            int32_t lit = f->lit_col[k];
            printf("%d ", decode_lit(lit));
        }
        printf(" [internal: ");
        for (int32_t k = start; k < end; k++) printf("%d ", f->lit_col[k]);
        printf("]\n");
    }
}
void formula_print_transpose(const Formula *f) {
    printf("Transposed CSR (literal -> clauses):\n");
    size_t n_lits = 2 * f->num_vars;
    for (size_t k = 0; k < n_lits; k++) {
        int32_t start = f->lit_row_off[k];
        int32_t end   = f->lit_row_off[k + 1];
        if (start == end) continue;   // skip literals that appear nowhere
        int32_t dimacs = decode_lit((int32_t)k);
        printf("  lit %3d (internal %2zu, count=%d): ",
               dimacs, k, end - start);
        for (int32_t i = start; i < end; i++) {
            printf("C%d ", f->clause_col[i]);
        }
        printf("\n");
    }
}
void formula_print(const Formula *f) {
    formula_print_clauses(f);
    formula_print_transpose(f);
}


// Build a tiny formula by hand, bypassing parse_dimacs so we can construct
// known instances directly. The CSR fields are populated to match a small
// hand-checkable input.
// [NOTE:] must use formula_free afterward
void build_formula_manual(
    Formula *f,
    size_t num_vars,
    size_t num_clauses,
    const int32_t *dimacs_clauses,
    const size_t *clause_lengths)
{
    memset(f, 0, sizeof(*f));
    f->num_vars = num_vars;
    f->num_clauses = num_clauses;

    // Count total literals + track max clause length
    size_t total = 0;
    f->max_clause_len = 0;
    for (size_t i = 0; i < num_clauses; i++) {
        total += clause_lengths[i];
        f->max_clause_len = (clause_lengths[i] > f->max_clause_len) ? clause_lengths[i] : f->max_clause_len;
    }
    f->num_lit_occurances = total;

    // Forward CSR
    f->lit_col = malloc(total * sizeof(int32_t));
    f->clause_row_off = malloc((num_clauses + 1) * sizeof(int32_t));
    f->clause_row_off[0] = 0;
    size_t li = 0;
    for (size_t c = 0; c < num_clauses; c++) {
        for (size_t k = 0; k < clause_lengths[c]; k++) {
            f->lit_col[li] = encode_lit(dimacs_clauses[li]);
            li++;
        }
        f->clause_row_off[c + 1] = (int32_t)li;
    }

    // DENSE
    f->clause_dense = malloc(num_clauses * f->max_clause_len * sizeof(int32_t));
    for(size_t c = 0; c < f->num_clauses; c++){
        int32_t clause_start = f->clause_row_off[c];
        int32_t clause_end = f->clause_row_off[c + 1];
        for(int32_t k = clause_start; k < clause_end; k++){
            f->clause_dense[c * f->max_clause_len + (k - clause_start)] = f->lit_col[k];
        }
        for(size_t i = clause_end - clause_start; i < f->max_clause_len; i++){
            f->clause_dense[c * f->max_clause_len + i] = LIT_PAD;
        } 
    }
    for(size_t i = 0; i < num_clauses; i++){
        printf("Clause %zu: [ ", i);
        for(size_t j = 0; j < f->max_clause_len; j++){
            printf("%i ", f->clause_dense[i*f->max_clause_len + j]);
        }
        printf("]\n");
    }

    // Transposed CSR (same histogram + prefix-sum trick as parse_dimacs)
    size_t n_lits = 2 * num_vars;
    f->lit_row_off = calloc(n_lits + 1, sizeof(int32_t));
    f->clause_col = malloc(total * sizeof(int32_t));
    for (size_t i = 0; i < total; i++) f->lit_row_off[f->lit_col[i] + 1]++;
    for (size_t k = 0; k < n_lits; k++) f->lit_row_off[k + 1] += f->lit_row_off[k];

    int32_t *cursor = malloc(n_lits * sizeof(int32_t));
    memcpy(cursor, f->lit_row_off, n_lits * sizeof(int32_t));
    for (size_t c = 0; c < num_clauses; c++) {
        int32_t s = f->clause_row_off[c], e = f->clause_row_off[c + 1];
        for (int32_t k = s; k < e; k++) {
            f->clause_col[cursor[f->lit_col[k]]++] = (int32_t)c;
        }
    }
    free(cursor);
}

// Print compact state for debugging.
void print_state(const Formula *f, const Assignment *a, const bcp_queue *q, const Trail *t) {
    printf("    values: ");
    for (size_t v = 1; v <= f->num_vars; v++) {
        printf("x%zu=", v);
        if      (a->values[v] ==  1) printf("T ");
        else if (a->values[v] == -1) printf("F ");
        else                          printf("? ");
    }
    printf("\n    counters: ");
    for (size_t c = 0; c < f->num_clauses; c++) {
        printf("C%zu(s=%d,u=%d) ", c, a->num_satisfied[c], a->num_unassigned[c]);
    }
    printf("\n    queue: [");
    for (size_t i = q->head; i < q->tail; i++) {
        printf("%d ", decode_lit(q->buf[i]));
    }
    printf("]\n    trail: [");
    for (size_t i = 0; i < t->top; i++) {
        printf("x%d ", t->vars[i]);
    }
    printf("]\n");
}

// Convenience: print just status name.
const char *status_name(enum bcp_status s) {
    switch (s) {
        case BCP_SAT:           return "SAT";
        case BCP_UNSAT:         return "UNSAT";
        case BCP_UNDETERMINED:  return "UNDETERMINED";
        default:                return "???";
    }
}

// setup state (drop at top of test case)
void setup_state(Formula *f, Assignment *a, bcp_queue *q, Trail *t,
                        const int32_t *clauses, const size_t *lengths,
                        size_t num_vars, size_t num_clauses)
{
    build_formula_manual(f, num_vars, num_clauses, clauses, lengths);
    assignment_init(a, f->num_vars, f->num_clauses);
    bcp_init(f, a);
    bcp_queue_init(q, bcp_queue_required_capacity(f));
    trail_init(t, f->num_vars);
}

// counterpart to setup state - frees arguments (drop before return in test case)
void teardown_state(Formula *f, Assignment *a, bcp_queue *q, Trail *t) {
    trail_free(t);
    bcp_queue_free(q);
    assignment_free(a);
    formula_free(f);
}

void preassign(Formula *f, Assignment *a, bcp_queue *q, Trail *t,
                     const int32_t *dimacs_lits, size_t n) {
    for (size_t i = 0; i < n; i++) bcp_queue_push(q, encode_lit(dimacs_lits[i]));
    bcp_drain(f, a, q, t);
}

// ─── Snapshot helpers ────────────────────────────────────────────────────

void snapshot_take(state_snapshot *s, const Formula *f, const Assignment *a) {
    s->num_vars = f->num_vars;
    s->num_clauses = f->num_clauses;
    s->values         = malloc((f->num_vars + 1) * sizeof(int8_t));
    s->lit_status     = malloc(2 * f->num_vars * sizeof(uint8_t));
    s->num_satisfied  = malloc(f->num_clauses * sizeof(int32_t));
    s->num_unassigned = malloc(f->num_clauses * sizeof(int32_t));
    memcpy(s->values,         a->values,         (f->num_vars + 1) * sizeof(int8_t));
    memcpy(s->lit_status,     a->lit_status,     2 * f->num_vars * sizeof(uint8_t));
    memcpy(s->num_satisfied,  a->num_satisfied,  f->num_clauses * sizeof(int32_t));
    memcpy(s->num_unassigned, a->num_unassigned, f->num_clauses * sizeof(int32_t));
}

void snapshot_free(state_snapshot *s) {
    free(s->values);
    free(s->lit_status);
    free(s->num_satisfied);
    free(s->num_unassigned);
}

// Returns 1 if the current assignment matches the snapshot exactly.
// Prints diffs on mismatch so failures are easy to localize.
int snapshot_compare(const state_snapshot *s, const Assignment *a) {
    int ok = 1;
    for (size_t v = 1; v <= s->num_vars; v++) {
        if (a->values[v] != s->values[v]) {
            printf("    DIFF: values[%zu]: snapshot=%d, current=%d\n",
                   v, s->values[v], a->values[v]);
            ok = 0;
        }
    }
    for (size_t k = 0; k < 2 * s->num_vars; k++) {
        if (a->lit_status[k] != s->lit_status[k]) {
            printf("    DIFF: lit_status[%zu]: snapshot=0x%02x, current=0x%02x\n",
                   k, s->lit_status[k], a->lit_status[k]);
            ok = 0;
        }
    }
    for (size_t c = 0; c < s->num_clauses; c++) {
        if (a->num_satisfied[c] != s->num_satisfied[c]) {
            printf("    DIFF: num_satisfied[%zu]: snapshot=%d, current=%d\n",
                   c, s->num_satisfied[c], a->num_satisfied[c]);
            ok = 0;
        }
        if (a->num_unassigned[c] != s->num_unassigned[c]) {
            printf("    DIFF: num_unassigned[%zu]: snapshot=%d, current=%d\n",
                   c, s->num_unassigned[c], a->num_unassigned[c]);
            ok = 0;
        }
    }
    return ok;
}

/* ===== Verify ===== */
// Verify that an assignment actually satisfies a formula. 
int verify_sat(const Formula *f, const Assignment *a) {
    for (size_t i = 0; i < f->num_clauses; i++) {
        int32_t start = f->clause_row_off[i];
        int32_t end   = f->clause_row_off[i + 1];
        int satisfied = 0;
        for (int32_t k = start; k < end; k++) {
            int32_t lit = f->lit_col[k];
            int32_t v = lit_var(lit);
            int8_t  required = lit_is_negated(lit) ? VAR_FALSE : VAR_TRUE;
            if (a->values[v] == required) { satisfied = 1; break; }
        }
        if (!satisfied) {
            printf("    VERIFY FAIL: clause %zu is not satisfied\n", i);
            int32_t s, e;
            clause_lit_range(f, (int32_t)i, &s, &e);
            printf("      clause: [");
            for (int32_t k = s; k < e; k++) printf("%d ", decode_lit(f->lit_col[k]));
            printf("]\n      values: ");
            for (size_t v = 1; v <= f->num_vars; v++) {
                printf("x%zu=%s ", v,
                       a->values[v] == VAR_TRUE  ? "T" :
                       a->values[v] == VAR_FALSE ? "F" : "?");
            }
            printf("\n");
            return 0;
        }
    }
    return 1;
}


/* additional debug fragments to add to bcp_rvv.c where marked by commments:

// add `#define DEBUG_DRAIN` to top of bcp_rvv.c
    // [DEBUG] `DEBUG_DRAIN` start message
    #ifdef DEBUG_DRAIN
    fprintf(stderr, "drain start, queue: ");
    for (size_t i = q->head; i < q->tail; i++) fprintf(stderr, "%d ", decode_lit(q->buf[i]));
    fprintf(stderr, "\n");
    #endif

    // [DEBUG] `DEBUG_DRAIN` dump queue
    #ifdef DEBUG_DRAIN
    fprintf(stderr, "  after prop, queue: ");
    for (size_t i = q->head; i < q->tail; i++) fprintf(stderr, "%d ", decode_lit(q->buf[i]));
    fprintf(stderr, "\n");
    #endif

// add `#define DEBUG_PROP_ONE` to top of bcp_rvv.c message
    // [DEBUG] `DEBUG_PROP_ONE` lit_true start mesage
    #ifdef DEBUG_PROP_ONE
    fprintf(stderr, "  prop_one(internal_lit=%d, dimacs=%d), lit_true=%d:\n",
        internal_lit, decode_lit(internal_lit), lit_true);
    #endif

    // [DEBUG] `DEBUG_PROP_ONE` lit_false start mesage
    #ifdef DEBUG_PROP_ONE
    fprintf(stderr, "  prop_one(internal_lit=%d, dimacs=%d), lit_false=%d:\n",
    internal_lit, decode_lit(internal_lit), lit_false);
    #endif

    // add twice
    // [DEBUG] `DEBUG_PROP_ONE` dump vunit, unit_mask, unset_mask, queue_mask, vk_lit, vk_val
    #ifdef DEBUG_PROP_ONE
    int32_t  vunit_buf[vl];
    uint8_t  unit_mask_buf[(vl + 7) / 8];
    uint8_t  unset_mask_buf[(vl + 7) / 8];
    uint8_t  queue_mask_buf[(vl + 7) / 8];
    int32_t  vk_lit_buf[vl];
    int8_t   vk_val_buf[vl];

    __riscv_vse32_v_i32m1(vunit_buf, vunit, vl);
    __riscv_vsm_v_b32(unit_mask_buf, unit_mask, vl);
    __riscv_vsm_v_b32(unset_mask_buf, unset_mask, vl);
    __riscv_vsm_v_b32(queue_mask_buf, queue_mask, vl);
    __riscv_vse32_v_i32m1(vk_lit_buf, vk_lit, vl);
    __riscv_vse8_v_i8mf4(vk_val_buf, vk_val, vl);

    fprintf(stderr, "    k=%zu: ", k);
    for (size_t i = 0; i < vl; i++) {
        int unit_bit  = (unit_mask_buf[i / 8]  >> (i % 8)) & 1;
        int unset_bit = (unset_mask_buf[i / 8] >> (i % 8)) & 1;
        int queue_bit = (queue_mask_buf[i / 8] >> (i % 8)) & 1;
        fprintf(stderr, "[lane %zu: lit=%d val=%d unit=%d unset=%d queue=%d vunit=%d] ",
                i, vk_lit_buf[i], vk_val_buf[i],
                unit_bit, unset_bit, queue_bit, vunit_buf[i]);
    }
    fprintf(stderr, "\n");
    #endif

*/