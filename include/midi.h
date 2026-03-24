#include <stdint.h>

typedef struct midi_config {
    void *nothing;
} midi_config_t;

typedef struct midi_note {
    union {
        uint32_t msg;
        struct {
            union {
                uint8_t status;
                struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                    uint8_t channel : 4;
                    uint8_t type : 4;
#else
                    uint8_t type : 4;
                    uint8_t channel : 4;
#endif
                };
            };
            uint8_t data1;
            uint8_t data2;
            uint8_t data3;
        };
    };
} midi_note_t;
