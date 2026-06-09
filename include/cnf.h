// cnf.h
#include <stdio.h>  // FILE
#include <stddef.h> // size_t
#include <stdint.h> // int32_t, int8_t

#ifndef RVV_SAT_CNF_H
#define RVV_SAT_CNF_H

#define MAX_VARS    4096      /* maximum number of Boolean variables          */
#define MAX_CLAUSES 65536     /* maximum number of clauses in the formula     */
#define MAX_LITS    3         /* literals per clause (3 for 3-SAT)            */

#define LIT_PAD     0

#define VAR_TRUE    1
#define VAR_FALSE   (-1)
#define VAR_UNSET   0

typedef struct {
    // Forward CSR: clause -> literals 
    // clause i contains literals
    //  lit_col[clause_row_off[i] : clause_row_off[i + 1] - 1]
    int32_t *lit_col;           // [num_lit_occurances]
    int32_t *clause_row_off;    // [num_clauses + 1]

    // Padded dense form: M rows × K columns, row-major.
    // Stores internal literal indices. Padding slots use the sentinel
    // value LIT_PAD (= -1 or similar) to indicate "no literal here".
    int32_t *clause_dense;       // [num_clauses * max_clause_len]
    size_t   max_clause_len;     // K
    
    // Transposed CSR: variable -> clause
    // literal k is contained by clauses
    //  clause_col[lit_row_off[k] : lit_row_off[k + 1] - 1]
    int32_t *clause_col;        // [num_lit_occurances]
    int32_t *lit_row_off;       // [2*num_vars + 1]
    // literal encoding: 
    //   2*v        positive var v (1-indexed)
    //   2*v + 1    negation of var v

    size_t    num_clauses;
    size_t    num_vars;
    size_t    num_lit_occurances;   // total literals across all clauses
} Formula;

typedef struct {
    int32_t *values;        // [num_vars+1], 1-indexed
                            // UNSET=0, TRUE=1, FALSE=-1
    uint8_t *lit_status;    // [2*num_vars], internal lit indexed
                            // 0x00 = falsified, 0x01 = satisfied, 0x10 = unset

    // Per-clause state (mutable, walked back on backtrack)
    int32_t *num_satisfied;   // [num_clauses]
    int32_t *num_unassigned;  // [num_clauses]
} Assignment;

// Encode a DIMACS literal (signed 1-based variable) into a 0-based literal
// index used by lit_row_off. We use:
// - DIMACS: +v -> internal: 2*(v-1)
// - DIMACS: -v -> internal: 2*(v-1) + 1
// literal index k corresponds to variable (k/2)+1 with polarity (k&1)==0.
// lit index 1,2 -> var 1; 
// lit index 2,3 -> var 2;
static inline int32_t encode_lit(int32_t dimacs_lit) {
    int32_t var = dimacs_lit > 0 ? dimacs_lit : -dimacs_lit;
    int32_t neg = dimacs_lit < 0 ? 1 : 0;
    return 2 * (var - 1) + neg;
}

// Decode a 0-based literal index used by lit_row_off back to a DIMACS 
// literal (signed 1-based variable) for printing. Use:
// - internal: 2*(v-1)   -> DIMACS  +v
// - internal: 2*(v-1)+1 -> DIMACS  -v
static inline int32_t decode_lit(int32_t internal) {
    int32_t var = internal / 2 + 1;
    int32_t neg = internal & 1;
    return neg ? -var : var;
}

// Variable index (1-based) from internal literal index.
static inline int32_t lit_var(int32_t internal) {
    return internal / 2 + 1;
}

// 1 if the literal is the negative form, 0 if positive.
static inline int32_t lit_is_negated(int32_t internal) {
    return internal & 1;
}

// For internal literal `lit`, get the half-open range of indices into
// `clause_col` containing the clauses where this literal appears.
static inline void lit_clause_range(const Formula *f, int32_t lit,
                                    int32_t *start, int32_t *end) {
    *start = f->lit_row_off[lit];
    *end   = f->lit_row_off[lit + 1];
}

// Symmetric helper for the forward direction.
static inline void clause_lit_range(const Formula *f, int32_t clause,
                                    int32_t *start, int32_t *end) {
    *start = f->clause_row_off[clause];
    *end   = f->clause_row_off[clause + 1];
}

// Helper to update assignment. 
// -- unused to avoid recomputing polarity. easier to do in bcp_prop_one
static inline void assign_var(Assignment *a, int32_t v, int32_t polarity) {
    a->values[v] = polarity;
    int32_t pos_lit = encode_lit(v);
    int32_t neg_lit = pos_lit + 1;
    if (polarity == VAR_TRUE) {
        a->lit_status[pos_lit] = 0x01;   // +v satisfied
        a->lit_status[neg_lit] = 0x00;   // -v falsified
    } else {
        a->lit_status[pos_lit] = 0x00;
        a->lit_status[neg_lit] = 0x01;
    }
}

static inline void unassign_var(Assignment *a, int32_t v) {
    a->values[v] = VAR_UNSET;
    a->lit_status[encode_lit(v)]  = 0x10;
    a->lit_status[encode_lit(-v)] = 0x10;
}

void parse_dimacs(FILE *in, Formula *f);
void formula_free(Formula *f);
void assignment_init(Assignment *a, size_t num_vars, size_t num_clauses);
void assignment_free(Assignment *a);

#endif // RVV_SAT_CNF_H