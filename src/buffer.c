/**
 * buffer.c — implementation of the opaque frame-based ring buffer.
 *
 * Threading model
 * ---------------
 * buffer_write and buffer_read are designed for one concurrent writer and
 * one concurrent reader (single-producer / single-consumer).
 *
 * The mutex guards ONLY pointer and counter advancement.  The actual
 * memcpy of frame data happens outside the lock because at the moment of
 * copying each thread owns its frame exclusively (the overlap invariant
 * guarantees pw..pw+frame and pr..pr+frame never coincide).
 *
 * Pointer arithmetic
 * ------------------
 * pw and pr are element indices into data[].
 * Advancement: idx = (idx + frame) % size
 * This is valid only when size % frame == 0, which is asserted in
 * buffer_create and checked in buffer_resize.
 *
 * Full / empty
 * ------------
 * frames_used tracks how many frames are currently readable.
 *   0              => empty
 *   size/frame     => full
 * This avoids the classic pw==pr ambiguity.
 */

#include "buffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_mutex.h>

/* ------------------------------------------------------------------ */
/* Internal struct definition (hidden from callers)                    */
/* ------------------------------------------------------------------ */

struct buffer_t {
    float *data;        /* heap-allocated sample storage          */
    size_t size;        /* capacity in float elements             */
    size_t frame;       /* elements per frame                     */
    SDL_Mutex *mut;     /* guards pw, pr, frames_used             */
    size_t pw;          /* write-pointer: element index           */
    size_t pr;          /* read-pointer:  element index           */
    size_t frames_used; /* readable frames currently buffered     */
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/** Advance an element index by one frame, wrapping at size. */
static inline size_t advance(size_t idx, size_t frame, size_t size) { return (idx + frame) % size; }

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

buffer_t *buffer_create(size_t size, size_t frame) {
    if (frame == 0 || size == 0 || size % frame != 0)
        return NULL;

    buffer_t *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;

    b->data = malloc(size * sizeof(float));
    if (!b->data) {
        free(b);
        return NULL;
    }

    b->mut = SDL_CreateMutex();
    if (!b->mut) {
        free(b->data);
        free(b);
        return NULL;
    }

    b->size = size;
    b->frame = frame;
    b->pw = 0;
    b->pr = 0;
    b->frames_used = 0;

    return b;
}

void buffer_destroy(buffer_t *b) {
    if (!b)
        return;

    SDL_DestroyMutex(b->mut);
    free(b->data);
    free(b);
}

bool buffer_write(buffer_t *b, const float *src) {
    assert(b && src);

    bool dropped = false;

    SDL_LockMutex(b->mut);

    size_t max_frames = b->size / b->frame;

    if (b->frames_used == max_frames) {
        /* Buffer full — drop the oldest frame by advancing pr. */
        b->pr = advance(b->pr, b->frame, b->size);
        b->frames_used--;
        dropped = true;
    }

    /* Snapshot the write position while holding the lock, then release
     * before the memcpy so the reader is not blocked during the copy. */
    size_t write_at = b->pw;
    b->pw = advance(b->pw, b->frame, b->size);
    b->frames_used++;

    SDL_UnlockMutex(b->mut);

    /* Safe to copy without the lock: write_at frame is exclusively ours. */
    memcpy(b->data + write_at, src, b->frame * sizeof(float));

    return !dropped;
}

bool buffer_read(buffer_t *b, float *dst) {
    assert(b && dst);

    SDL_LockMutex(b->mut);

    if (b->frames_used == 0) {
        SDL_UnlockMutex(b->mut);
        return false;
    }

    /* Snapshot the read position, advance pr, then release before copy. */
    size_t read_at = b->pr;
    b->pr = advance(b->pr, b->frame, b->size);
    b->frames_used--;

    SDL_UnlockMutex(b->mut);

    /* Safe to copy without the lock: read_at frame is exclusively ours. */
    memcpy(dst, b->data + read_at, b->frame * sizeof(float));

    return true;
}

frame_t buffer_get_frame(buffer_t *b) {
    assert(b);

    SDL_LockMutex(b->mut);

    if (b->frames_used == 0) {
        SDL_UnlockMutex(b->mut);
        return (frame_t){.data = NULL, .len = 0};
    }
    /* Snapshot the read position, advance pr, then release before copy. */
    size_t read_at = b->pr;
    b->pr = advance(b->pr, b->frame, b->size);
    b->frames_used--;

    SDL_UnlockMutex(b->mut);
    return (frame_t){.data = b->data + read_at, .len = b->frame * sizeof(float)};
}

size_t buffer_frames_available(const buffer_t *b) {
    assert(b);

    SDL_LockMutex(b->mut);
    size_t n = b->frames_used;
    SDL_UnlockMutex(b->mut);

    return n;
}

void buffer_reset(buffer_t *b) {
    assert(b);

    SDL_LockMutex(b->mut);
    b->pw = 0;
    b->pr = 0;
    b->frames_used = 0;
    SDL_UnlockMutex(b->mut);
}

bool buffer_resize(buffer_t *b, size_t new_size) {
    assert(b);

    if (new_size == 0 || new_size % b->frame != 0)
        return false;

    size_t new_max_frames = new_size / b->frame;

    float *new_data = malloc(new_size * sizeof(float));
    if (!new_data)
        return false;

    SDL_LockMutex(b->mut);

    /* Drain readable frames into new_data in arrival order.
     * If new capacity is smaller, oldest frames are silently discarded. */
    size_t to_copy = b->frames_used;
    if (to_copy > new_max_frames)
        to_copy = new_max_frames;

    /* Skip (frames_used - to_copy) oldest frames — discard them. */
    size_t skip = b->frames_used - to_copy;
    size_t old_pr = b->pr;
    for (size_t i = 0; i < skip; i++)
        old_pr = advance(old_pr, b->frame, b->size);

    /* Copy retained frames sequentially into new_data. */
    for (size_t i = 0; i < to_copy; i++) {
        memcpy(new_data + i * b->frame, b->data + old_pr, b->frame * sizeof(float));
        old_pr = advance(old_pr, b->frame, b->size);
    }

    free(b->data);
    b->data = new_data;
    b->size = new_size;
    b->pr = 0;
    b->pw = to_copy * b->frame; /* directly after last frame  */
    b->frames_used = to_copy;

    /* pw wraps to 0 when the buffer is exactly full after resize. */
    if (b->pw == new_size)
        b->pw = 0;

    SDL_UnlockMutex(b->mut);

    return true;
}
