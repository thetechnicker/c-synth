#include "midi.h"
#include "synth.h"
#include "portmidi.h"
#include "porttime/porttime.h"
#include "version.h"
#include <stdint.h>
#include <stdio.h>

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

    printf("starting loop\n");
    while (1) {
        PmEvent buffer[32];
        int count = Pm_Read(stream, buffer, 32);
        if (count < 0) {
            fprintf(stderr, "Pm_Read error: %d\n", count);
            break;
        }
        for (int i = 0; i < count; ++i) {
            midi_note_t msg = {.msg = (uint32_t)buffer[i].message};

            printf("message: data1: %u, data2: %u, data3: %u, status: %u\n",
                   msg.data1, msg.data2, msg.data3, msg.status);

            switch (msg.type) {
            case 0x8:
                printf("Note Off  ch=%u note=%u vel=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0x9:
                printf("Note On   ch=%u note=%u vel=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0xB:
                printf("Control    ch=%u ctrl=%u val=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0xC:
                printf("ProgramCh  ch=%u prog=%u\n", msg.channel + 1,
                       msg.data1);
                break;
            default:
                printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n", msg.type,
                       msg.channel + 1, msg.data1, msg.data2);
            }
        }
        Pt_Sleep(10);
    }

    Pm_Close(stream);
    Pm_Terminate();
    return 0;
}
