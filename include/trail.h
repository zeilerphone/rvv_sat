// trail.h
#include <stddef.h> // size_t
#include <stdint.h> // int32_t, int8_t

#include "cnf.h"

#ifndef RVV_TRAIL_H
#define RVV_TRAIL_H

typedef struct Trail {
    int32_t *vars;       // [capacity], stack of assigned variable indices
                         // (1-based, matching values[] indexing)
    size_t   capacity;
    size_t   top;        // index of next slot to push to
} Trail;

void   trail_init  (Trail *t, size_t capacity);
void   trail_free  (Trail *t);
void   trail_push  (Trail *t, int32_t v);
void   trail_unwind(Trail *t, size_t mark,
                    const Formula *f, Assignment *a);  // pops + unassigns

static inline size_t trail_mark(const Trail *t) {
    return t->top;
}
static inline int   trail_empty(const Trail *t) {
    return t->top == 0;
}

#endif // RVV_TRAIL_H