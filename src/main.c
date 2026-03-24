#include <stdint.h>
#include <stdio.h>
//#include <stdlib.h>
#include "version.h"
#include "portmidi.h"

int main(void) {
    printf("Version: %s (%s)%s, built: %s\n", PROJECT_GIT_DESCRIBE,
           PROJECT_GIT_COMMIT, PROJECT_GIT_DIRTY ? "-dirty" : "",
           PROJECT_BUILD_TIMESTAMP);

    PmError err;
    err = Pm_Initialize();
    if (err != pmNoError) {
        fprintf(stderr, "Pm_Initialize failed: %s\n", Pm_GetErrorText(err));
        return 1;
    }

    int num_devs = Pm_CountDevices();
    if (num_devs <= 0) {
        fprintf(stderr, "No MIDI devices found\n");
        Pm_Terminate();
        return 1;
    }

    const PmDeviceInfo *info;
    int input_id = -1;
    for (int i = 0; i < num_devs; ++i) {
        info = Pm_GetDeviceInfo(i);
        if (info && info->input) {
            input_id = i;
            break;
        }
    }
    if (input_id < 0) {
        fprintf(stderr, "No MIDI input devices available\n");
        Pm_Terminate();
        return 1;
    }

    PmStream *stream;
    err = Pm_OpenInput(&stream, input_id, NULL, 512, NULL, NULL);
    if (err != pmNoError) {
        fprintf(stderr, "Pm_OpenInput failed: %s\n", Pm_GetErrorText(err));
        Pm_Terminate();
        return 1;
    }

    printf("Opened input: %s\n", Pm_GetDeviceInfo(input_id)->name);
    int desired_channel =
        1; // use 1..16 for user-facing channel; change as needed
    int desired_channel0 = desired_channel - 1; // 0..15 internal

    while (1) {
        PmEvent buffer[32];
        int count = Pm_Read(stream, buffer, 32);
        if (count < 0) {
            fprintf(stderr, "Pm_Read error: %d\n", count);
            break;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t msg = (uint32_t)buffer[i].message;
            uint8_t status = msg & 0xFF;
            uint8_t data1 = (msg >> 8) & 0xFF;
            uint8_t data2 = (msg >> 16) & 0xFF;
            uint8_t type = status & 0xF0;
            uint8_t channel = status & 0x0F; // 0..15

            if (channel != desired_channel0)
                continue;

            switch (type) {
            case 0x80:
                printf("Note Off  ch=%u note=%u vel=%u\n", channel + 1, data1,
                       data2);
                break;
            case 0x90:
                printf("Note On   ch=%u note=%u vel=%u\n", channel + 1, data1,
                       data2);
                break;
            case 0xB0:
                printf("Control    ch=%u ctrl=%u val=%u\n", channel + 1, data1,
                       data2);
                break;
            case 0xC0:
                printf("ProgramCh  ch=%u prog=%u\n", channel + 1, data1);
                break;
            default:
                printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n", type,
                       channel + 1, data1, data2);
            }
        }
        //Pm_Sleep(10);
    }

    Pm_Close(stream);
    Pm_Terminate();
    return 0;
}
