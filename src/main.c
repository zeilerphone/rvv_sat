// main.c
#include "cnf.h"
#include "bcp.h"
#include "trail.h"
#include "solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Verify a SAT result: walk every clause and confirm at least one literal
// evaluates to TRUE under the assignment. Returns 1 on success, 0 on failure
// with diagnostic output. Same logic as the verify_sat used in tests, kept
// here so main.c is self-contained.
static int verify_sat(const Formula *f, const Assignment *a) {
    for (size_t i = 0; i < f->num_clauses; i++) {
        int32_t start = f->clause_row_off[i];
        int32_t end   = f->clause_row_off[i + 1];
        int satisfied = 0;
        for (int32_t k = start; k < end; k++) {
            int32_t lit = f->lit_col[k];
            int32_t v = lit_var(lit);
            int8_t required = lit_is_negated(lit) ? VAR_FALSE : VAR_TRUE;
            if (a->values[v] == required) { satisfied = 1; break; }
        }
        if (!satisfied) {
            fprintf(stderr, "VERIFY FAIL: clause %zu is not satisfied\n", i);
            fprintf(stderr, "  clause: [");
            for (int32_t k = start; k < end; k++) {
                fprintf(stderr, "%d ", decode_lit(f->lit_col[k]));
            }
            fprintf(stderr, "]\n");
            return 0;
        }
    }
    return 1;
}

// Print a SAT assignment in DIMACS-compatible "v" line format:
//   v 1 -2 3 -4 ... 0
// One signed variable per assigned var, terminated by 0.
static void print_assignment(const Assignment *a, size_t num_vars) {
    printf("v");
    for (size_t v = 1; v <= num_vars; v++) {
        int32_t lit = (a->values[v] == VAR_TRUE)  ?  (int32_t)v :
                      (a->values[v] == VAR_FALSE) ? -(int32_t)v :
                                                     (int32_t)v;
        // Unset variables print as positive (arbitrary choice — they can
        // be either polarity and the formula is still satisfied).
        printf(" %d", lit);
    }
    printf(" 0\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.cnf>\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "error: could not open %s\n", path);
        return 2;
    }

    Formula f;
    parse_dimacs(fp, &f);
    fclose(fp);

    fprintf(stderr, "c parsed %s: %zu vars, %zu clauses, %zu literals\n",
            path, f.num_vars, f.num_clauses, f.num_lit_occurances);

    Assignment a;
    assignment_init(&a, f.num_vars, f.num_clauses);

    clock_t t0 = clock();
    enum sat_result r = solve(&f, &a);
    clock_t t1 = clock();
    clock_t diff = t1 - t0;

    fprintf(stderr, "c solve time: %lu cycles\n", diff);

    int exit_code = 0;
    if (r == SAT) {
        if (!verify_sat(&f, &a)) {
            fprintf(stderr, "s INTERNAL ERROR: solver returned SAT but "
                            "assignment does not satisfy formula\n");
            exit_code = 3;
        } else {
            printf("s SATISFIABLE\n");
            print_assignment(&a, f.num_vars);
        }
    } else {
        printf("s UNSATISFIABLE\n");
    }

    assignment_free(&a);
    formula_free(&f);
    return exit_code;
}