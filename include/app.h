#pragma once

#include "argparse.h"
#include <SDL3/SDL.h>
#include <portmidi.h>

typedef struct App {
    SDL_Window *window;
    PortMidiStream *stream;
    ArgParse *ap;
} App_t;
