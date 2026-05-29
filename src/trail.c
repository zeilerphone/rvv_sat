// trail.c
#include <stdlib.h>     // malloc, free
#include <assert.h>

#include "trail.h"
#include "bcp.h"

void trail_init(Trail *t, size_t capacity){
    t->vars = malloc(capacity * sizeof(int32_t));
    if (!t->vars) {
        fprintf(stderr, "trail_init: allocation failed (capacity%zu)\n", capacity);
        exit(1);
    }
    t->capacity = capacity;
    t->top = 0;
}

void trail_free(Trail *t) {
    if (!t) return;
    free(t->vars);
    t->vars = NULL;
    t->capacity = 0;
    t->top = 0;
}

void trail_push(Trail *t, int32_t var) {
    assert(t->top < t->capacity && "trail overflow");
    t->vars[t->top++] = var;
}

void trail_unwind(Trail *t, size_t mark, const Formula *f, Assignment *a){
    while(t->top > mark){
        t->top--;
        int32_t var = t->vars[t->top];
        bcp_rwnd_one(f, a, var);
    }
}
