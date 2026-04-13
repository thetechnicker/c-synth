#ifndef SYNTH_H
#define SYNTH_H

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* ─── Constants ─────────────────────────────────────────────────────────── */

#define SR 48000
#define CH 2
#define BLOCK 128*4    /* samples per callback block — keep power of 2     */
#define NUM_VOICES 8 /* compile-time polyphony knob — change freely       */

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
    float a;             /* filter coefficient, recomputed from cutoff */
    float z;             /* one sample of state                        */
    float cutoff_hz;     /* current (smoothed) cutoff                  */
    float cutoff_target; /* requested cutoff (set atomically from MIDI)*/
    float cutoff_smooth; /* one-pole smoother coeff for cutoff itself  */
} LpFilter;

/* ─── Voice ──────────────────────────────────────────────────────────────── */

/**
 * One independent synthesis voice: oscillator + filter + allocation metadata.
 *
 * Lives inside Pipeline.  All fields are written and read exclusively by the
 * RT callback (voice_allocate / voice_release / render loop), so no locking
 * is required.
 */
typedef struct {
    Osc osc;
    LpFilter filter;
    uint8_t note;   /* MIDI note number 0–127                            */
    float velocity; /* normalised 0–1; used as stealing priority key     */
    uint32_t age;   /* incremented on each note-on; lower = older        */
    bool active;    /* false = slot is free for allocation               */
} Voice;

/* ─── Pipeline ───────────────────────────────────────────────────────────── */

/**
 * A complete, self-contained signal chain with NUM_VOICES independent voices.
 *
 * The manager thread allocates and initialises one of these, then publishes
 * it via g_pipeline.  The RT thread reads it every block via an atomic
 * acquire-load.  All per-voice DSP state (phase, filter z, cutoff_hz) is
 * written back by the RT thread at the end of each block.
 *
 * The pre-allocated stereo output block lives here as a mix accumulator so
 * the RT callback never needs to call malloc.
 */
typedef struct Pipeline {
    Voice voices[NUM_VOICES];
    float block[BLOCK * CH]; /* mix accumulator — zeroed at top of each block */
} Pipeline;

/* ─── Inter-thread communication ─────────────────────────────────────────── */

/**
 * Patch command sent from the main thread to the manager thread.
 * These are defaults applied to newly allocated voices; active, playing
 * voices are not retroactively modified to avoid mid-chord surprises.
 */
typedef struct {
    float osc_freq;      /* Hz   — default frequency for new voices */
    float osc_amp;       /* 0–1  — default amplitude scalar         */
    float filter_cutoff; /* Hz   — initial cutoff for new voices    */
} PatchCmd;

/* ─── Globals shared between manager and RT threads ─────────────────────── */

extern _Atomic(Pipeline *) g_pipeline;

/* ─── Entry point ────────────────────────────────────────────────────────── */

/** Top-level thread function.  Spawns the manager, opens the SDL audio
 *  stream (RT callback), then parks until thread_stop() is called. */
int synth_thread(void *data);

/**
 * Send a patch-change command from the main thread to the manager.
 * Non-blocking — drops the command silently if the ring is full.
 */
void synth_send_patch(float osc_freq, float osc_amp, float filter_cutoff);

#endif /* SYNTH_H */
