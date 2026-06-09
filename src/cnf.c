// cnf.c
#include <stdlib.h>     // malloc, free
#include <string.h>     // strtok, etc.
#include <stdio.h>

#include "cnf.h"

// ===== Allocation lifecycle ===================
void assignment_init(Assignment *a, size_t num_vars, size_t num_clauses) {
    // zero initialize values : VAR_UNSET = 0
    a->values         = calloc(num_vars + 1, sizeof(int32_t));
    // different representation of above 
    // - helpful for interfacing with Formula
    a->lit_status     = malloc(2 * num_vars * sizeof(uint8_t));
    // allocate memory for per-clause counters
    a->sat_una = malloc(num_clauses * sizeof(int32_t));

    // check for issues with allocating memory
    if (!a->values || !a->lit_status || !a->sat_una) {
        fprintf(stderr, "assignment_init: allocation failed\n");
        free(a->values); free(a->lit_status); free(a->sat_una);
        a->values = NULL;
        a->lit_status = NULL;
        a->sat_una = NULL;
        exit(1);
    }

    // Initialize all literals to UNSET (0x10) explicitely 
    // [NOTE]: vectorizable but only runs once
    for (size_t k = 0; k < 2 * num_vars; k++) {
        a->lit_status[k] = 0x10;
    }
}

void assignment_free(Assignment *a) {
    if (!a) return;
    free(a->values);
    free(a->lit_status);
    free(a->sat_una);
    a->values = NULL;
    a->lit_status = NULL;
    a->sat_una = NULL;
}

void formula_free(Formula *f) {
    if (!f) return;
    free(f->lit_col);
    free(f->clause_row_off);
    free(f->clause_dense);
    free(f->clause_col);
    free(f->lit_row_off);
    // Zero the struct so accidental reuse fails loudly rather than
    // dereferencing a freed pointer.
    f->lit_col = NULL;
    f->clause_row_off = NULL;
    f->clause_dense = NULL;
    f->max_clause_len = 0;
    f->clause_col = NULL;
    f->lit_row_off = NULL;
    f->num_clauses = 0;
    f->num_vars = 0;
    f->num_lit_occurances = 0;
}

// ─── Parsing ─────────────────────────────────────────────────────────────

// formula_init 
// largely LLM generated
void parse_dimacs(FILE *in, Formula *f) {
    // Initialize to a clean state so partial failures don't leave garbage.
    memset(f, 0, sizeof(*f));

    char line[4096];

    // ── Pass 1: find the "p cnf <vars> <clauses>" header ──
    long header_pos = 0;
    int found_header = 0;
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == 'c' || line[0] == '\n') continue;
        if (line[0] == 'p') {
            int nv, nc;
            if (sscanf(line, "p cnf %d %d", &nv, &nc) != 2) {
                fprintf(stderr, "parse_dimacs: malformed header: %s", line);
                exit(1);
            }
            f->num_vars    = (size_t)nv;
            f->num_clauses = (size_t)nc;
            header_pos = ftell(in);   // remember where the header ended
            found_header = 1;
            break;
        }
    }
    if (!found_header) {
        fprintf(stderr, "parse_dimacs: no 'p cnf' header found\n");
        exit(1);
    }

    // ── Pass 2: count total literal occurrences so we can size lit_col ──
    // We do this by scanning all remaining tokens once. Alternative: do
    // a single pass that grows lit_col with realloc, but a separate
    // counting pass keeps the code simpler and avoids reallocation.
    size_t total_lits = 0;
    size_t clause_len = 0;
    size_t max_len = 0;
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == 'c' || line[0] == 'p' || line[0] == '\n') continue;
        if (line[0] == '%') break;   // SATLIB end marker
        char *tok = strtok(line, " \t\r\n");
        while (tok) {
            int lit = atoi(tok);
            if (lit != 0) {
                total_lits++;
                clause_len++;
            }
            tok = strtok(NULL, " \t\r\n");
        }
        if(clause_len > max_len) max_len = clause_len;
    }
    f->num_lit_occurances = total_lits;
    f->max_clause_len = max_len;

    // ── Allocate forward-CSR storage now that we know the sizes ──
    f->lit_col        = malloc(total_lits * sizeof(int32_t));
    f->clause_row_off = malloc((f->num_clauses + 1) * sizeof(int32_t));
    if (!f->lit_col || !f->clause_row_off) {
        fprintf(stderr, "parse_dimacs: allocation failed (forward CSR)\n");
        exit(1);
    }

    // ── Pass 3: rewind and fill the forward CSR ──
    fseek(in, header_pos, SEEK_SET);
    size_t lit_idx = 0;
    size_t clause_idx = 0;
    f->clause_row_off[0] = 0;

    while (fgets(line, sizeof(line), in)) {
        if (line[0] == 'c' || line[0] == 'p' || line[0] == '\n') continue;
        if (line[0] == '%') break;
        char *tok = strtok(line, " \t\r\n");
        while (tok) {
            int lit = atoi(tok);
            tok = strtok(NULL, " \t\r\n");
            if (lit == 0) {
                // End of clause: record where the next clause starts.
                clause_idx++;
                if (clause_idx > f->num_clauses) {
                    fprintf(stderr, "parse_dimacs: more clauses than header claimed\n");
                    exit(1);
                }
                f->clause_row_off[clause_idx] = (int32_t)lit_idx;
            } else {
                if (lit_idx >= total_lits) {
                    fprintf(stderr, "parse_dimacs: literal overflow\n");
                    exit(1);
                }
                f->lit_col[lit_idx++] = encode_lit(lit);
            }
        }
    }

    if (clause_idx != f->num_clauses) {
        fprintf(stderr,
                "parse_dimacs: header said %zu clauses, found %zu\n",
                f->num_clauses, clause_idx);
        // Not fatal - some DIMACS files have minor mismatches - but warn.
    }

    // Allocate dense storage
    f->clause_dense = malloc(f->num_clauses * max_len * sizeof(int32_t));
    if (!f->clause_dense){
        fprintf(stderr, "parse_dimacs: allocation failed (dense table)\n");
        exit(1);
    }

    // copy literals into dense representation, fill empty with LIT_PAD 
    for(size_t c = 0; c < f->num_clauses; c++){
        size_t clause_start = f->clause_row_off[c];
        size_t clause_end = f->clause_row_off[c + 1];
        for(size_t k = clause_start; k < clause_end; k++){
            f->clause_dense[c * max_len + (k - clause_start)] = f->lit_col[k];
        }
        for(size_t i = clause_end - clause_start; i < max_len; i++){
            f->clause_dense[c * max_len + i] = LIT_PAD;
        } 
    }

    // ── Build the transposed CSR (literal -> clauses containing it) ──
    // First pass: count occurrences of each literal index.
    size_t n_lit_slots = 2 * f->num_vars;
    f->clause_col  = malloc(total_lits * sizeof(int32_t));
    f->lit_row_off = calloc(n_lit_slots + 1, sizeof(int32_t));
    if (!f->lit_row_off || !f->clause_col) {
        fprintf(stderr, "parse_dimacs: allocation failed (transposed CSR)\n");
        exit(1);
    }

    // Histogram: lit_row_off[k+1] = count of literal k
    // (We deliberately offset by 1 so that the prefix-sum below directly
    // produces the correct lit_row_off without an extra shift.)
    for (size_t i = 0; i < total_lits; i++) {
        int32_t k = f->lit_col[i];
        f->lit_row_off[k + 1]++;
    }
    // Prefix sum: lit_row_off[k] becomes the start index for literal k.
    for (size_t k = 0; k < n_lit_slots; k++) {
        f->lit_row_off[k + 1] += f->lit_row_off[k];
    }

    // Second pass: place each (clause, literal) pair into clause_col.
    // We need a temporary cursor that tracks where the next entry for
    // each literal goes. Copy lit_row_off into it, then increment as
    // we fill.
    int32_t *cursor = malloc(n_lit_slots * sizeof(int32_t));
    if (!cursor) {
        fprintf(stderr, "parse_dimacs: allocation failed (cursor)\n");
        exit(1);
    }
    memcpy(cursor, f->lit_row_off, n_lit_slots * sizeof(int32_t));

    for (size_t ci = 0; ci < f->num_clauses; ci++) {
        int32_t start = f->clause_row_off[ci];
        int32_t end   = f->clause_row_off[ci + 1];
        for (int32_t li = start; li < end; li++) {
            int32_t k = f->lit_col[li];
            f->clause_col[cursor[k]++] = (int32_t)ci;
        }
    }

    free(cursor);
}