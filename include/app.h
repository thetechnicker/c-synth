#pragma once

#include <SDL3/SDL.h>
#include <portmidi.h>

typedef struct App {
    SDL_Window *window;
    PortMidiStream *stream;
} App_t;
