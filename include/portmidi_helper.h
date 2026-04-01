/**
 * @file portmidi_helper.h
 * @brief PortMidi helper — device enumeration, input/output streams,
 *        MIDI clock, channel filtering, and callback-based event dispatch.
 *
 * Merged from midi.h (MIDI wire types) + portmidi helper API.
 * Designed for use alongside SDL3 (audio + event loop).
 * No background threads — call pmh_input_poll() from your SDL3 loop.
 *
 * Dependencies: PortMidi (portmidi.h, porttime.h), SDL3, C11
 *
 * Typical usage:
 * @code
 *   pmh_init();
 *   PmhInputStream in = {0};
 *   pmh_input_open(&in, pmh_device_find_by_name("My Device", PMH_DIR_INPUT), PMH_CHANNEL_OMNI);
 *   pmh_input_set_callback(&in, my_callback, synth_ptr);
 *
 *   // SDL3 event loop:
 *   pmh_input_poll(&in);
 *
 *   pmh_input_close(&in);
 *   pmh_shutdown();
 * @endcode
 */

#ifndef PORTMIDI_HELPER_H
#define PORTMIDI_HELPER_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <portmidi.h>
#include <porttime/porttime.h>

/* --------------------------------------------------------------------------
 * Compile-time tunables
 * ---------------------------------------------------------------------- */

/** Maximum events drained from PortMidi per pmh_input_poll() call. */
#ifndef PMH_POLL_BUFFER
#define PMH_POLL_BUFFER 64
#endif

/* --------------------------------------------------------------------------
 * 1.  MIDI wire types  (originally midi.h)
 * ---------------------------------------------------------------------- */

/**
 * @brief Status byte — raw uint8 with type/channel nibble view.
 *
 * Wire format: high nibble = message type, low nibble = channel (0-based).
 */
typedef union midi_status {
    uint8_t raw; /**< Full status byte, e.g. 0x9E = note-on ch 14 */
    struct {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        uint8_t type : 4;    /**< Message type  (high nibble) */
        uint8_t channel : 4; /**< MIDI channel  (low nibble)  */
#else
        uint8_t channel : 4; /**< MIDI channel  (low nibble)  */
        uint8_t type : 4;    /**< Message type  (high nibble) */
#endif
    };
} midi_status_t;

static_assert(sizeof(midi_status_t) == 1, "midi_status_t must be 1 byte");

/** High nibble values of the MIDI status byte. */
typedef enum midi_msg_type {
    MIDI_NOTE_OFF = 0x8,
    MIDI_NOTE_ON = 0x9,
    MIDI_POLY_PRESSURE = 0xA,
    MIDI_CONTROL_CHANGE = 0xB,
    MIDI_PROGRAM_CHANGE = 0xC,
    MIDI_CHANNEL_PRESSURE = 0xD,
    MIDI_PITCH_BEND = 0xE,
    MIDI_SYSTEM = 0xF, /**< SysEx / real-time / system messages */
} midi_msg_type_t;

/** Unpacked MIDI message — named fields matching the wire layout. */
typedef struct midi_message_unpacked {
    midi_status_t status; /**< Status byte with nibble view */
    uint8_t data1;        /**< First data byte  (e.g. note number, CC number) */
    uint8_t data2;        /**< Second data byte (e.g. velocity, CC value)     */
    uint8_t _pad;         /**< Reserved — always zero on wire                 */
} midi_message_unpacked_t;

static_assert(sizeof(midi_message_unpacked_t) == 4, "unpacked size must be 4");
static_assert(offsetof(midi_message_unpacked_t, status) == 0, "bad status offset");
static_assert(offsetof(midi_message_unpacked_t, data1) == 1, "bad data1 offset");
static_assert(offsetof(midi_message_unpacked_t, data2) == 2, "bad data2 offset");

/** Top-level MIDI message — raw bytes OR unpacked named-field view. */
typedef union midi_message {
    uint8_t bytes[4];            /**< Wire-safe byte access  */
    midi_message_unpacked_t msg; /**< Named field access      */
} midi_message_t;

static_assert(sizeof(midi_message_t) == 4, "midi_message_t must be 4 bytes");

/* --- Inline helpers ------------------------------------------------------ */

/**
 * @brief Pack individual fields into a midi_message_t.
 */
static inline midi_message_t midi_pack(midi_msg_type_t type, uint8_t channel, uint8_t data1,
                                       uint8_t data2) {
    midi_message_t m;
    m.msg.status.type = (uint8_t)type;
    m.msg.status.channel = channel & 0x0Fu;
    m.msg.data1 = data1;
    m.msg.data2 = data2;
    m.msg._pad = 0;
    return m;
}

/**
 * @brief Unpack a raw 4-byte buffer into a midi_message_t.
 * @note  Use instead of casting to avoid strict-aliasing UB.
 */
static inline midi_message_t midi_from_bytes(const uint8_t buf[4]) {
    midi_message_t m;
    memcpy(&m, buf, 4);
    return m;
}

/**
 * @brief Write a midi_message_t into a raw 4-byte buffer for wire transmission.
 */
static inline void midi_to_bytes(const midi_message_t *m, uint8_t buf[4]) { memcpy(buf, m, 4); }

/**
 * @brief Convert a PortMidi PmMessage (uint32_t) to midi_message_t.
 *
 * PmMessage byte layout (endian-independent):
 *   bits  7:0  = status
 *   bits 15:8  = data1
 *   bits 23:16 = data2
 *   bits 31:24 = unused
 */
static inline midi_message_t midi_from_uint32(uint32_t pm) {
    return midi_pack((midi_msg_type_t)((pm & 0xF0u) >> 4), (uint8_t)(pm & 0x0Fu),
                     (uint8_t)((pm >> 8) & 0xFFu), (uint8_t)((pm >> 16) & 0xFFu));
}

/* --------------------------------------------------------------------------
 * 2.  Helper constants
 * ---------------------------------------------------------------------- */

/**
 * @brief Pass as channel_filter to receive events on all channels.
 */
#define PMH_CHANNEL_OMNI 0xFFu

/**
 * @brief PortMidi input buffer latency in milliseconds.
 *        Increase if you observe dropped events on slow systems.
 */
#define PMH_INPUT_LATENCY_MS 0

/**
 * @brief PortMidi output buffer latency in milliseconds.
 *        Must be > 0 when using PmTimestamp-based scheduling.
 */
#define PMH_OUTPUT_LATENCY_MS 1

/* --------------------------------------------------------------------------
 * 3.  Device descriptor
 * ---------------------------------------------------------------------- */

/** Direction flag for device queries. */
typedef enum PmhDirection {
    PMH_DIR_INPUT = 0,
    PMH_DIR_OUTPUT = 1,
} PmhDirection;

/**
 * @brief Snapshot of a PortMidi device's properties.
 *
 * Populated by pmh_device_get().  The name pointer is owned by PortMidi
 * and remains valid until Pm_Terminate() is called.
 */
typedef struct PmhDevice {
    int index;              /**< PortMidi device index (pass to open calls) */
    const char *name;       /**< Human-readable device name                 */
    const char *interf;     /**< Host API name (e.g. "CoreMIDI", "ALSA")    */
    PmhDirection direction; /**< Input or output                            */
    int is_open;            /**< Non-zero if currently opened by PortMidi   */
    int is_virtual;         /**< Non-zero for virtual/software devices       */
} PmhDevice;

/* --------------------------------------------------------------------------
 * 4.  Decoded MIDI event (dispatched to callback)
 * ---------------------------------------------------------------------- */

/**
 * @brief A decoded, timestamped MIDI event.
 *
 * Built from a raw PmEvent; the original message is preserved in @c raw.
 * Channel voice events are additionally broken out into named fields.
 */
typedef struct PmhMidiEvent {
    midi_message_t raw;    /**< Full decoded message (type + channel + data) */
    PmTimestamp timestamp; /**< PortMidi timestamp in milliseconds           */

    /* Convenience aliases — valid for channel voice messages only */
    uint8_t channel;    /**< 0-based MIDI channel (0–15)                    */
    uint8_t note;       /**< Note number    (NOTE_ON / NOTE_OFF)            */
    uint8_t velocity;   /**< Velocity       (NOTE_ON / NOTE_OFF)            */
    uint8_t cc_number;  /**< CC number      (CONTROL_CHANGE)                */
    uint8_t cc_value;   /**< CC value       (CONTROL_CHANGE)                */
    uint8_t program;    /**< Program number (PROGRAM_CHANGE)                */
    int16_t pitch_bend; /**< Pitch bend, signed, -8192..+8191 (PITCH_BEND)  */
} PmhMidiEvent;

/* --------------------------------------------------------------------------
 * 5.  Callback type
 * ---------------------------------------------------------------------- */

/**
 * @brief Called once per incoming MIDI event that passes the channel filter.
 *
 * Invoked synchronously from pmh_input_poll() — do not call pmh_input_close()
 * or pmh_shutdown() from inside the callback.
 *
 * @param event     Pointer to the decoded event (valid only for this call).
 * @param userdata  Opaque pointer registered with pmh_input_set_callback().
 */
typedef void (*PmhEventCallback)(const PmhMidiEvent *event, void *userdata);

/* --------------------------------------------------------------------------
 * 6.  Stream types
 * ---------------------------------------------------------------------- */

/**
 * @brief An open PortMidi input stream with channel filter and callback.
 *
 * Zero-initialise before use:
 * @code
 *   PmhInputStream in = {0};
 * @endcode
 */
typedef struct PmhInputStream {
    PortMidiStream *pm_stream; /**< Underlying PortMidi stream         */
    int device_index;          /**< PortMidi device index              */
    uint8_t channel_filter;    /**< 0–15 = single channel, PMH_CHANNEL_OMNI = all */
    PmhEventCallback callback; /**< Event callback (may be NULL)       */
    void *userdata;            /**< Passed verbatim to callback        */
} PmhInputStream;

/**
 * @brief An open PortMidi output stream (used for MIDI clock / sync output).
 *
 * Zero-initialise before use:
 * @code
 *   PmhOutputStream out = {0};
 * @endcode
 */
typedef struct PmhOutputStream {
    PortMidiStream *pm_stream; /**< Underlying PortMidi stream */
    int device_index;          /**< PortMidi device index      */
} PmhOutputStream;

/* --------------------------------------------------------------------------
 * 7.  Public API
 * ---------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* --- Lifecycle ----------------------------------------------------------- */

/**
 * @brief Initialise PortMidi and PortTime.
 * @return 0 on success, non-zero on failure (call pmh_last_error()).
 */
int pmh_init(void);

/**
 * @brief Shut down PortMidi and PortTime.  Close all streams first.
 */
void pmh_shutdown(void);

/* --- Device enumeration -------------------------------------------------- */

/**
 * @brief Return the number of MIDI devices visible to PortMidi.
 */
int pmh_device_count(void);

/**
 * @brief Fill @p out with information about device at @p index.
 * @return 0 on success, -1 if @p index is out of range.
 */
int pmh_device_get(int index, PmhDevice *out);

/**
 * @brief Find a device by (partial, case-insensitive) name and direction.
 * @return PortMidi device index, or -1 if not found.
 */
int pmh_device_find_by_name(const char *name, PmhDirection dir);

/**
 * @brief Print all available devices to stdout (useful for debugging).
 */
void pmh_device_list_print(void);

/* --- Input stream -------------------------------------------------------- */

/**
 * @brief Open a PortMidi input stream.
 *
 * @param s              Caller-allocated, zero-initialised stream struct.
 * @param device_index   PortMidi device index (from enumeration).
 * @param channel_filter 0–15 to receive only that channel, PMH_CHANNEL_OMNI for all.
 * @return 0 on success, non-zero on failure.
 */
int pmh_input_open(PmhInputStream *s, int device_index, uint8_t channel_filter);

/**
 * @brief Register (or replace) the event callback for an input stream.
 *
 * May be called before or after pmh_input_open().
 * Pass NULL to disable the callback.
 *
 * @param s        Target input stream.
 * @param cb       Callback function pointer (or NULL).
 * @param userdata Opaque pointer forwarded to @p cb on each call.
 */
void pmh_input_set_callback(PmhInputStream *s, PmhEventCallback cb, void *userdata);

/**
 * @brief Poll the input stream and fire the callback for each pending event.
 *
 * Call this from your SDL3 event/audio loop.  Non-blocking.
 *
 * @param s Input stream (must be open).
 * @return Number of events dispatched, or -1 on PortMidi error.
 */
int pmh_input_poll(PmhInputStream *s);

/**
 * @brief Close an input stream.  Safe to call on a zero-initialised struct.
 */
void pmh_input_close(PmhInputStream *s);

/* --- Output stream ------------------------------------------------------- */

/**
 * @brief Open a PortMidi output stream (for clock / sync output).
 *
 * @param s            Caller-allocated, zero-initialised stream struct.
 * @param device_index PortMidi device index.
 * @return 0 on success, non-zero on failure.
 */
int pmh_output_open(PmhOutputStream *s, int device_index);

/**
 * @brief Close an output stream.  Safe to call on a zero-initialised struct.
 */
void pmh_output_close(PmhOutputStream *s);

/* --- MIDI clock / sync --------------------------------------------------- */

/**
 * @brief Send a MIDI Timing Clock tick (0xF8) on the output stream.
 *        Call 24 times per quarter note at your target BPM.
 */
void pmh_clock_tick(PmhOutputStream *s);

/**
 * @brief Send MIDI Start (0xFA).
 */
void pmh_clock_start(PmhOutputStream *s);

/**
 * @brief Send MIDI Stop (0xFC).
 */
void pmh_clock_stop(PmhOutputStream *s);

/**
 * @brief Send MIDI Continue (0xFB).
 */
void pmh_clock_continue(PmhOutputStream *s);

/* --- Error helpers ------------------------------------------------------- */

/**
 * @brief Human-readable string for a PmError code.
 * @return Never NULL.
 */
const char *pmh_strerror(PmError err);

/**
 * @brief Return the last error string set by any pmh_* function.
 * @return Never NULL.  Empty string if no error has occurred.
 */
const char *pmh_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTMIDI_HELPER_H */
