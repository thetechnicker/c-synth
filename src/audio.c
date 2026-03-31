#include "audio.h"
#include "log.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL.h>

static void print_audio_devices(const char *label, SDL_AudioDeviceID *devices, int count) {
    printf("%s devices (%i):\n", label, count);
    for (int i = 0; i < count; i++) {
        const char *name = SDL_GetAudioDeviceName(devices[i]);
        if (name) {
            printf("\t[%u] %s\n", devices[i], name);
        } else {
            printf("\t[%u] <error: %s>\n", devices[i], SDL_GetError());
        }
    }
}

int audio_thread(void *data) {
    (void)data;
    SDL

    // --- Drivers ---
    int num_audio = SDL_GetNumAudioDrivers();
    printf("Number of audio drivers: %i\n", num_audio);
    for (int i = 0; i < num_audio; i++) {
        const char *name = SDL_GetAudioDriver(i);
        printf("\t- %s\n", name);
    }

    const char *current_driver = SDL_GetCurrentAudioDriver();
    printf("Current driver: %s\n", current_driver);

    // --- Playback devices ---
    int playback_count = 0;
    SDL_AudioDeviceID *playback = SDL_GetAudioPlaybackDevices(&playback_count);
    if (!playback) {
        printf("Failed to get playback devices: %s\n", SDL_GetError());
    } else {
        print_audio_devices("Playback", playback, playback_count);
        SDL_free(playback);
    }

    // --- Recording devices ---
    int recording_count = 0;
    SDL_AudioDeviceID *recording = SDL_GetAudioRecordingDevices(&recording_count);
    if (!recording) {
        printf("Failed to get recording devices: %s\n", SDL_GetError());
    } else {
        print_audio_devices("Recording", recording, recording_count);
        SDL_free(recording);
    }

    return 0;
}
