/**
 * buffer.h — opaque frame-based ring buffer
 *
 * Thread safety:
 *   buffer_write and buffer_read are safe to call concurrently from two
 *   threads.  All other functions must be called from a single thread or
 *   with external synchronisation.
 *
 * Drop policy:
 *   On a full write the oldest readable frame is silently discarded so
 *   that the incoming frame is always stored.  buffer_read returns false
 *   when no frame is available.
 *
 * Units:
 *   `size` and `frame` are always counts of float elements, never bytes.
 *   `size` must be a non-zero multiple of `frame`.
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — callers may not inspect or modify fields directly. */
typedef struct buffer_t buffer_t;
typedef struct {
    float *data;
    size_t len;
} frame_t;

/**
 * buffer_create — allocate and initialise a ring buffer.
 *
 * @param size   Total capacity in float elements.  Must be a non-zero
 *               multiple of @p frame.
 * @param frame  Number of float elements per frame.  Must be > 0.
 *
 * @return  Pointer to a new buffer_t, or NULL on allocation failure or
 *          invalid arguments.
 */
buffer_t *buffer_create(size_t size, size_t frame);

/**
 * buffer_destroy — release all resources owned by @p b.
 *
 * Passing NULL is a no-op.  The pointer must not be used after this call.
 */
void buffer_destroy(buffer_t *b);

/**
 * buffer_write — copy one frame from @p src into the buffer.
 *
 * If the buffer is full the oldest frame is dropped to make room.
 * @p src must point to at least `frame` floats.
 *
 * @return  true  — frame stored (no drop).
 *          false — frame stored after dropping one old frame.
 */
bool buffer_write(buffer_t *b, const float *src);

/**
 * buffer_read — copy the oldest frame from the buffer into @p dst.
 *
 * @p dst must point to at least `frame` floats.
 *
 * @return  true  — frame copied successfully.
 *          false — buffer was empty, @p dst is unchanged.
 */
bool buffer_read(buffer_t *b, float *dst);
frame_t buffer_get_frame(buffer_t *b);

/**
 * buffer_frames_available — number of frames ready to be read.
 */
size_t buffer_frames_available(const buffer_t *b);

/**
 * buffer_reset — discard all buffered data.
 *
 * Read and write pointers are reset to the start of the internal array.
 * Existing float data is NOT zeroed.
 */
void buffer_reset(buffer_t *b);

/**
 * buffer_resize — change the total capacity to @p new_size elements.
 *
 * @p new_size must be a non-zero multiple of the existing frame size.
 * Readable frames are preserved in arrival order up to the new capacity;
 * excess old frames (oldest first) are discarded.
 *
 * @return  true on success, false if allocation fails or @p new_size is
 *          invalid (the buffer is left unchanged on failure).
 */
bool buffer_resize(buffer_t *b, size_t new_size);

#ifdef __cplusplus
}
#endif

#endif /* BUFFER_H */
