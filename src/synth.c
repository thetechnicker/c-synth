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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Globals
 * ═════════════════════════════════════════════════════════════════════════ */

_Atomic(Pipeline *) g_pipeline = NULL;

static SpscRing s_midi_ring;

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
 * DSP helpers
 * ═════════════════════════════════════════════════════════════════════════ */

static inline float lp_coeff(float cutoff_hz, float sr) {
    float a = (2.0f * (float)M_PI * cutoff_hz) / sr;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Voice allocation  (RT-safe: no malloc, no locks, no I/O)
 * ═════════════════════════════════════════════════════════════════════════ */

/*
 * Monotonically increasing counter stamped on every note-on.
 * Written only from the RT callback, so no atomic needed — the RT thread
 * is the sole writer and reader.
 */
static uint32_t s_voice_age = 0;

/*
 * Fully initialise a voice slot for a new note.
 * Resets phase and filter state to avoid clicks when stealing.
 */
static inline void voice_init(Voice *v, uint8_t note, float vel_norm, float cutoff_hz,
                              float amp_scale) {
    v->osc.phase = 0.0f;
    v->osc.freq = 440.0f * dsp_powf(2.0f, ((float)note - 69.0f) / 12.0f);
    v->osc.amp = vel_norm * amp_scale;
    v->osc.sr = (float)SR;

    v->filter.cutoff_target = cutoff_hz;
    v->filter.cutoff_hz = cutoff_hz;
    v->filter.a = lp_coeff(cutoff_hz, (float)SR);
    v->filter.z = 0.0f;
    /* ~5 ms smoother so cutoff changes don't produce zipper noise. */
    v->filter.cutoff_smooth = lp_coeff(1.0f / 0.005f, (float)SR);

    v->note = note;
    v->velocity = vel_norm;
    v->age = ++s_voice_age;
    v->active = true;
}

/*
 * Find a free voice or steal the one with the lowest velocity (softest note).
 *
 * Priority order:
 *   1. Re-trigger an already-playing instance of the same note (avoids
 *      accumulating duplicate voices for repeated keys).
 *   2. First inactive slot.
 *   3. Active voice with the lowest velocity.
 */
static Voice *voice_allocate(Pipeline *pipe, uint8_t note, float vel_norm, float cutoff_hz,
                             float amp_scale) {
    Voice *steal = NULL;

    for (int i = 0; i < NUM_VOICES; ++i) {
        Voice *v = &pipe->voices[i];

        if (v->active && v->note == note) {
            /* Re-trigger same note. */
            voice_init(v, note, vel_norm, cutoff_hz, amp_scale);
            return v;
        }
        if (!v->active) {
            voice_init(v, note, vel_norm, cutoff_hz, amp_scale);
            return v;
        }
        /* Track softest active voice for stealing. */
        if (!steal || v->velocity < steal->velocity)
            steal = v;
    }

    /* All voices busy — steal the softest. */
    voice_init(steal, note, vel_norm, cutoff_hz, amp_scale);
    return steal;
}

/*
 * Release the active voice holding the given MIDI note number.
 * Simply marks it inactive; amplitude ramp-down (envelope) can be
 * added here later without touching the allocator.
 */
static void voice_release(Pipeline *pipe, uint8_t note) {
    for (int i = 0; i < NUM_VOICES; ++i) {
        Voice *v = &pipe->voices[i];
        if (v->active && v->note == note) {
            v->active = false;
            v->osc.amp = 0.0f;
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pipeline construction / destruction
 * ═════════════════════════════════════════════════════════════════════════ */

/*
 * Allocate and initialise a silent pipeline.
 * All voices start inactive; filter defaults are pre-loaded so the first
 * note-on sounds correct even before any CC messages have arrived.
 */
static Pipeline *pipeline_build(float default_freq, float default_amp, float cutoff_hz) {
    (void)default_amp; /* applied by voice_init on first note-on */

    Pipeline *p = (Pipeline *)malloc(sizeof(Pipeline));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(Pipeline));

    for (int i = 0; i < NUM_VOICES; ++i) {
        Voice *v = &p->voices[i];
        v->active = false;
        v->osc.sr = (float)SR;
        v->osc.freq = default_freq;
        v->osc.amp = 0.0f;
        v->filter.cutoff_target = cutoff_hz;
        v->filter.cutoff_hz = cutoff_hz;
        v->filter.a = lp_coeff(cutoff_hz, (float)SR);
        v->filter.cutoff_smooth = lp_coeff(1.0f / 0.005f, (float)SR);
    }
    return p;
}

static void pipeline_free(Pipeline *p) { free(p); }

/*
 * Atomically publish a new pipeline and free the old one after a grace period
 * long enough for the RT thread to finish any block started with the old ptr.
 */
static void pipeline_swap(Pipeline *new_pipe) {
    Pipeline *old = atomic_exchange_explicit(&g_pipeline, new_pipe, memory_order_acq_rel);
    if (old) {
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

/*
 * Fixed headroom scale applied to the summed mix.
 * Prevents clipping when all NUM_VOICES sound at full amplitude.
 * A soft-knee limiter or RMS compressor can replace this later.
 */
#define MIX_SCALE (1.0f / (float)NUM_VOICES)

void SDLCALL synth_callback(void *userdata, SDL_AudioStream *stream, int additional_amount,
                            int total_amount) {
    LOGD("%d/%d", additional_amount, total_amount);
    (void)userdata;
    (void)additional_amount;
    (void)total_amount;

    /* 1. Acquire the current pipeline. */
    Pipeline *pipe = atomic_load_explicit(&g_pipeline, memory_order_acquire);
    if (!pipe) {
        float silence[additional_amount * CH];
        memset(silence, 0, sizeof(silence));
        SDL_PutAudioStreamData(stream, silence, (int)sizeof(silence));
        return;
    }

    /* 2. Drain all pending MIDI events and update voice state. */
    PmhMidiEvent ev;
    while (spsc_pop(&s_midi_ring, &ev)) {
        uint8_t type = ev.raw.msg.status.type;

        if (type == MIDI_NOTE_ON) {
            if (ev.velocity == 0) {
                /* Note-on with velocity 0 is equivalent to note-off. */
                voice_release(pipe, ev.note);
            } else {
                float vel_norm = (float)ev.velocity / 127.0f;
                /* Use the mod-wheel cutoff target already set on voice 0
                 * as the initial cutoff for the new voice. */
                float cutoff = pipe->voices[0].filter.cutoff_target;
                voice_allocate(pipe, ev.note, vel_norm, cutoff, 0.6f);
            }
        } else if (type == MIDI_NOTE_OFF) {
            voice_release(pipe, ev.note);
        } else if (type == MIDI_CONTROL_CHANGE) {
            if (ev.cc_number == 1) {
                /* Mod wheel → filter cutoff on all voices (200–8000 Hz). */
                float ct = 200.0f + ((float)ev.cc_value / 127.0f) * 7800.0f;
                for (int i = 0; i < NUM_VOICES; ++i)
                    pipe->voices[i].filter.cutoff_target = ct;
            } else if (ev.cc_number == 7) {
                /* CC7 volume → rescale amplitude on all active voices. */
                float gain = (float)ev.cc_value / 127.0f * 0.8f;
                for (int i = 0; i < NUM_VOICES; ++i)
                    if (pipe->voices[i].active)
                        pipe->voices[i].osc.amp = gain;
            }
        }
    }

    /* 3. Render: accumulate all active voices into the mix buffer. */
    float *buf = pipe->block;
    memset(buf, 0, BLOCK * CH * sizeof(float));

    // int render_amount = additional_amount < BLOCK ? additional_amount : BLOCK;
    for (int vi = 0; vi < NUM_VOICES; ++vi) {
        Voice *v = &pipe->voices[vi];
        if (!v->active)
            continue;

        /* Load DSP state into locals for the inner loop. */
        float phase = v->osc.phase;
        float freq = v->osc.freq;
        float amp = v->osc.amp;
        float sr = v->osc.sr;
        float lp_a = v->filter.a;
        float lp_z = v->filter.z;
        float lp_ct = v->filter.cutoff_hz;
        float lp_tgt = v->filter.cutoff_target;
        float lp_sm = v->filter.cutoff_smooth;

        for (int i = 0; i < BLOCK; ++i) {
            /* 3a. Smooth filter cutoff to prevent zipper noise. */
            lp_ct += lp_sm * (lp_tgt - lp_ct);
            lp_a = lp_coeff(lp_ct, sr);

            /* 3b. Sine oscillator. */
            float s = amp * dsp_sinf(2.0f * (float)M_PI * phase);
            phase += freq / sr;
            if (phase >= 1.0f)
                phase -= 1.0f;

            /* 3c. One-pole lowpass. */
            lp_z = lp_a * s + (1.0f - lp_a) * lp_z;

            /* Accumulate into stereo mix buffer. */
            buf[2 * i + 0] += lp_z; /* L */
            buf[2 * i + 1] += lp_z; /* R */
        }

        /* Write back DSP state for next block. */
        v->osc.phase = phase;
        v->filter.z = lp_z;
        v->filter.cutoff_hz = lp_ct;
        v->filter.a = lp_a;
    }

    /* 4. Apply headroom scale to the final mix. */
    for (int i = 0; i < BLOCK * CH; ++i)
        buf[i] *= MIX_SCALE;

    SDL_PutAudioStreamData(stream, buf, BLOCK * CH * (int)sizeof(float));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Manager thread
 *
 * Responsibilities:
 *   • Poll MIDI and push events to the RT SPSC ring.
 *   • Drain patch-change commands and rebuild the pipeline when needed.
 * ═════════════════════════════════════════════════════════════════════════ */

typedef struct {
    thread_t *parent;
    SDL_Thread *sdl_thread;
    SDL_AtomicInt running;
} ManagerCtx;

static PmhInputStream s_pm_in = {0};

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

    if (pmh_init() != 0) {
        LOGE("pmh_init failed: %s", pmh_last_error());
    } else {
        if (open_first_midi_input() < 0)
            LOGD("No MIDI input device found");
    }

    Pipeline *initial = pipeline_build(220.0f, 0.0f, 8000.0f);
    if (!initial) {
        LOGE("Failed to allocate initial pipeline");
        SDL_SetAtomicInt(&ctx->running, 0);
        return 1;
    }
    pipeline_swap(initial);
    LOGD("Initial pipeline published (%d voices)", NUM_VOICES);

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

            Pipeline *cur = atomic_load_explicit(&g_pipeline, memory_order_acquire);
            Pipeline *np = pipeline_build(cmd.osc_freq, cmd.osc_amp, cmd.filter_cutoff);
            if (!np) {
                LOGE("pipeline_build OOM — keeping current pipeline");
                continue;
            }

            /*
             * Carry over per-voice state for click-free transitions.
             * Active voices keep their current pitch and amplitude;
             * inactive voices will pick up the new defaults on their
             * next note-on via voice_init().
             */
            if (cur) {
                for (int i = 0; i < NUM_VOICES; ++i) {
                    Voice *src = &cur->voices[i];
                    Voice *dst = &np->voices[i];

                    dst->osc.phase = src->osc.phase;
                    dst->filter.z = src->filter.z;
                    dst->active = src->active;
                    dst->note = src->note;
                    dst->velocity = src->velocity;
                    dst->age = src->age;

                    if (src->active) {
                        /* Preserve frequency and amplitude of playing notes. */
                        dst->osc.freq = src->osc.freq;
                        dst->osc.amp = src->osc.amp;
                    }
                }
            }

            pipeline_swap(np);
            LOGD("Pipeline rebuilt and published");
        }

        SDL_Delay(1);
    }

    if (s_pm_in.pm_stream)
        pmh_input_close(&s_pm_in);
    pmh_shutdown();

    LOGD("Manager thread exiting");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
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
 * synth_thread — entry point
 * ═════════════════════════════════════════════════════════════════════════ */

int synth_thread(void *data) {
    thread_t *this = (thread_t *)data;

    audio_setup_print();

    spsc_init(&s_midi_ring);
    patch_ring_init(&s_cmd_ring);

    static ManagerCtx mgr_ctx;
    SDL_SetAtomicInt(&mgr_ctx.running, 1);
    mgr_ctx.parent = this;
    mgr_ctx.sdl_thread = SDL_CreateThread(manager_thread_fn, "synth-manager", &mgr_ctx);
    if (!mgr_ctx.sdl_thread) {
        LOGE("Failed to create manager thread: %s", SDL_GetError());
        return 1;
    }

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
    LOGD("Audio stream started (SR=%d CH=%d BLOCK=%d NUM_VOICES=%d)", SR, CH, BLOCK, NUM_VOICES);

    while (SDL_GetAtomicInt(&this->running))
        SDL_Delay(10);

    LOGD("synth_thread: shutting down");
    SDL_SetAtomicInt(&mgr_ctx.running, 0);
    SDL_WaitThread(mgr_ctx.sdl_thread, NULL);

    SDL_DestroyAudioStream(stream);

    Pipeline *last = atomic_exchange_explicit(&g_pipeline, NULL, memory_order_acq_rel);
    pipeline_free(last);

    LOGD("synth_thread: exited cleanly");
    return 0;
}
