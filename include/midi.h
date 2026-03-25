/**
 * @file midi_message.h
 * @brief MIDI message type with raw and unpacked views.
 *
 * Wire format (per MIDI 1.0 spec):
 *   byte[0] = status  (0xTN — T = type nibble, N = channel nibble)
 *   byte[1] = data1
 *   byte[2] = data2
 *   byte[3] = unused / padding
 *
 * Requires: C11
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 1.  Status byte — raw uint8 + type/channel nibbles
 * ---------------------------------------------------------------------- */

typedef union midi_status {
    uint8_t raw; /**< Full status byte, e.g. 0x9E = note-on ch 14 */

    struct {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        uint8_t type    : 4; /**< Message type  (high nibble) */
        uint8_t channel : 4; /**< MIDI channel  (low nibble)  */
#else
        uint8_t channel : 4; /**< MIDI channel  (low nibble)  */
        uint8_t type    : 4; /**< Message type  (high nibble) */
#endif
    };
} midi_status_t;

static_assert(sizeof(midi_status_t) == 1, "midi_status_t must be 1 byte");

/* -------------------------------------------------------------------------
 * 2.  Known message types (high nibble of status byte)
 * ---------------------------------------------------------------------- */

typedef enum midi_msg_type {
    MIDI_NOTE_OFF         = 0x8,
    MIDI_NOTE_ON          = 0x9,
    MIDI_POLY_PRESSURE    = 0xA,
    MIDI_CONTROL_CHANGE   = 0xB,
    MIDI_PROGRAM_CHANGE   = 0xC,
    MIDI_CHANNEL_PRESSURE = 0xD,
    MIDI_PITCH_BEND       = 0xE,
    MIDI_SYSTEM           = 0xF, /**< SysEx / system messages */
} midi_msg_type_t;

/* -------------------------------------------------------------------------
 * 3.  Unpacked message — named fields
 * ---------------------------------------------------------------------- */

typedef struct midi_message_unpacked {
    midi_status_t status; /**< Status byte with nibble view */
    uint8_t       data1;  /**< First data byte  (e.g. note number) */
    uint8_t       data2;  /**< Second data byte (e.g. velocity)    */
    uint8_t       _pad;   /**< Reserved — always zero on wire      */
} midi_message_unpacked_t;

static_assert(sizeof(midi_message_unpacked_t) == 4,  "unpacked size must be 4");
static_assert(offsetof(midi_message_unpacked_t, status) == 0, "bad status offset");
static_assert(offsetof(midi_message_unpacked_t, data1)  == 1, "bad data1 offset");
static_assert(offsetof(midi_message_unpacked_t, data2)  == 2, "bad data2 offset");

/* -------------------------------------------------------------------------
 * 4.  Top-level message — raw bytes OR unpacked view
 * ---------------------------------------------------------------------- */

typedef union midi_message {
    uint8_t               bytes[4];  /**< Wire-safe byte access  */
    midi_message_unpacked_t msg;      /**< Named field access      */
} midi_message_t;

static_assert(sizeof(midi_message_t) == 4, "midi_message_t must be 4 bytes");

/* -------------------------------------------------------------------------
 * 5.  Helpers
 * ---------------------------------------------------------------------- */

/**
 * @brief Pack individual fields into a midi_message_t.
 */
static inline midi_message_t midi_pack(
    midi_msg_type_t type,
    uint8_t         channel,
    uint8_t         data1,
    uint8_t         data2)
{
    midi_message_t m;
    m.msg.status.type    = (uint8_t)type;
    m.msg.status.channel = channel & 0x0F;
    m.msg.data1          = data1;
    m.msg.data2          = data2;
    m.msg._pad           = 0;
    return m;
}

/**
 * @brief Unpack a raw 4-byte buffer (e.g. from serial read) into a midi_message_t.
 * @note  Always use this instead of casting — avoids aliasing UB.
 */
static inline midi_message_t midi_from_bytes(const uint8_t buf[4])
{
    midi_message_t m;
    memcpy(&m, buf, 4);
    return m;
}

/**
 * @brief Write a midi_message_t into a raw 4-byte buffer for wire transmission.
 */
static inline void midi_to_bytes(const midi_message_t *m, uint8_t buf[4])
{
    memcpy(buf, m, 4);
}

/**
 * @brief Convert a PortMidi-style uint32_t PmMessage to midi_message_t.
 *
 * PmMessage byte layout (always, regardless of endianness):
 *   bits  7:0  = status
 *   bits 15:8  = data1
 *   bits 23:16 = data2
 *   bits 31:24 = unused
 *
 * @note  Uses masking/shifting — never casts the uint32_t directly.
 */
static inline midi_message_t midi_from_uint32(uint32_t pm)
{
    return midi_pack(
        (midi_msg_type_t)((pm & 0xF0u) >> 4), /* type nibble from status */
        (uint8_t) (pm & 0x0Fu),                /* channel nibble          */
        (uint8_t)((pm >>  8) & 0xFFu),         /* data1                   */
        (uint8_t)((pm >> 16) & 0xFFu)          /* data2                   */
    );
}
