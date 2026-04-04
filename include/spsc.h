#ifndef SPSC_H
#define SPSC_H

/*
 * spsc.h — single-producer / single-consumer lock-free ring buffer.
 *
 * Safe for exactly ONE writer thread and ONE reader thread simultaneously.
 * Uses C11 stdatomic acquire/release — no mutex, no spin, no alloc.
 *
 * Capacity is SPSC_CAPACITY - 1 usable slots (one slot kept empty to
 * distinguish full from empty without a separate counter).
 *
 * Producer:  spsc_push(&ring, &ev)   → returns 1 on success, 0 if full
 * Consumer:  spsc_pop(&ring, &ev)    → returns 1 on success, 0 if empty
 */

#include "portmidi_helper.h"
#include <stdatomic.h>

#define SPSC_CAPACITY 256u /* must be a power of 2 */
#define SPSC_MASK     (SPSC_CAPACITY - 1u)

typedef struct {
    PmhMidiEvent buf[SPSC_CAPACITY];
    /* Each counter on its own cache line to prevent false sharing
     * between producer (writes head) and consumer (writes tail). */
    _Alignas(64) atomic_uint head; /* bumped by producer after write */
    _Alignas(64) atomic_uint tail; /* bumped by consumer after read  */
} SpscRing;

static inline void spsc_init(SpscRing *r) {
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
}

/* Push — call from the PRODUCER (manager) thread only. */
static inline int spsc_push(SpscRing *r, const PmhMidiEvent *ev) {
    unsigned h    = atomic_load_explicit(&r->head, memory_order_relaxed);
    unsigned next = (h + 1u) & SPSC_MASK;
    /* Full when next would lap the tail. */
    if (next == atomic_load_explicit(&r->tail, memory_order_acquire))
        return 0; /* drop — ring full */
    r->buf[h] = *ev;
    /* Release: buf[h] write must be visible before head advances. */
    atomic_store_explicit(&r->head, next, memory_order_release);
    return 1;
}

/* Pop — call from the CONSUMER (RT callback) thread only. */
static inline int spsc_pop(SpscRing *r, PmhMidiEvent *ev_out) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    /* Acquire: see the buf write the producer released. */
    if (t == atomic_load_explicit(&r->head, memory_order_acquire))
        return 0; /* empty */
    *ev_out = r->buf[t];
    atomic_store_explicit(&r->tail, (t + 1u) & SPSC_MASK, memory_order_release);
    return 1;
}

#endif /* SPSC_H */
