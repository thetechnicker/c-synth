#ifndef SYNTH_H
#define SYNTH_H

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <stdatomic.h>

/* ─── Constants ─────────────────────────────────────────────────────────── */

#define SR    48000
#define CH    2
#define BLOCK 256 /* samples per callback block — keep power of 2 */

/* Grace period after an atomic pipeline swap before the old pipeline is
 * freed.  Two block-lengths at SR gives the RT thread time to finish any
 * in-progress render with the old pointer. */
#define PIPELINE_GRACE_MS ((BLOCK * 1000 * 2) / SR + 1)

/* ─── DSP building blocks ────────────────────────────────────────────────── */

/** Band-limited sine oscillator (phase accumulator). */
typedef struct {
    float phase; /* 0.0 – 1.0 normalised */
    float freq;
    float amp;
    float sr;
} Osc;

/**
 * One-pole lowpass filter  y[n] = a*x[n] + (1-a)*y[n-1]
 *
 * cutoff_target is written by the RT thread when it applies a MIDI CC;
 * the smoother inside the render loop prevents zipper noise.
 */
typedef struct {
    float a;              /* filter coefficient, recomputed from cutoff */
    float z;              /* one sample of state                        */
    float cutoff_hz;      /* current (smoothed) cutoff                  */
    float cutoff_target;  /* requested cutoff (set atomically from MIDI)*/
    float cutoff_smooth;  /* one-pole smoother coeff for cutoff itself  */
} LpFilter;

/* ─── Pipeline ───────────────────────────────────────────────────────────── */

/**
 * A complete, self-contained signal chain.
 *
 * The manager thread allocates and initialises one of these, then publishes
 * it via g_pipeline.  The RT thread reads it every block via an atomic
 * acquire-load and never modifies the struct layout — only the DSP state
 * fields (phase, z, cutoff_hz) inside.
 *
 * The pre-allocated stereo output block lives here so the RT callback never
 * needs to call malloc.
 */
typedef struct Pipeline {
    Osc      osc;
    LpFilter filter;
    float    block[BLOCK * CH]; /* pre-allocated render scratch buffer */
} Pipeline;

/* ─── Inter-thread communication ─────────────────────────────────────────── */

/**
 * Patch command sent from the main thread to the manager thread via a
 * second (non-RT) SPSC ring.  Add more fields as the synth grows.
 */
typedef struct {
    float osc_freq;       /* Hz   */
    float osc_amp;        /* 0–1  */
    float filter_cutoff;  /* Hz   */
} PatchCmd;

/* ─── Globals shared between manager and RT threads ─────────────────────── */

/*
 * Atomic pipeline pointer.
 *
 * Manager: atomic_store(..., memory_order_release)  after full init.
 * RT:      atomic_load (..., memory_order_acquire)  at top of every block.
 *
 * Declared extern so synth.c owns the definition; other TUs can read it
 * if needed (e.g. a future visualiser thread), but should treat it as
 * read-only outside synth.c.
 */
extern _Atomic(Pipeline *) g_pipeline;

/* ─── Entry point ────────────────────────────────────────────────────────── */

/** Top-level thread function.  Spawns the manager, opens the SDL audio
 *  stream (RT callback), then parks until thread_stop() is called. */
int synth_thread(void *data);

/**
 * Send a patch-change command from the main thread to the manager.
 * Non-blocking — drops the command silently if the ring is full.
 * The manager will rebuild the pipeline asynchronously; audio continues
 * uninterrupted until the new pipeline is swapped in.
 */
void synth_send_patch(float osc_freq, float osc_amp, float filter_cutoff);

#endif /* SYNTH_H */
