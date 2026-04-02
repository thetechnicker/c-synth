#ifndef SYNTH_H
#define SYNTH_H

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#define SR 48000
#define CH 2
#define BLOCK 256 // process in blocks for better vectorization

typedef struct {
    float phase;
    float freq;
    float amp;
    float sr;
} Osc;

int synth_thread(void *data);

#endif
