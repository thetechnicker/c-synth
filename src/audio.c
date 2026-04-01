#include "audio.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

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

static void print_audio_setup(void) {
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
    SDL_AudioDeviceID sdl_audio_device_default_playback = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    SDL_AudioDeviceID sdl_audio_device_default_recording = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    print_audio_devices("Default Playback", &sdl_audio_device_default_playback, 1);
    print_audio_devices("Default Recording", &sdl_audio_device_default_recording, 1);
}

int audio_thread(void *data) {
    (void)data;
    print_audio_setup();

    while (1) {
    }
    return 0;
}
