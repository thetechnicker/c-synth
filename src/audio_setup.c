#include "audio_setup.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <stdio.h>

static void print_devices(const char *label, SDL_AudioDeviceID *ids, int count) {
    printf("%s devices (%d):\n", label, count);
    for (int i = 0; i < count; i++) {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        if (name)
            printf("\t[%u] %s\n", ids[i], name);
        else
            printf("\t[%u] <error: %s>\n", ids[i], SDL_GetError());
    }
}

void audio_setup_print(void) {
    /* Drivers */
    int n = SDL_GetNumAudioDrivers();
    printf("Audio drivers (%d):\n", n);
    for (int i = 0; i < n; i++)
        printf("\t- %s\n", SDL_GetAudioDriver(i));
    printf("Current driver: %s\n", SDL_GetCurrentAudioDriver());

    /* Playback */
    int pc = 0;
    SDL_AudioDeviceID *playback = SDL_GetAudioPlaybackDevices(&pc);
    if (playback) {
        print_devices("Playback", playback, pc);
        SDL_free(playback);
    } else {
        printf("Failed to get playback devices: %s\n", SDL_GetError());
    }

    /* Recording */
    int rc = 0;
    SDL_AudioDeviceID *recording = SDL_GetAudioRecordingDevices(&rc);
    if (recording) {
        print_devices("Recording", recording, rc);
        SDL_free(recording);
    } else {
        printf("Failed to get recording devices: %s\n", SDL_GetError());
    }

    /* Defaults */
    SDL_AudioDeviceID def_pb  = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    SDL_AudioDeviceID def_rec = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    print_devices("Default Playback",   &def_pb,  1);
    print_devices("Default Recording",  &def_rec, 1);
}
