#pragma once

#include "argparse.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <portmidi.h>
#include <string.h>

#define CHECK_ERR_SDL(err)                                                     \
    do {                                                                       \
        int _check_err_val = (err);                                            \
        if (_check_err_val != 0) {                                             \
            const char *_check_err_msg = (_check_err_val > 0)                  \
                                             ? strerror(_check_err_val)        \
                                             : strerror(errno);                \
            LOGE("POSIX error %d: %s", _check_err_val, _check_err_msg);        \
            return SDL_APP_FAILURE;                                            \
        }                                                                      \
    } while (0)

typedef struct AppConfig {
    int screen_width;
    int screen_height;
    union {
        uint32_t flags;
        struct {
            uint32_t fullscreen : 1;
            uint32_t resizable : 1;
            uint32_t padding : 30;
        };
    };
} AppConfig_t;

typedef struct App {
    SDL_Window *window;
    PortMidiStream *stream;
    ArgParse *ap;
} App_t;

void create_app_argparse(ArgParse *ap, AppConfig_t *conf);
