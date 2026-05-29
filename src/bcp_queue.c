// bcp_queue.c
#include "bcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void bcp_queue_init(bcp_queue *q, size_t capacity) {
    q->buf = malloc(capacity * sizeof(int32_t));
    if (!q->buf) {
        fprintf(stderr, "bcp_queue_init: allocation failed (capacity=%zu)\n",
                capacity);
        exit(1);
    }
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
}

void bcp_queue_free(bcp_queue *q) {
    if (!q) return;
    free(q->buf);
    q->buf = NULL;
    q->capacity = 0;
    q->head = 0;
    q->tail = 0;
}

// Reset to empty without freeing memory. Called at the start of each
// bcp_run invocation so we reuse the same allocation across many calls.
void bcp_queue_reset(bcp_queue *q) {
    q->head = 0;
    q->tail = 0;
}

void bcp_queue_push(bcp_queue *q, int32_t lit) {
    // Capacity is sized to num_vars, which bounds the number of literals
    // we could ever enqueue in one BCP run (each variable can be forced
    // at most once before being assigned). If we hit the cap, something
    // is wrong — likely a literal was enqueued multiple times due to a
    // missing dedup check upstream.
    assert(q->tail < q->capacity && "bcp_queue overflow");
    q->buf[q->tail++] = lit;
}

int32_t bcp_queue_pop(bcp_queue *q) {
    assert(q->head < q->tail && "bcp_queue_pop on empty queue");
    return q->buf[q->head++];
}

int bcp_queue_empty(const bcp_queue *q) {
    return q->head >= q->tail;
}