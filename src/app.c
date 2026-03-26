#include "app.h"
#include "argparse.h"
#include "log.h"
#include "midi.h"
#include "version.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_TITLE_LEN 256

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    log_init("app.log.jsonl", LOG_DEBUG);

    ArgParse *ap = malloc(sizeof(ArgParse));
    CHECK_ERR_SDL(argparse_init(ap, "C-Synth", NULL, NULL, 0));
    AppConfig_t *conf = malloc(sizeof(AppConfig_t));
    memset(conf, 0, sizeof(AppConfig_t));
    create_app_argparse(ap, conf);

    SDL_AppResult res = argparse_parse(argc, argv, ap);
    if (res != SDL_APP_CONTINUE) {
        return res;
    }
    LOGD("%dx%d; %llx", conf->screen_width, conf->screen_height, conf->flags);

    return SDL_APP_SUCCESS;

    App_t *app = malloc(sizeof(App_t));
    app->ap = ap;

    char title[MAX_TITLE_LEN];
    sprintf(title, "Version: %s (%s)%s, built: %s", PROJECT_GIT_DESCRIBE, PROJECT_GIT_COMMIT,
            PROJECT_GIT_DIRTY ? "-dirty" : "", PROJECT_BUILD_TIMESTAMP);
    SDL_Window *window = SDL_CreateWindow(title, 800, 600, SDL_WINDOW_FULLSCREEN);

    if (!window)
        goto failure;

    app->window = window;

    *appstate = app;

    return SDL_APP_CONTINUE;

failure:
    free(app);
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    // App_t *app = (App_t *)appstate;
    // PmEvent buffer[32];
    // int count = Pm_Read(app->stream, buffer, 32);
    // if (count < 0) {
    //     fprintf(stderr, "Pm_Read error: %d\n", count);
    //     return SDL_APP_FAILURE;
    // }
    // for (int i = 0; i < count; ++i) {
    //     midi_message_unpacked_t msg =
    //     midi_from_uint32(buffer[i].message).msg;

    //    printf("message: data1: %u, data2: %u, padding: %u, status: %u\n",
    //           msg.data1, msg.data2, msg._pad, msg.status.raw);

    //    switch (msg.status.type) {
    //    case 0x8:
    //        printf("Note Off  ch=%u note=%u vel=%u\n", msg.status.channel + 1,
    //               msg.data1, msg.data2);
    //        break;
    //    case 0x9:
    //        printf("Note On   ch=%u note=%u vel=%u\n", msg.status.channel + 1,
    //               msg.data1, msg.data2);
    //        break;
    //    case 0xB:
    //        printf("Control    ch=%u ctrl=%u val=%u\n", msg.status.channel +
    //        1,
    //               msg.data1, msg.data2);
    //        break;
    //    case 0xC:
    //        printf("ProgramCh  ch=%u prog=%u\n", msg.status.channel + 1,
    //               msg.data1);
    //        break;
    //    default:
    //        printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n",
    //               msg.status.type, msg.status.channel + 1, msg.data1,
    //               msg.data2);
    //    }
    //}
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;

    char buf[256];
    SDL_GetEventDescription(event, buf, sizeof(buf));
    LOGD("Received event: %s", buf);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
}

void create_app_argparse(ArgParse *ap, AppConfig_t *conf) {
    (void)conf;
    add_kw_argument(ap, "screen-width", "Size of the window", VAL_INT, true, 'w', false, NULL);
    add_kw_argument(ap, "screen-height", "", VAL_INT, true, 'h', false, NULL);
    add_flaglist(ap, "sdl-window-flags", "SDL Window Flags", 0);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_FULLSCREEN           ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_OPENGL               ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_OCCLUDED             ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_HIDDEN               ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_BORDERLESS           ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_RESIZABLE            ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MINIMIZED            ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MAXIMIZED            ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MOUSE_GRABBED        ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_INPUT_FOCUS          ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MOUSE_FOCUS          ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_EXTERNAL             ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MODAL                ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_HIGH_PIXEL_DENSITY   ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MOUSE_CAPTURE        ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_MOUSE_RELATIVE_MODE  ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_ALWAYS_ON_TOP        ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_UTILITY              ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_TOOLTIP              ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_POPUP_MENU           ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_KEYBOARD_GRABBED     ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_FILL_DOCUMENT        ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_VULKAN               ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_METAL                ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_TRANSPARENT          ", true, '\0', NULL);
    add_flaglist_flag(ap, "sdl-window-flags", "SDL_WINDOW_NOT_FOCUSABLE        ", true, '\0', NULL);
}

/*
 * kept for later reuse
 *
 * PmError err;
 * err = Pm_Initialize();
 * if (err != pmNoError) {
 *     fprintf(stderr, "Pm_Initialize failed: %s\n", Pm_GetErrorText(err));
 *     return 1;
 * }
 *
 * int num_devs = Pm_CountDevices();
 * if (num_devs <= 0) {
 *     fprintf(stderr, "No MIDI devices found\n");
 *     Pm_Terminate();
 *     goto failure;
 * }
 *
 * const PmDeviceInfo *info;
 * int input_id = -1;
 * for (int i = 0; i < num_devs; ++i) {
 *     info = Pm_GetDeviceInfo(i);
 *     if (info && info->input) {
 *         input_id = i;
 *         break;
 *     }
 * }
 * if (input_id < 0) {
 *     fprintf(stderr, "No MIDI input devices available\n");
 *     Pm_Terminate();
 *     goto failure;
 * }
 *
 * PmStream *stream;
 * err = Pm_OpenInput(&stream, input_id, NULL, 512, NULL, NULL);
 * if (err != pmNoError) {
 *     fprintf(stderr, "Pm_OpenInput failed: %s\n", Pm_GetErrorText(err));
 *     Pm_Terminate();
 *     goto failure;
 * }
 *
 * printf("Opened input: %s\n", Pm_GetDeviceInfo(input_id)->name);
 *
 * app->stream = stream;
 */
