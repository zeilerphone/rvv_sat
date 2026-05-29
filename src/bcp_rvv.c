// bcp_rvv.c
#include <riscv_vector.h>
#include "bcp.h"

// #define DEBUG_PROP_ONE

void bcp_init(const Formula *f, Assignment *a){
    size_t n = f->num_clauses;
    int32_t *n_satisfied = a->num_satisfied;
    int32_t *n_unassigned = a->num_unassigned;
    int32_t *c_row_off = f->clause_row_off;
    for(size_t vl; n > 0; n -= vl, n_satisfied += vl, n_unassigned += vl, c_row_off += vl){
        vl = __riscv_vsetvl_e32m1(n);
        // fill up vector for num_satisfied with zeroes
        vint32m1_t vn_sat = __riscv_vmv_v_x_i32m1(0, vl);
        __riscv_vse32_v_i32m1(n_satisfied, vn_sat, vl);
        // a->num_unassigned[i] = f->clause_row_off[i + 1] - f->clause_row_off[i];
        vint32m1_t vn_una = __riscv_vsub_vv_i32m1(__riscv_vle32_v_i32m1(c_row_off + 1, vl), __riscv_vle32_v_i32m1(c_row_off, vl), vl);
        __riscv_vse32_v_i32m1(n_unassigned, vn_una, vl);
    }
}

enum bcp_status bcp_run(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){
    bcp_queue_reset(q);

    size_t n = f->num_clauses;
    int32_t *n_satisfied = a->num_satisfied;
    int32_t *n_unassigned = a->num_unassigned;
    int32_t *c_dense= f->clause_dense;

    size_t max_len = f->max_clause_len;

    // scan (no shortcuts to skip clauses - have to scan all)
    int32_t all_sat = 1;
    for(size_t vl; n > 0; n -= vl, n_satisfied += vl, n_unassigned += vl, c_dense += vl * max_len){
        vl = __riscv_vsetvl_e32m1(n);
        
        // load num_satisifed, num_unassigned
        vint32m1_t vn_sat = __riscv_vle32_v_i32m1(n_satisfied, vl);
        vint32m1_t vn_una = __riscv_vle32_v_i32m1(n_unassigned, vl);

        // build 3 masks - sat_mask, conflict_mask, unit_mask
        // sat_mask = (num_satisfied != 0)
        vbool32_t sat_mask = __riscv_vmsne_vx_i32m1_b32(vn_sat, 0, vl);
        if(__riscv_vcpop_m_b32(sat_mask, vl) == vl){
            continue; // if all clauses checked are satisified, continue
        }
        // set all_sat to 0 if not lanes for sat_mask are true
        all_sat = 0;
        // conflict_mask = (num_unassigned == 0) && !sat_mask
        vbool32_t conflict_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 0, vl), sat_mask, vl);
        // exit early if any conflicts - return UNSAT
        if(__riscv_vcpop_m_b32(conflict_mask, vl) > 0) return BCP_UNSAT;

        // unit_mask = (num_unassigned == 1) && !sat_mask
        vbool32_t unit_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 1, vl), sat_mask, vl);
        size_t unit_count = __riscv_vcpop_m_b32(unit_mask, vl);
        // these are the lanes we actually care about getting unset literals for
        if (unit_count == 0) continue;   // no unit clauses, skip k-loop entirely
        
        vint32m1_t vunit = __riscv_vmv_v_x_i32m1(0, vl);
        for(size_t k = 0; k < max_len; k++){
            // load kth literal for each clause (from dense table) - multiply stride by sizeof(int32_t)
            vint32m1_t vk_lit = __riscv_vlse32_v_i32m1(c_dense + k, (max_len << 2), vl);
            // get variable index to access assignment ((lit/2) + 1)
            vint32m1_t vk_var = __riscv_vsra_vx_i32m1(vk_lit, 1, vl);
            vk_var            = __riscv_vadd_vx_i32m1(vk_var, 1, vl);
            // get polarity
            // vint32m1_t vk_pol = __riscv_vand_vx_i32m1(vk_lit, 1, vl);

            // load assignment
            vint8mf4_t vk_val = __riscv_vluxei32_v_i8mf4(a->values, __riscv_vreinterpret_v_i32m1_u32m1(vk_var), vl);
            
            // get boolean mask for vals == UNSET
            vbool32_t unset_mask = __riscv_vmseq_vx_i8mf4_b32(vk_val, VAR_UNSET, vl);

            // combine mask for unset with unit
            vbool32_t queue_mask = __riscv_vmand_mm_b32(unit_mask, unset_mask, vl);

            vunit = __riscv_vor_vv_i32m1_mu(queue_mask, vunit, vunit, vk_lit, vl);
        }
        // // scalar loop (if compaction is slow)
        // int32_t buf[vl];
        // uint8_t mask[vl];
        // __riscv_vse32_v_i32m1(buf, vunit, vl);
        // __riscv_vsm_v_b32(mask, unit_mask, vl);
        // for(size_t i = 0; i < vl; i++){
        //     if(mask[i]) bcp_queue_push(q, buf[i]);
        // }

        // vcompress 
        vint32m1_t vqueue = __riscv_vcompress_vm_i32m1(vunit, unit_mask, vl);
        // may need to vsetvl before this
        __riscv_vse32_v_i32m1(q->buf + q->tail, vqueue, unit_count);
        q->tail += unit_count;
    }

    if(all_sat == 1) return BCP_SAT;

    return bcp_drain(f, a, q, t);
}

// not parallelized at all - relying on bcp_prop_one parallelism
enum bcp_status bcp_drain(const Formula *f, Assignment *a, bcp_queue *q, Trail *t){    
    // [DEBUG] `DEBUG_DRAIN` start message
    while(!bcp_queue_empty(q)){
        int32_t lit = bcp_queue_pop(q);
        #ifdef DEBUG_DRAIN
        fprintf(stderr, "  popping %d (internal %d)\n", decode_lit(lit), lit);
        #endif
        if(bcp_prop_one(f, a, q, t, lit) == BCP_STEP_UNSAT) {
            #ifdef DEBUG_DRAIN
            fprintf(stderr, "  prop returned UNSAT\n");
            #endif            
            return BCP_UNSAT;
        }
        // [DEBUG] `DEBUG_DRAIN` dump queue
    }

    for(size_t i = 0; i < f->num_clauses; i++){
        if(a->num_satisfied[i] == 0) return BCP_UNDETERMINED;
    }
    return BCP_SAT;
}

enum bcp_step_status bcp_prop_one(const Formula *f, Assignment *a, bcp_queue *q, Trail *t, int32_t internal_lit){
    // shift for clause indexing
    size_t clause_shift = 2; // clog2(sizeof(int32_t)) 

    // load reused data once
    size_t max_len = f->max_clause_len;
    // restricted base assignment pointers to save a few cycles
    int32_t *restrict ns = a->num_satisfied;
    int32_t *restrict nu = a->num_unassigned;
    int8_t  *restrict vals = a->values;


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
    int32_t clause_idx = f->lit_row_off[lit_true];
    int32_t n = f->lit_row_off[lit_true + 1] - clause_idx;
    // [DEBUG] `DEBUG_PROP_ONE` lit_true start message
    for(size_t vl; n > 0; n -= vl, clause_idx += vl){
        vl = __riscv_vsetvl_e32m1(n);
        vuint32m1_t vclause = __riscv_vsll_vx_u32m1(__riscv_vle32_v_u32m1((const uint32_t *)(f->clause_col + clause_idx), vl), clause_shift, vl);
        vint32m1_t vn_sat = __riscv_vluxei32_v_i32m1(ns, vclause, vl);
        vint32m1_t vn_una = __riscv_vluxei32_v_i32m1(nu, vclause, vl);
        vn_sat = __riscv_vadd_vx_i32m1(vn_sat, 1, vl);
        vn_una = __riscv_vsub_vx_i32m1(vn_una, 1, vl);
        __riscv_vsuxei32_v_i32m1(ns,  vclause, vn_sat, vl);
        __riscv_vsuxei32_v_i32m1(nu, vclause, vn_una, vl);
        
        // sat_mask = (num_satisfied != 0)
        vbool32_t sat_mask = __riscv_vmsne_vx_i32m1_b32(vn_sat, 0, vl);
        // unit_mask = (num_unassigned == 1) && !sat_mask
        vbool32_t unit_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 1, vl), sat_mask, vl);
        size_t unit_count = __riscv_vcpop_m_b32(unit_mask, vl);
        // these are the lanes we actually care about finding BCP lit for
        // conflict_mask = (num_unassigned == 0) && !sat_mask
        vbool32_t conflict_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 0, vl), sat_mask, vl);
        // mark if conflict found - can't just break bc of trail issues?
        conflict_found |= __riscv_vcpop_m_b32(conflict_mask, vl);
        
        // if all clauses checked are satisified, OR no unit clauses are found, continue early
        if((__riscv_vcpop_m_b32(sat_mask, vl) == vl) || unit_count == 0) continue; 
        
        // need start and end literal idx - difference is #lits per clause 
        // alternatively, could use dense array again with different start spots
        
        // need to make clause*max_len vector to gather
        vuint32m1_t vclause_ml = __riscv_vmul_vx_u32m1(vclause, max_len, vl);

        vint32m1_t vunit = __riscv_vmv_v_x_i32m1(0, vl);
        for(size_t k = 0; k < max_len; k++){
            // load kth literal for each clause (gather from dense table)
            vint32m1_t vk_lit = __riscv_vluxei32_v_i32m1(f->clause_dense + k, vclause_ml, vl);
            // get variable index to access assignment ((lit/2) + 1)
            vint32m1_t vk_var = __riscv_vsra_vx_i32m1(vk_lit, 1, vl);
            vk_var            = __riscv_vadd_vx_i32m1(vk_var, 1, vl);

            // load assignment
            vint8mf4_t vk_val = __riscv_vluxei32_v_i8mf4(vals, __riscv_vreinterpret_v_i32m1_u32m1(vk_var), vl);
            
            // get boolean mask for vals == UNSET
            vbool32_t unset_mask = __riscv_vmseq_vx_i8mf4_b32(vk_val, VAR_UNSET, vl);

            // combine mask for unset with unit
            vbool32_t queue_mask = __riscv_vmand_mm_b32(unit_mask, unset_mask, vl);

            vunit = __riscv_vor_vv_i32m1_mu(queue_mask, vunit, vunit, vk_lit, vl);
            // [DEBUG] `DEBUG_PROP_ONE` dump vunit, unit_mask, unset_mask, queue_mask, vk_lit, vk_val
        }
        // vcompress 
        vint32m1_t vqueue = __riscv_vcompress_vm_i32m1(vunit, unit_mask, vl);
        // may need to vsetvl before this; might be handled by intrinsic
        __riscv_vse32_v_i32m1(q->buf + q->tail, vqueue, unit_count);
        q->tail += unit_count;
    }

    // clauses with literal lit_false
    clause_idx = f->lit_row_off[lit_false];
    n = f->lit_row_off[lit_false + 1] - clause_idx;
    // [DEBUG] `DEBUG_PROP_ONE` lit_false start mesage
    for(size_t vl; n > 0; n -= vl, clause_idx += vl){
        vl = __riscv_vsetvl_e32m1(n);
        vuint32m1_t vclause = __riscv_vsll_vx_u32m1(__riscv_vle32_v_u32m1((const uint32_t *)(f->clause_col + clause_idx), vl), clause_shift, vl);
        vint32m1_t vn_sat = __riscv_vluxei32_v_i32m1(ns, vclause, vl);
        vint32m1_t vn_una = __riscv_vluxei32_v_i32m1(nu, vclause, vl);
        // vn_sat = __riscv_vadd_vx_i32m1(vn_sat, 1, vl);
        vn_una = __riscv_vsub_vx_i32m1(vn_una, 1, vl);
        // __riscv_vsuxei32_v_i32m1(ns,  vclause, vn_sat, vl);
        __riscv_vsuxei32_v_i32m1(nu, vclause, vn_una, vl);
        
        vbool32_t sat_mask = __riscv_vmsne_vx_i32m1_b32(vn_sat, 0, vl);
        vbool32_t unit_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 1, vl), sat_mask, vl);
        size_t unit_count = __riscv_vcpop_m_b32(unit_mask, vl);        
        vbool32_t conflict_mask = __riscv_vmandn_mm_b32(__riscv_vmseq_vx_i32m1_b32(vn_una, 0, vl), sat_mask, vl);
        conflict_found |= __riscv_vcpop_m_b32(conflict_mask, vl);
        // if all clauses checked are satisified, OR no unit clauses are found, continue early
        if((__riscv_vcpop_m_b32(sat_mask, vl) == vl) || unit_count == 0) continue; 
        
        vuint32m1_t vclause_ml = __riscv_vmul_vx_u32m1(vclause, max_len, vl);
        vint32m1_t vunit = __riscv_vmv_v_x_i32m1(0, vl);
        for(size_t k = 0; k < max_len; k++){    // run the most
            vint32m1_t vk_lit = __riscv_vluxei32_v_i32m1(f->clause_dense + k, vclause_ml, vl);
            vint32m1_t vk_var = __riscv_vsra_vx_i32m1(vk_lit, 1, vl);
            vk_var            = __riscv_vadd_vx_i32m1(vk_var, 1, vl);
            vint8mf4_t vk_val = __riscv_vluxei32_v_i8mf4(vals, __riscv_vreinterpret_v_i32m1_u32m1(vk_var), vl);
            
            vbool32_t unset_mask = __riscv_vmseq_vx_i8mf4_b32(vk_val, VAR_UNSET, vl);
            vbool32_t queue_mask = __riscv_vmand_mm_b32(unit_mask, unset_mask, vl);

            vunit = __riscv_vor_vv_i32m1_mu(queue_mask, vunit, vunit, vk_lit, vl);
            // [DEBUG] `DEBUG_PROP_ONE` dump vunit, unit_mask, unset_mask, queue_mask, vk_lit, vk_val
        }
        // compress masked lanes to enable writing to queue more easily
        vint32m1_t vqueue = __riscv_vcompress_vm_i32m1(vunit, unit_mask, vl);
        // may need to vsetvl before this
        __riscv_vse32_v_i32m1(q->buf + q->tail, vqueue, unit_count);
        q->tail += unit_count;
    }
    return (conflict_found != 0) ? BCP_STEP_UNSAT : BCP_STEP_OK;
}

void bcp_rwnd_one(const Formula *f, Assignment *a, int32_t var){
    size_t clause_shift = 2; // clog2(sizeof(int32_t)) 

    int8_t val = a->values[var];
    if(val == VAR_UNSET) return;
    int32_t lit = encode_lit(var);
    int32_t sat_lit = (val == VAR_TRUE) ? lit : (lit ^ 1);
    int32_t uns_lit = (sat_lit ^ 1);

    a->values[var] = VAR_UNSET;
    a->lit_status[sat_lit] = 0x10;
    a->lit_status[uns_lit] = 0x10;

    // rewind affected clauses
    // clauses with literal sat_lit
    int32_t clause_idx = f->lit_row_off[sat_lit];
    int32_t n = f->lit_row_off[sat_lit + 1] - clause_idx;
    for(size_t vl; n > 0; n -= vl, clause_idx += vl){
        vl = __riscv_vsetvl_e32m1(n);
        vuint32m1_t vclause = __riscv_vsll_vx_u32m1(__riscv_vle32_v_u32m1((const uint32_t *)(f->clause_col + clause_idx), vl), clause_shift, vl);
        vint32m1_t vn_sat = __riscv_vluxei32_v_i32m1(a->num_satisfied, vclause, vl);
        vint32m1_t vn_una = __riscv_vluxei32_v_i32m1(a->num_unassigned, vclause, vl);
        vn_sat = __riscv_vsub_vx_i32m1(vn_sat, 1, vl);
        vn_una = __riscv_vadd_vx_i32m1(vn_una, 1, vl);
        __riscv_vsuxei32_v_i32m1(a->num_satisfied,  vclause, vn_sat, vl);
        __riscv_vsuxei32_v_i32m1(a->num_unassigned, vclause, vn_una, vl);
    }
    // clauses with literal uns_lit
    clause_idx = f->lit_row_off[uns_lit];
    n = f->lit_row_off[uns_lit + 1] - clause_idx;
    for(size_t vl; n > 0; n -= vl, clause_idx += vl){
        vl = __riscv_vsetvl_e32m1(n);
        vuint32m1_t vclause = __riscv_vsll_vx_u32m1(__riscv_vle32_v_u32m1((const uint32_t *)(f->clause_col + clause_idx), vl), clause_shift, vl);
        vint32m1_t vn_una = __riscv_vluxei32_v_i32m1(a->num_unassigned, vclause, vl);
        vn_una = __riscv_vadd_vx_i32m1(vn_una, 1, vl);
        __riscv_vsuxei32_v_i32m1(a->num_unassigned, vclause, vn_una, vl);
    }
}