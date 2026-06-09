// bcp_rvv_xorsig.c — XOR-signature BCP for RISC-V Vector
//
// Per-clause invariant: g_sig[c] = XOR of all literals falsified for clause c
// so far.  When num_unassigned[c]==1 and num_satisfied[c]==0, g_sig[c] is the
// one remaining unset literal — no k-loop needed to find it.
//
// sat/una semantics match bcp_scalar.c exactly:
//   lit_true  → sat++, una--   (no sig update); combined as sat_una += 0xFFFF
//   lit_false → una--, sig ^= lit_false  (unit/conflict gated by sat==0)
//   rwnd sat_lit → sat--, una++   (no sig update); combined as sat_una -= 0xFFFF
//   rwnd uns_lit → una++, sig ^= uns_lit

#include <riscv_vector.h>
#include <stdlib.h>
#include "bcp.h"

static int32_t *g_sig = NULL;   // [num_clauses]

void bcp_init(const Formula *f, Assignment *a) {
    // Init sat_una: sat=0, una=clause_length. Since pack_sat_una(0,len)=len
    // for len < 65536, we just store vlen directly.
    size_t n           = f->num_clauses;
    int32_t *su        = a->sat_una;
    int32_t *c_row_off = f->clause_row_off;
    for (size_t vl; n > 0; n -= vl, su += vl, c_row_off += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vint32m2_t vlen = __riscv_vsub_vv_i32m2(
            __riscv_vle32_v_i32m2(c_row_off + 1, vl),
            __riscv_vle32_v_i32m2(c_row_off,     vl), vl);
        __riscv_vse32_v_i32m2(su, vlen, vl);
    }

    // Allocate and init g_sig from CSR to avoid XOR'ing LIT_PAD=0 padding.
    free(g_sig);
    g_sig = malloc(f->num_clauses * sizeof(int32_t));
    for (size_t c = 0; c < f->num_clauses; c++) {
        int32_t sig = 0;
        for (int32_t k = f->clause_row_off[c]; k < f->clause_row_off[c + 1]; k++)
            sig ^= f->lit_col[k];
        g_sig[c] = sig;
    }
}

// bcp_run: unit-stride scan; g_sig replaces the k-loop literal search.
enum bcp_status bcp_run(const Formula *f, Assignment *a, bcp_queue *q, Trail *t) {
    bcp_queue_reset(q);

    size_t   n      = f->num_clauses;
    int32_t *su     = a->sat_una;
    int32_t *sigp   = g_sig;
    int32_t  all_sat = 1;

    for (size_t vl; n > 0; n -= vl, su += vl, sigp += vl) {
        vl = __riscv_vsetvl_e32m2(n);

        vint32m2_t vsu  = __riscv_vle32_v_i32m2(su, vl);
        vint32m2_t vsat = __riscv_vsra_vx_i32m2(vsu, 16, vl);
        vint32m2_t vuna = __riscv_vand_vx_i32m2(vsu, 0xFFFF, vl);

        vbool16_t sat_mask = __riscv_vmsne_vx_i32m2_b16(vsat, 0, vl);
        if (__riscv_vcpop_m_b16(sat_mask, vl) == vl) continue;

        all_sat = 0;

        vbool16_t conflict_mask = __riscv_vmandn_mm_b16(
            __riscv_vmseq_vx_i32m2_b16(vuna, 0, vl), sat_mask, vl);
        if (__riscv_vcpop_m_b16(conflict_mask, vl) > 0) return BCP_UNSAT;

        vbool16_t unit_mask = __riscv_vmandn_mm_b16(
            __riscv_vmseq_vx_i32m2_b16(vuna, 1, vl), sat_mask, vl);
        size_t unit_count = __riscv_vcpop_m_b16(unit_mask, vl);
        if (unit_count == 0) continue;

        // g_sig[c] is the unit literal — no k-loop.
        vint32m2_t vsig   = __riscv_vle32_v_i32m2(sigp, vl);
        vint32m2_t vqueue = __riscv_vcompress_vm_i32m2(vsig, unit_mask, vl);
        __riscv_vse32_v_i32m2(q->buf + q->tail, vqueue, unit_count);
        q->tail += unit_count;
    }

    if (all_sat == 1) return BCP_SAT;
    return bcp_drain(f, a, q, t);
}

enum bcp_status bcp_drain(const Formula *f, Assignment *a, bcp_queue *q, Trail *t) {
    while (!bcp_queue_empty(q)) {
        int32_t lit = bcp_queue_pop(q);
        if (bcp_prop_one(f, a, q, t, lit) == BCP_STEP_UNSAT) return BCP_UNSAT;
    }
    int32_t *su = a->sat_una;
    size_t n = f->num_clauses;
    for (size_t vl; n > 0; n -= vl, su += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vbool16_t zero_mask = __riscv_vmseq_vx_i32m2_b16(
            __riscv_vsra_vx_i32m2(__riscv_vle32_v_i32m2(su, vl), 16, vl), 0, vl);
        if (__riscv_vcpop_m_b16(zero_mask, vl) > 0) return BCP_UNDETERMINED;
    }
    return BCP_SAT;
}

enum bcp_step_status bcp_prop_one(const Formula *f, Assignment *a, bcp_queue *q, Trail *t, int32_t internal_lit) {
    const size_t clause_shift = 2;   // log2(sizeof(int32_t))

    int32_t var          = (internal_lit >> 1) + 1;
    int32_t var_polarity = (internal_lit & 1) ? VAR_FALSE : VAR_TRUE;

    if (a->values[var] ==  var_polarity) return BCP_STEP_OK;
    if (a->values[var] == -var_polarity) return BCP_STEP_UNSAT;

    int32_t lit_true  = internal_lit;
    int32_t lit_false = internal_lit ^ 1;
    a->values[var] = var_polarity;
    a->lit_status[lit_true]  = 0x01;
    a->lit_status[lit_false] = 0x00;
    trail_push(t, var);

    int conflict_found = 0;

    // --- lit_true path: sat++, una-- (same as scalar; no sig update) ---
    int32_t clause_idx = f->lit_row_off[lit_true];
    int32_t n = f->lit_row_off[lit_true + 1] - clause_idx;
    for (size_t vl; n > 0; n -= vl, clause_idx += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vuint32m2_t voff = __riscv_vsll_vx_u32m2(
            __riscv_vle32_v_u32m2((const uint32_t *)(f->clause_col + clause_idx), vl),
            clause_shift, vl);
        vint32m2_t vsu = __riscv_vluxei32_v_i32m2(a->sat_una, voff, vl);
        vsu = __riscv_vadd_vx_i32m2(vsu, SAT_INC_UNA_DEC, vl);
        __riscv_vsuxei32_v_i32m2(a->sat_una, voff, vsu, vl);
    }

    // --- lit_false path: una--, sig ^= lit_false; detect unit/conflict ---
    clause_idx = f->lit_row_off[lit_false];
    n = f->lit_row_off[lit_false + 1] - clause_idx;
    for (size_t vl; n > 0; n -= vl, clause_idx += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vuint32m2_t voff = __riscv_vsll_vx_u32m2(
            __riscv_vle32_v_u32m2((const uint32_t *)(f->clause_col + clause_idx), vl),
            clause_shift, vl);

        vint32m2_t vsu  = __riscv_vluxei32_v_i32m2(a->sat_una, voff, vl);
        vint32m2_t vsig = __riscv_vluxei32_v_i32m2(g_sig,      voff, vl);

        // Unconditional update (matches scalar's una decrement and sig xor in lit_false).
        vint32m2_t vsu_new  = __riscv_vsub_vx_i32m2(vsu,  1,         vl);  // una--
        vint32m2_t vsig_new = __riscv_vxor_vx_i32m2(vsig, lit_false, vl);

        __riscv_vsuxei32_v_i32m2(a->sat_una, voff, vsu_new,  vl);
        __riscv_vsuxei32_v_i32m2(g_sig,      voff, vsig_new, vl);

        vint32m2_t vsat = __riscv_vsra_vx_i32m2(vsu_new, 16,     vl);
        vint32m2_t vuna = __riscv_vand_vx_i32m2(vsu_new, 0xFFFF, vl);

        vbool16_t sat_mask      = __riscv_vmsne_vx_i32m2_b16(vsat, 0, vl);
        vbool16_t unit_mask     = __riscv_vmandn_mm_b16(
            __riscv_vmseq_vx_i32m2_b16(vuna, 1, vl), sat_mask, vl);
        vbool16_t conflict_mask = __riscv_vmandn_mm_b16(
            __riscv_vmseq_vx_i32m2_b16(vuna, 0, vl), sat_mask, vl);

        conflict_found |= (int)__riscv_vcpop_m_b16(conflict_mask, vl);

        size_t unit_count = __riscv_vcpop_m_b16(unit_mask, vl);
        if (unit_count == 0) continue;

        // vsig_new at unit lanes IS the unit literal — no k-loop.
        vint32m2_t vqueue = __riscv_vcompress_vm_i32m2(vsig_new, unit_mask, vl);
        __riscv_vse32_v_i32m2(q->buf + q->tail, vqueue, unit_count);
        q->tail += unit_count;
    }

    return conflict_found ? BCP_STEP_UNSAT : BCP_STEP_OK;
}

void bcp_rwnd_one(const Formula *f, Assignment *a, int32_t var) {
    const size_t cshift = 2;

    int32_t val = a->values[var];
    if (val == VAR_UNSET) return;
    int32_t lit     = encode_lit(var);
    int32_t sat_lit = (val == VAR_TRUE) ? lit : (lit ^ 1);
    int32_t uns_lit = sat_lit ^ 1;

    a->values[var]          = VAR_UNSET;
    a->lit_status[sat_lit]  = 0x10;
    a->lit_status[uns_lit]  = 0x10;

    // sat_lit path: undo satisfaction (sat--, una++; sig unchanged).
    int32_t cidx = f->lit_row_off[sat_lit];
    int32_t n    = f->lit_row_off[sat_lit + 1] - cidx;
    for (size_t vl; n > 0; n -= vl, cidx += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vuint32m2_t voff = __riscv_vsll_vx_u32m2(
            __riscv_vle32_v_u32m2((const uint32_t *)(f->clause_col + cidx), vl),
            cshift, vl);
        vint32m2_t vsu = __riscv_vluxei32_v_i32m2(a->sat_una, voff, vl);
        vsu = __riscv_vsub_vx_i32m2(vsu, SAT_INC_UNA_DEC, vl);
        __riscv_vsuxei32_v_i32m2(a->sat_una, voff, vsu, vl);
    }

    // uns_lit path: undo falsification (una++, sig ^= uns_lit; self-inverse).
    cidx = f->lit_row_off[uns_lit];
    n    = f->lit_row_off[uns_lit + 1] - cidx;
    for (size_t vl; n > 0; n -= vl, cidx += vl) {
        vl = __riscv_vsetvl_e32m2(n);
        vuint32m2_t voff = __riscv_vsll_vx_u32m2(
            __riscv_vle32_v_u32m2((const uint32_t *)(f->clause_col + cidx), vl),
            cshift, vl);
        vint32m2_t vsu  = __riscv_vluxei32_v_i32m2(a->sat_una, voff, vl);
        vint32m2_t vsig = __riscv_vluxei32_v_i32m2(g_sig,      voff, vl);
        vsu  = __riscv_vadd_vx_i32m2(vsu, 1,        vl);  // una++
        vsig = __riscv_vxor_vx_i32m2(vsig, uns_lit, vl);
        __riscv_vsuxei32_v_i32m2(a->sat_una, voff, vsu,  vl);
        __riscv_vsuxei32_v_i32m2(g_sig,      voff, vsig, vl);
    }
}
