// bcp.h
#include <stddef.h> // size_t
#include <stdint.h> // int32_t, int8_t

#include "cnf.h"
#include "trail.h"

#ifndef RVV_SAT_BCP_H
#define RVV_SAT_BCP_H

enum bcp_step_status {
    BCP_STEP_OK,
    BCP_STEP_UNSAT
};

enum bcp_status {
    BCP_SAT,
    BCP_UNSAT,
    BCP_UNDETERMINED
};

// queue of lits in 0-indexed form
typedef struct {
    int32_t *buf;       // [capacity], queue of unit clause literals
                        // (0-indexed, matching lit_status[] indexing)
    size_t   capacity;
    size_t   head;
    size_t   tail;
} bcp_queue;

void    bcp_queue_init(bcp_queue *q, size_t capacity);
void    bcp_queue_free(bcp_queue *q);
void    bcp_queue_reset(bcp_queue *q);   // head = tail = 0
void    bcp_queue_push(bcp_queue *q, int32_t lit);
int32_t bcp_queue_pop(bcp_queue *q);
int     bcp_queue_empty(const bcp_queue *q);

static inline size_t bcp_queue_required_capacity(const Formula *f) {
    return f->num_clauses + f->num_vars;
}

void                 bcp_init(const Formula *f, Assignment *a);
enum bcp_status      bcp_run(const Formula *f, Assignment *a, bcp_queue *q, Trail *t);
enum bcp_status      bcp_drain(const Formula *f, Assignment *a, bcp_queue *q, Trail *t);
enum bcp_step_status bcp_prop_one(const Formula *f, Assignment *a, bcp_queue *q, Trail *t,
                            int32_t internal_lit); // 0-indexed
void                 bcp_rwnd_one(const Formula *f, Assignment *a, 
                            int32_t var); // 1-based, signed


#endif //RVV_SAT_BCP_H
// void                    bcp_rwnd_one(const Formula *f, Assignment *a,);
