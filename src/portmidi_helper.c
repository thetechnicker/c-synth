/**
 * @file portmidi_helper.c
 * @brief Implementation of portmidi_helper.h
 *
 * Build example (Linux/macOS):
 *   cc -std=c11 -Wall -Wextra portmidi_helper.c -lportmidi -lSDL3 -o yourapp
 *
 * Build example (Windows, MSVC):
 *   cl /std:c11 portmidi_helper.c portmidi.lib SDL3.lib
 */

#include "portmidi_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PortTime for Pm_Initialize callback (may be NULL on some platforms) */
#include <porttime/porttime.h>

/* SDL3 for Uint64 ticks — only needed if you extend clock scheduling.
   If you don't want the SDL3 dependency here, remove this include and
   the SDL_GetTicks() reference below. */
#ifdef PMH_USE_SDL3_TICKS
#include <SDL3/SDL.h>
#endif

/* --------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

/** Set by every pmh_* function that fails.  Retrieved via pmh_last_error(). */
static char s_last_error[256] = "";

static void set_error(const char *msg) {
    strncpy(s_last_error, msg ? msg : "(null)", sizeof(s_last_error) - 1);
    s_last_error[sizeof(s_last_error) - 1] = '\0';
}

static void clear_error(void) { s_last_error[0] = '\0'; }

/* --------------------------------------------------------------------------
 * Internal: timestamp wrapper
 *
 * Pt_Time has signature  PtTimestamp (*)(void)
 * PmTimeProcPtr expects  int         (*)(void *)
 *
 * They are ABI-identical on all supported platforms but GCC/Clang correctly
 * reject the implicit cast.  This wrapper satisfies the expected type.
 * ---------------------------------------------------------------------- */

static PmTimestamp pmh_time_proc(void *unused) {
    (void)unused;
    return Pt_Time();
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

int pmh_init(void) {
    clear_error();

    /* PortTime must be started before Pm_Initialize on some platforms */
    PtError pt_err = Pt_Start(1 /* ms resolution */, NULL, NULL);
    if (pt_err != ptNoError) {
        set_error("Pt_Start failed");
        return -1;
    }

    PmError pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        set_error(Pm_GetErrorText(pm_err));
        Pt_Stop();
        return -1;
    }

    return 0;
}

void pmh_shutdown(void) {
    Pm_Terminate();
    Pt_Stop();
    clear_error();
}

/* --------------------------------------------------------------------------
 * Device enumeration
 * ---------------------------------------------------------------------- */

int pmh_device_count(void) { return Pm_CountDevices(); }

int pmh_device_get(int index, PmhDevice *out) {
    if (!out)
        return -1;
    if (index < 0 || index >= Pm_CountDevices()) {
        set_error("pmh_device_get: index out of range");
        return -1;
    }

    const PmDeviceInfo *info = Pm_GetDeviceInfo(index);
    if (!info) {
        set_error("pmh_device_get: Pm_GetDeviceInfo returned NULL");
        return -1;
    }

    out->index = index;
    out->name = info->name;
    out->interf = info->interf;
    out->direction = info->input ? PMH_DIR_INPUT : PMH_DIR_OUTPUT;
    out->is_open = info->opened;
    out->is_virtual = 0; /* PortMidi 2.x does not expose virtual flag */

    return 0;
}

int pmh_device_find_by_name(const char *name, PmhDirection dir) {
    if (!name)
        return -1;

    int count = Pm_CountDevices();
    for (int i = 0; i < count; ++i) {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (!info)
            continue;

        int dir_match = (dir == PMH_DIR_INPUT) ? info->input : info->output;
        if (!dir_match)
            continue;

        /* Case-insensitive substring match */
        const char *haystack = info->name;
        const char *needle = name;

        /* Simple strstr-style scan, lowercasing on the fly */
        size_t nlen = strlen(needle);
        size_t hlen = strlen(haystack);
        if (nlen > hlen)
            continue;

        for (size_t h = 0; h <= hlen - nlen; ++h) {
            int match = 1;
            for (size_t n = 0; n < nlen; ++n) {
                char hc = haystack[h + n];
                char nc = needle[n];
                /* tolower without locale */
                if (hc >= 'A' && hc <= 'Z')
                    hc += 32;
                if (nc >= 'A' && nc <= 'Z')
                    nc += 32;
                if (hc != nc) {
                    match = 0;
                    break;
                }
            }
            if (match)
                return i;
        }
    }

    return -1;
}

void pmh_device_list_print(void) {
    int count = Pm_CountDevices();
    int def_in = Pm_GetDefaultInputDeviceID();
    int def_out = Pm_GetDefaultOutputDeviceID();

    printf("PortMidi devices (%d total):\n", count);
    for (int i = 0; i < count; ++i) {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (!info)
            continue;

        const char *dir = info->input ? "IN " : "OUT";
        const char *opened = info->opened ? " [open]" : "";
        const char *dflt = (i == def_in || i == def_out) ? " [default]" : "";

        printf("  [%2d] %s  %-40s  via %s%s%s\n", i, dir, info->name, info->interf, opened, dflt);
    }
}

/* --------------------------------------------------------------------------
 * Internal: decode a PmEvent into a PmhMidiEvent
 * ---------------------------------------------------------------------- */

static PmhMidiEvent decode_event(const PmEvent *ev) {
    PmhMidiEvent out;
    memset(&out, 0, sizeof(out));

    out.raw = midi_from_uint32((uint32_t)ev->message);
    out.timestamp = ev->timestamp;
    out.channel = out.raw.msg.status.channel;

    switch ((midi_msg_type_t)out.raw.msg.status.type) {
    case MIDI_NOTE_ON:
    case MIDI_NOTE_OFF:
        out.note = out.raw.msg.data1;
        out.velocity = out.raw.msg.data2;
        break;

    case MIDI_CONTROL_CHANGE:
        out.cc_number = out.raw.msg.data1;
        out.cc_value = out.raw.msg.data2;
        break;

    case MIDI_PROGRAM_CHANGE:
        out.program = out.raw.msg.data1;
        break;

    case MIDI_PITCH_BEND: {
        /* 14-bit value: data1 = LSB, data2 = MSB, centre = 0x2000 */
        uint16_t raw14 = (uint16_t)(out.raw.msg.data1) | ((uint16_t)(out.raw.msg.data2) << 7);
        out.pitch_bend = (int16_t)(raw14 - 0x2000);
        break;
    }

    default:
        break;
    }

    return out;
}

/* --------------------------------------------------------------------------
 * Input stream
 * ---------------------------------------------------------------------- */

int pmh_input_open(PmhInputStream *s, int device_index, uint8_t channel_filter) {
    if (!s)
        return -1;
    clear_error();

    PmError err =
        Pm_OpenInput(&s->pm_stream, device_index, NULL, /* platform-specific info (unused) */
                     PMH_POLL_BUFFER,                   /* internal PortMidi queue size     */
                     pmh_time_proc,                     /* timestamp provider               */
                     NULL                               /* time info (unused)               */
        );

    if (err != pmNoError) {
        set_error(Pm_GetErrorText(err));
        s->pm_stream = NULL;
        return -1;
    }

    s->device_index = device_index;
    s->channel_filter = channel_filter;
    return 0;
}

void pmh_input_set_callback(PmhInputStream *s, PmhEventCallback cb, void *userdata) {
    if (!s)
        return;
    s->callback = cb;
    s->userdata = userdata;
}

int pmh_input_poll(PmhInputStream *s) {
    if (!s || !s->pm_stream)
        return -1;

    PmEvent buffer[PMH_POLL_BUFFER];
    int dispatched = 0;

    /* Pm_Read returns the number of events read, or a negative PmError */
    int n = Pm_Read(s->pm_stream, buffer, PMH_POLL_BUFFER);
    if (n < 0) {
        set_error(Pm_GetErrorText((PmError)n));
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        PmhMidiEvent ev = decode_event(&buffer[i]);

        /* Channel filter — only apply to channel voice messages (type < 0xF) */
        if (s->channel_filter != PMH_CHANNEL_OMNI &&
            ev.raw.msg.status.type != (uint8_t)MIDI_SYSTEM && ev.channel != s->channel_filter) {
            continue;
        }

        if (s->callback) {
            s->callback(&ev, s->userdata);
        }
        ++dispatched;
    }

    return dispatched;
}

void pmh_input_close(PmhInputStream *s) {
    if (!s || !s->pm_stream)
        return;
    Pm_Close(s->pm_stream);
    s->pm_stream = NULL;
}

/* --------------------------------------------------------------------------
 * Output stream
 * ---------------------------------------------------------------------- */

int pmh_output_open(PmhOutputStream *s, int device_index) {
    if (!s)
        return -1;
    clear_error();

    PmError err = Pm_OpenOutput(&s->pm_stream, device_index, NULL, /* platform-specific info   */
                                PMH_POLL_BUFFER,                   /* output buffer size       */
                                pmh_time_proc,                     /* timestamp provider       */
                                NULL,                              /* time info                */
                                PMH_OUTPUT_LATENCY_MS              /* latency in ms            */
    );

    if (err != pmNoError) {
        set_error(Pm_GetErrorText(err));
        s->pm_stream = NULL;
        return -1;
    }

    s->device_index = device_index;
    return 0;
}

void pmh_output_close(PmhOutputStream *s) {
    if (!s || !s->pm_stream)
        return;
    Pm_Close(s->pm_stream);
    s->pm_stream = NULL;
}

/* --------------------------------------------------------------------------
 * MIDI clock / sync helpers
 * ---------------------------------------------------------------------- */

/** Send a single-byte real-time message (status only, no data bytes). */
static void send_realtime(PmhOutputStream *s, uint8_t status) {
    if (!s || !s->pm_stream)
        return;

    /* Real-time messages are packed as: status | 0 | 0 */
    PmMessage msg = Pm_Message(status, 0, 0);
    PmEvent ev = {msg, pmh_time_proc(NULL)};
    PmError err = Pm_Write(s->pm_stream, &ev, 1);
    if (err != pmNoError) {
        set_error(Pm_GetErrorText(err));
    }
}

void pmh_clock_tick(PmhOutputStream *s) { send_realtime(s, 0xF8); }
void pmh_clock_start(PmhOutputStream *s) { send_realtime(s, 0xFA); }
void pmh_clock_continue(PmhOutputStream *s) { send_realtime(s, 0xFB); }
void pmh_clock_stop(PmhOutputStream *s) { send_realtime(s, 0xFC); }

/* --------------------------------------------------------------------------
 * Error helpers
 * ---------------------------------------------------------------------- */

const char *pmh_strerror(PmError err) {
    const char *text = Pm_GetErrorText(err);
    return text ? text : "(unknown PortMidi error)";
}

const char *pmh_last_error(void) { return s_last_error; }
