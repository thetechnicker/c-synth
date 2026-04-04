#include "synth.h"
#include "audio_setup.h"
#include "dspmath.h"
#include "log.h"
#include "portmidi_helper.h"
#include "spsc.h"
#include "thread.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Globals
 * ═════════════════════════════════════════════════════════════════════════ */

/* Atomic pipeline pointer — RT thread reads, manager thread writes. */
_Atomic(Pipeline *) g_pipeline = NULL;

/*
 * MIDI ring: manager thread pushes, RT callback pops.
 * Patch-command ring: main thread pushes, manager thread pops.
 */
static SpscRing s_midi_ring;
static SpscRing s_patch_ring; /* reuses PmhMidiEvent slot via PatchCmd cast — see below */

/*
 * We need a second SPSC ring for PatchCmd (main → manager).
 * PatchCmd is smaller than PmhMidiEvent so we can safely reuse SpscRing's
 * buf storage by casting.  A cleaner project would template the ring or
 * define a union; for now a dedicated ring avoids that complexity.
 */
typedef struct {
    PatchCmd buf[SPSC_CAPACITY];
    _Alignas(64) atomic_uint head;
    _Alignas(64) atomic_uint tail;
} PatchCmdRing;

static PatchCmdRing s_cmd_ring;

static inline void patch_ring_init(PatchCmdRing *r) {
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
}
static inline int patch_ring_push(PatchCmdRing *r, const PatchCmd *cmd) {
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    unsigned next = (h + 1u) & SPSC_MASK;
    if (next == atomic_load_explicit(&r->tail, memory_order_acquire))
        return 0;
    r->buf[h] = *cmd;
    atomic_store_explicit(&r->head, next, memory_order_release);
    return 1;
}
static inline int patch_ring_pop(PatchCmdRing *r, PatchCmd *out) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&r->head, memory_order_acquire))
        return 0;
    *out = r->buf[t];
    atomic_store_explicit(&r->tail, (t + 1u) & SPSC_MASK, memory_order_release);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pipeline construction / destruction
 * ═════════════════════════════════════════════════════════════════════════ */

static inline float lp_coeff(float cutoff_hz, float sr) {
    /* Bilinear approximation: a = 2πfc/sr (clamped to [0,1]) */
    float a = (2.0f * (float)M_PI * cutoff_hz) / sr;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

/*
 * Allocate and fully initialise a new pipeline.
 * Called from the manager thread — may block, allocate, do anything.
 */
static Pipeline *pipeline_build(float osc_freq, float osc_amp, float cutoff_hz) {
    Pipeline *p = (Pipeline *)malloc(sizeof(Pipeline));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(Pipeline));

    /* Oscillator */
    p->osc.phase = 0.0f;
    p->osc.freq = osc_freq;
    p->osc.amp = osc_amp;
    p->osc.sr = (float)SR;

    /* One-pole lowpass */
    p->filter.cutoff_target = cutoff_hz;
    p->filter.cutoff_hz = cutoff_hz;
    p->filter.a = lp_coeff(cutoff_hz, (float)SR);
    p->filter.z = 0.0f;
    /* Smoother time-constant ~5 ms so cutoff changes don't zipper */
    p->filter.cutoff_smooth = lp_coeff(1.0f / 0.005f, (float)SR);

    return p;
}

static void pipeline_free(Pipeline *p) { free(p); }

/*
 * Publish a new pipeline atomically and free the old one after a grace period.
 * Called from the manager thread.
 */
static void pipeline_swap(Pipeline *new_pipe) {
    Pipeline *old = atomic_exchange_explicit(&g_pipeline, new_pipe, memory_order_acq_rel);
    if (old) {
        /* Wait long enough for the RT thread to finish any block it started
         * with the old pointer.  Two block-lengths is conservative. */
        SDL_Delay(PIPELINE_GRACE_MS);
        pipeline_free(old);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RT audio callback  (runs on the SDL audio thread — hard deadline)
 *
 * RULES — never break these inside this function:
 *   • No malloc / free / realloc
 *   • No mutex / SDL_LockMutex
 *   • No file or socket I/O
 *   • No SDL_Log / printf / LOGD
 *   • No sleep / SDL_Delay
 * ═════════════════════════════════════════════════════════════════════════ */

void SDLCALL synth_callback(void *userdata, SDL_AudioStream *stream, int additional_amount,
                            int total_amount) {
    LOGD("%d/%d", additional_amount, total_amount);
    (void)userdata;
    (void)additional_amount;
    (void)total_amount;

    /* 1. Acquire the current pipeline — invisible if manager is rebuilding. */
    Pipeline *pipe = atomic_load_explicit(&g_pipeline, memory_order_acquire);
    if (!pipe) {
        /* No pipeline yet: output silence. */
        float silence[BLOCK * CH];
        memset(silence, 0, sizeof(silence));
        SDL_PutAudioStreamData(stream, silence, (int)sizeof(silence));
        return;
    }

    /* 2. Drain any pending MIDI events and apply them to the pipeline. */
    PmhMidiEvent ev;
    if (spsc_pop(&s_midi_ring, &ev)) {
        uint8_t type = ev.raw.msg.status.type;

        if (type == MIDI_NOTE_ON) {
            if (ev.velocity == 0) {
                pipe->osc.amp = 0.0f; /* note-on vel 0 = note off */
            } else {
                /* note → frequency: A4 = 440 Hz at MIDI note 69 */
                pipe->osc.freq = 440.0f * dsp_powf(2.0f, ((float)ev.note - 69.0f) / 12.0f);
                pipe->osc.amp = ((float)ev.velocity / 127.0f) * 0.6f;
            }
        } else if (type == MIDI_NOTE_OFF) {
            pipe->osc.amp = 0.0f;
        } else if (type == MIDI_CONTROL_CHANGE) {
            if (ev.cc_number == 1) {
                /* Mod wheel → filter cutoff: 200 Hz – 8000 Hz */
                pipe->filter.cutoff_target = 200.0f + ((float)ev.cc_value / 127.0f) * 7800.0f;
            } else if (ev.cc_number == 7) {
                /* CC7 volume */
                pipe->osc.amp = (float)ev.cc_value / 127.0f * 0.8f;
            }
        }
    }

    /* 3. Render BLOCK stereo frames into pipe->block. */
    float *buf = pipe->block;
    float phase = pipe->osc.phase;
    float freq = pipe->osc.freq;
    float amp = pipe->osc.amp;
    float sr = pipe->osc.sr;
    float lp_a = pipe->filter.a;
    float lp_z = pipe->filter.z;
    float lp_ct = pipe->filter.cutoff_hz;
    float lp_tgt = pipe->filter.cutoff_target;
    float lp_sm = pipe->filter.cutoff_smooth;

    for (int i = 0; i < BLOCK; ++i) {
        /* 3a. Smooth the filter cutoff to prevent zipper noise. */
        lp_ct += lp_sm * (lp_tgt - lp_ct);
        lp_a = lp_coeff(lp_ct, sr);

        /* 3b. Oscillator — sine via dspmath fast path. */
        float s = amp * dsp_sinf(2.0f * (float)M_PI * phase);
        phase += freq / sr;
        if (phase >= 1.0f)
            phase -= 1.0f;

        /* 3c. One-pole lowpass. */
        // lp_z = lp_a * s + (1.0f - lp_a) * lp_z;
        lp_z = s;

        buf[2 * i + 0] = lp_z; /* L */
        buf[2 * i + 1] = lp_z; /* R */
    }

    /* Write back DSP state. */
    pipe->osc.phase = phase;
    pipe->filter.z = lp_z;
    pipe->filter.cutoff_hz = lp_ct;
    pipe->filter.a = lp_a;

    SDL_PutAudioStreamData(stream, buf, BLOCK * CH * (int)sizeof(float));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Manager thread
 *
 * Responsibilities:
 *   • Poll MIDI and push events to the RT SPSC ring.
 *   • Drain patch-change commands from the main thread and rebuild the
 *     pipeline when needed (MIDI polling pauses during rebuild — that's OK).
 *
 * Timing: SDL_Delay(1) gives ~1 ms MIDI poll cadence, which is imperceptible.
 * ═════════════════════════════════════════════════════════════════════════ */

typedef struct {
    thread_t *parent; /* the synth_thread handle for running check */
    SDL_Thread *sdl_thread;
    SDL_AtomicInt running;
} ManagerCtx;

static PmhInputStream s_pm_in = {0};

/* Callback invoked synchronously from pmh_input_poll() on the manager thread.
 * Pushes the event into the SPSC ring for the RT callback to consume. */
static void midi_event_cb(const PmhMidiEvent *ev, void *userdata) {
    (void)userdata;
    if (!spsc_push(&s_midi_ring, ev))
        LOGW("MIDI ring full — event dropped");
}

static int open_first_midi_input(void) {
    int n = pmh_device_count();
    for (int i = 0; i < n; ++i) {
        PmhDevice d;
        if (pmh_device_get(i, &d) == 0 && d.direction == PMH_DIR_INPUT) {
            if (pmh_input_open(&s_pm_in, d.index, PMH_CHANNEL_OMNI) == 0) {
                pmh_input_set_callback(&s_pm_in, midi_event_cb, NULL);
                LOGD("Opened MIDI input: [%d] %s", d.index, d.name);
                return d.index;
            }
        }
    }
    return -1;
}

static int manager_thread_fn(void *arg) {
    ManagerCtx *ctx = (ManagerCtx *)arg;

    /* Init MIDI */
    if (pmh_init() != 0) {
        LOGE("pmh_init failed: %s", pmh_last_error());
    } else {
        if (open_first_midi_input() < 0)
            LOGD("No MIDI input device found");
    }

    /* Build the initial pipeline and publish it. */
    Pipeline *initial = pipeline_build(220.0f, 0.0f, 8000.0f);
    if (!initial) {
        LOGE("Failed to allocate initial pipeline");
        SDL_SetAtomicInt(&ctx->running, 0);
        return 1;
    }
    pipeline_swap(initial);
    LOGD("Initial pipeline published");

    while (SDL_GetAtomicInt(&ctx->running)) {
        /* ── MIDI polling ──────────────────────────────────────────────── */
        if (s_pm_in.pm_stream) {
            int r = pmh_input_poll(&s_pm_in);
            if (r < 0)
                LOGE("pmh_input_poll: %s", pmh_last_error());
        }

        /* ── Patch command processing ──────────────────────────────────── */
        PatchCmd cmd;
        while (patch_ring_pop(&s_cmd_ring, &cmd)) {
            LOGD("Patch cmd: freq=%.1f amp=%.2f cutoff=%.0f", cmd.osc_freq, cmd.osc_amp,
                 cmd.filter_cutoff);

            /*
             * Rebuild the pipeline from scratch with the new parameters.
             * MIDI polling pauses here — acceptable since a patch change
             * is a user-initiated event, not a performance event.
             *
             * Preserve DSP state from the current pipeline where it makes
             * sense (phase continuity, filter state) to avoid clicks.
             */
            Pipeline *cur = atomic_load_explicit(&g_pipeline, memory_order_acquire);
            float phase = cur ? cur->osc.phase : 0.0f;
            float lp_z = cur ? cur->filter.z : 0.0f;

            Pipeline *np = pipeline_build(cmd.osc_freq, cmd.osc_amp, cmd.filter_cutoff);
            if (!np) {
                LOGE("pipeline_build OOM — keeping current pipeline");
                continue;
            }
            /* Carry over phase and filter state for click-free transitions. */
            np->osc.phase = phase;
            np->filter.z = lp_z;

            pipeline_swap(np);
            LOGD("Pipeline rebuilt and published");
        }

        SDL_Delay(1); /* ~1 ms poll cadence */
    }

    /* Cleanup */
    if (s_pm_in.pm_stream)
        pmh_input_close(&s_pm_in);
    pmh_shutdown();

    LOGD("Manager thread exiting");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API: send a patch command from the main thread
 * ═════════════════════════════════════════════════════════════════════════ */

void synth_send_patch(float osc_freq, float osc_amp, float filter_cutoff) {
    PatchCmd cmd = {
        .osc_freq = osc_freq,
        .osc_amp = osc_amp,
        .filter_cutoff = filter_cutoff,
    };
    if (!patch_ring_push(&s_cmd_ring, &cmd))
        LOGW("Patch command ring full — command dropped");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * synth_thread — entry point (called by thread_create in app.c)
 * ═════════════════════════════════════════════════════════════════════════ */

int synth_thread(void *data) {
    thread_t *this = (thread_t *)data;

    audio_setup_print();

    /* Initialise shared rings. */
    spsc_init(&s_midi_ring);
    patch_ring_init(&s_cmd_ring);

    /* Start the manager thread. */
    static ManagerCtx mgr_ctx;
    SDL_SetAtomicInt(&mgr_ctx.running, 1);
    mgr_ctx.parent = this;
    mgr_ctx.sdl_thread = SDL_CreateThread(manager_thread_fn, "synth-manager", &mgr_ctx);
    if (!mgr_ctx.sdl_thread) {
        LOGE("Failed to create manager thread: %s", SDL_GetError());
        return 1;
    }

    /* Open the SDL3 audio stream — this is what drives the RT callback. */
    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32,
        .channels = CH,
        .freq = SR,
    };
    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, synth_callback, NULL);
    if (!stream) {
        LOGE("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        SDL_SetAtomicInt(&mgr_ctx.running, 0);
        SDL_WaitThread(mgr_ctx.sdl_thread, NULL);
        return 1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    LOGD("Audio stream started (SR=%d CH=%d BLOCK=%d)", SR, CH, BLOCK);

    /* Park here until thread_stop() signals us. */
    while (SDL_GetAtomicInt(&this->running))
        SDL_Delay(10);

    /* Teardown: stop manager, destroy audio stream, free pipeline. */
    LOGD("synth_thread: shutting down");
    SDL_SetAtomicInt(&mgr_ctx.running, 0);
    SDL_WaitThread(mgr_ctx.sdl_thread, NULL);

    SDL_DestroyAudioStream(stream);

    /* Free the pipeline that's still live — no RT thread left to race with. */
    Pipeline *last = atomic_exchange_explicit(&g_pipeline, NULL, memory_order_acq_rel);
    pipeline_free(last);

    LOGD("synth_thread: exited cleanly");
    return 0;
}
