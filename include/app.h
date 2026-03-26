#pragma once

#include "argparse.h"
#include "hashmap.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <portmidi.h>
#include <string.h>

#define CHECK_ERR_SDL(err)                                                                         \
    do {                                                                                           \
        int _check_err_val = (err);                                                                \
        if (_check_err_val != 0) {                                                                 \
            const char *_check_err_msg =                                                           \
                (_check_err_val > 0) ? strerror(_check_err_val) : strerror(errno);                 \
            LOGE("POSIX error %d: %s", _check_err_val, _check_err_msg);                            \
            return SDL_APP_FAILURE;                                                                \
        }                                                                                          \
    } while (0)

typedef struct App {
    SDL_Window *window;
    PortMidiStream *stream;
    HashMap config;
} App_t;

void create_app_argparse(ArgParse *ap);
