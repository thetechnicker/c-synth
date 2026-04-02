#include "synth.h"
#include "log.h"
#include "thread.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static Osc osc;

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

static inline void osc_init(Osc *o, float freq, float amp, float sr) {
    o->phase = 0.0f;
    o->freq = freq;
    o->amp = amp;
    o->sr = sr;
}

static void fill_sine_block(Osc *o, float *__restrict buf, int frames) {
    float phase = o->phase;
    const float sr = o->sr;
    for (int i = 0; i < frames; ++i) {
        float phase_inc = o->freq / sr;
        float s = o->amp * sinf(2.0f * M_PI * phase); // <-- only change
        buf[2 * i + 0] = s;
        buf[2 * i + 1] = s;
        phase += phase_inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        else if (phase < 0.0f)
            phase += 1.0f;
    }
    o->phase = phase;
}

void SDLCALL synth_callback(void *userdata, SDL_AudioStream *stream, int additional_amount,
                            int total_amount) {
    (void)userdata;
    (void)total_amount;
    int frames = additional_amount > 0 ? additional_amount : BLOCK;
    size_t samples = (size_t)frames * CH;
    size_t bytes = samples * sizeof(float);

    float *block = (float *)calloc(samples, sizeof(float));
    if (!block) {
        LOGE("calloc failed for audio block (%zu bytes)", bytes);
        return;
    }

    fill_sine_block(&osc, block, frames);

    /* SDL_PutAudioStreamData returns the number of bytes written on success,
       or a negative SDL error code; we ignore it here but you may want to check. */
    if (SDL_PutAudioStreamData(stream, block, (int)bytes)) {
        LOGE("SDL_PutAudioStreamData failed: %s", SDL_GetError());
    }

    free(block);
}

int synth_thread(void *data) {
    thread_t *this = data;
    print_audio_setup();

    osc_init(&osc, 220.0f, 0.2f, (float)SR);
    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL, synth_callback, NULL);
    if (!stream) {
        LOGE("Failed to open Audio Stream");
        return 1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    LOGD("Audio Stream Started");

    while (SDL_GetAtomicInt(&this->running) != 0) {
        SDL_Delay(10);
    }

    LOGD("Audio Stream Ended");
    // free(block);
    SDL_DestroyAudioStream(stream);
    return 0;
}
