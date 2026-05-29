#include "cnf.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Verify that the transpose is consistent with the forward CSR:
// for every (clause, literal) entry in the forward direction, the
// corresponding (literal, clause) entry must exist in the transpose.
static int check_transpose_consistency(const Formula *f) {
    int ok = 1;

    for (size_t ci = 0; ci < f->num_clauses; ci++) {
        int32_t start = f->clause_row_off[ci];
        int32_t end   = f->clause_row_off[ci + 1];
        for (int32_t li = start; li < end; li++) {
            int32_t lit = f->lit_col[li];
            // Search transpose for this clause under this literal.
            int32_t tstart = f->lit_row_off[lit];
            int32_t tend   = f->lit_row_off[lit + 1];
            int found = 0;
            for (int32_t ti = tstart; ti < tend; ti++) {
                if (f->clause_col[ti] == (int32_t)ci) { found = 1; break; }
            }
            if (!found) {
                printf("  CONSISTENCY FAIL: clause %zu contains literal %d "
                       "(internal %d) but transpose doesn't list this clause\n",
                       ci, decode_lit(lit), lit);
                ok = 0;
            }
        }
    }

    // And the reverse: every entry in the transpose must correspond to
    // a real entry in the forward CSR.
    size_t n_lits = 2 * f->num_vars;
    for (size_t k = 0; k < n_lits; k++) {
        int32_t start = f->lit_row_off[k];
        int32_t end   = f->lit_row_off[k + 1];
        for (int32_t i = start; i < end; i++) {
            int32_t ci = f->clause_col[i];
            int32_t cstart = f->clause_row_off[ci];
            int32_t cend   = f->clause_row_off[ci + 1];
            int found = 0;
            for (int32_t li = cstart; li < cend; li++) {
                if (f->lit_col[li] == (int32_t)k) { found = 1; break; }
            }
            if (!found) {
                printf("  CONSISTENCY FAIL: transpose says clause %d "
                       "contains internal literal %zu, but forward CSR "
                       "disagrees\n", ci, k);
                ok = 0;
            }
        }
    }

    return ok;
}

// Verify clause_row_off is monotone non-decreasing and ends at num_lit_occurances.
static int check_offsets(const Formula *f) {
    int ok = 1;
    if (f->clause_row_off[0] != 0) {
        printf("  OFFSET FAIL: clause_row_off[0] should be 0, is %d\n",
               f->clause_row_off[0]);
        ok = 0;
    }
    for (size_t i = 0; i < f->num_clauses; i++) {
        if (f->clause_row_off[i+1] < f->clause_row_off[i]) {
            printf("  OFFSET FAIL: clause_row_off non-monotone at %zu\n", i);
            ok = 0;
        }
    }
    if ((size_t)f->clause_row_off[f->num_clauses] != f->num_lit_occurances) {
        printf("  OFFSET FAIL: clause_row_off[end]=%d, expected %zu\n",
               f->clause_row_off[f->num_clauses], f->num_lit_occurances);
        ok = 0;
    }
    // Same checks for lit_row_off
    size_t n_lits = 2 * f->num_vars;
    if (f->lit_row_off[0] != 0) {
        printf("  OFFSET FAIL: lit_row_off[0] should be 0, is %d\n",
               f->lit_row_off[0]);
        ok = 0;
    }
    for (size_t k = 0; k < n_lits; k++) {
        if (f->lit_row_off[k+1] < f->lit_row_off[k]) {
            printf("  OFFSET FAIL: lit_row_off non-monotone at %zu\n", k);
            ok = 0;
        }
    }
    if ((size_t)f->lit_row_off[n_lits] != f->num_lit_occurances) {
        printf("  OFFSET FAIL: lit_row_off[end]=%d, expected %zu\n",
               f->lit_row_off[n_lits], f->num_lit_occurances);
        ok = 0;
    }
    return ok;
}

// Verify literal indices are in valid range.
static int check_literal_range(const Formula *f) {
    int ok = 1;
    int32_t max_valid = 2 * (int32_t)f->num_vars - 1;
    for (size_t i = 0; i < f->num_lit_occurances; i++) {
        int32_t k = f->lit_col[i];
        if (k < 0 || k > max_valid) {
            printf("  RANGE FAIL: lit_col[%zu]=%d out of [0, %d]\n",
                   i, k, max_valid);
            ok = 0;
        }
    }
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.cnf>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }

    Formula f;
    parse_dimacs(fp, &f);
    fclose(fp);

    printf("=== Parsed formula ===\n");
    formula_print(&f);

    printf("\n=== Consistency checks ===\n");
    int ok = 1;
    printf("offsets:    %s\n", (ok &= check_offsets(&f))            ? "PASS" : "FAIL");
    printf("lit range:  %s\n", (ok &= check_literal_range(&f))      ? "PASS" : "FAIL");
    printf("transpose:  %s\n", (ok &= check_transpose_consistency(&f)) ? "PASS" : "FAIL");

    formula_free(&f);

    printf("\noverall: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}