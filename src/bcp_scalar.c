// bcp_scalar.c
#include "bcp.h"

void bcp_init(const Formula *f, Assignment *a){
    for(size_t i = 0; i < f->num_clauses; i++){
        a->num_satisfied[i] = 0;
        a->num_unassigned[i] = f->clause_row_off[i + 1] - f->clause_row_off[i];
    }
}

enum bcp_status bcp_run(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){
    bcp_queue_reset(q);

    int32_t all_sat = 1;
    for(size_t i = 0; i < f->num_clauses; i++){
        if(a->num_satisfied[i] == 0){
            all_sat = 0;
            if(a->num_unassigned[i] == 0) return BCP_UNSAT;
            if(a->num_unassigned[i] == 1) {
                int32_t start_lit_idx = f->clause_row_off[i];
                int32_t end_lit_idx = f->clause_row_off[i + 1];
                for(int32_t qlit_idx = start_lit_idx; qlit_idx < end_lit_idx; qlit_idx++){
                    int32_t qlit = f->lit_col[qlit_idx];
                    int32_t v = lit_var(qlit);
                    if(a->values[v] == VAR_UNSET){
                        bcp_queue_push(q, qlit);
                        break;
                    }
                }
            }
        }
    }
    if(all_sat == 1) return BCP_SAT;

    return bcp_drain(f, a, q, t);
}

enum bcp_status bcp_drain(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){
    while(!bcp_queue_empty(q)){
        int32_t lit = bcp_queue_pop(q);
        if(bcp_prop_one(f, a, q, t, lit) == BCP_STEP_UNSAT) return BCP_UNSAT;
    }

    for(size_t i = 0; i < f->num_clauses; i++){
        if(a->num_satisfied[i] == 0) return BCP_UNDETERMINED;
    }
    return BCP_SAT;
}

enum bcp_step_status bcp_prop_one(const Formula *f, Assignment *a, bcp_queue *q, Trail *t, int32_t internal_lit){
    // decode literal
    int32_t var = (internal_lit / 2) + 1; 
    int32_t var_polarity = (internal_lit & 1) ? VAR_FALSE : VAR_TRUE;

    // check for existing assignment
    if(a->values[var] == var_polarity){
        return BCP_STEP_OK;
    } else if (a->values[var] == -var_polarity){
        return BCP_STEP_UNSAT;
    } 

    // make assignment
    int32_t lit_true = internal_lit;
    int32_t lit_false = internal_lit ^ 1; // flip the LSB
    a->values[var] = var_polarity;
    a->lit_status[lit_true] = 0x01;
    a->lit_status[lit_false] = 0x00;

    // log assignment on the trail
    trail_push(t, var);

    // update affected clauses
    int conflict_found = 0;
    // clauses with literal lit_true
    int32_t start_clause_idx = f->lit_row_off[lit_true];
    int32_t end_clause_idx = f->lit_row_off[lit_true + 1];
    for(int32_t clause_idx = start_clause_idx; clause_idx < end_clause_idx; clause_idx ++){
        int32_t clause = f->clause_col[clause_idx];
        a->num_satisfied[clause]++;
        a->num_unassigned[clause]--;
        if(a->num_satisfied[clause] == 0){
            if(a->num_unassigned[clause] == 0) {
                conflict_found = 1;
                continue;
            }
            if(a->num_unassigned[clause] == 1) {
                int32_t start_lit_idx = f->clause_row_off[clause];
                int32_t end_lit_idx = f->clause_row_off[clause + 1];
                for(int32_t qlit_idx = start_lit_idx; qlit_idx < end_lit_idx; qlit_idx++){
                    int32_t qlit = f->lit_col[qlit_idx];
                    int32_t v = lit_var(qlit);
                    if(a->values[v] == VAR_UNSET){
                        bcp_queue_push(q, qlit);
                        break;
                    }
                }
            }
        }
    }

    // clauses with literal lit_false
    start_clause_idx = f->lit_row_off[lit_false];
    end_clause_idx = f->lit_row_off[lit_false + 1];
    for(int32_t clause_idx= start_clause_idx; clause_idx < end_clause_idx; clause_idx ++){
        int32_t clause = f->clause_col[clause_idx];
        a->num_unassigned[clause]--;
        if(a->num_satisfied[clause] == 0){
            if(a->num_unassigned[clause] == 0) {
                conflict_found = 1;
                continue;
            }
            if(a->num_unassigned[clause] == 1) {
                int32_t start_lit_idx = f->clause_row_off[clause];
                int32_t end_lit_idx = f->clause_row_off[clause + 1];
                for(int32_t qlit_idx = start_lit_idx; qlit_idx < end_lit_idx; qlit_idx++){
                    int32_t qlit = f->lit_col[qlit_idx];
                    int32_t v = lit_var(qlit);
                    if(a->values[v] == VAR_UNSET){
                        bcp_queue_push(q, qlit);
                        break;
                    }
                }
            }
        }
    }

    return conflict_found ? BCP_STEP_UNSAT : BCP_STEP_OK;
}

void bcp_rwnd_one(const Formula *f, Assignment *a, int32_t var){
    int32_t val = a->values[var];
    if(val == VAR_UNSET) return;
    int32_t lit = encode_lit(var);
    int32_t sat_lit = (val == VAR_TRUE) ? lit : (lit ^ 1);
    int32_t uns_lit = (sat_lit ^ 1);

    a->values[var] = VAR_UNSET;
    a->lit_status[sat_lit] = 0x10;
    a->lit_status[uns_lit] = 0x10;
    
    // rewind affected clauses
    // clauses with literal sat_lit
    int32_t start_clause_idx = f->lit_row_off[sat_lit];
    int32_t end_clause_idx = f->lit_row_off[sat_lit + 1];
    for(int32_t clause_idx = start_clause_idx; clause_idx < end_clause_idx; clause_idx ++){
        int32_t clause = f->clause_col[clause_idx];
        a->num_satisfied[clause]--;
        a->num_unassigned[clause]++;
    }
    // clauses with literal uns_lit
    start_clause_idx = f->lit_row_off[uns_lit];
    end_clause_idx = f->lit_row_off[uns_lit + 1];
    for(int32_t clause_idx = start_clause_idx; clause_idx < end_clause_idx; clause_idx ++){
        int32_t clause = f->clause_col[clause_idx];
        a->num_unassigned[clause]++;
    }
}
