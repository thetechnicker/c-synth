#include "app.h"
#include "argparse.h"
#include "log.h"
#include "ui.h"
#include "ui_widgets.h"
// #include "midi.h"
#include "version.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_timer.h>

#define MAX_TITLE_LEN 256
#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint32_t dt = 0;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    // SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO); // idk if this line is needed, no difference found
    // when uncommented
    log_init("app.log.jsonl", LOG_DEBUG);
    LOGI("Application is starting up ...");
    LOGD("Application data at: %s", SDL_GetBasePath());
    LOGD("Current workdir is: %s", SDL_GetCurrentDirectory());

    ArgParse *ap = malloc(sizeof(ArgParse));
    CHECK_ERR_SDL(argparse_init(ap, "C-Synth", NULL, NULL, 0));
    create_app_argparse(ap);
    HashMap *map = hashmap_new(0);

    SDL_AppResult res = argparse_parse(argc, argv, ap, map);
    if (res != SDL_APP_CONTINUE) {
        return res;
    }

    App_t *app = malloc(sizeof(App_t));
    if (!app) {
        LOGE("Cannot create app: %s", strerror(ENOMEM));
    }
    app->config = map;

    char title[MAX_TITLE_LEN];
    sprintf(title, "Version: %s (%s)%s, built: %s", PROJECT_GIT_DESCRIBE, PROJECT_GIT_COMMIT,
            PROJECT_GIT_DIRTY ? "-dirty" : "", PROJECT_BUILD_TIMESTAMP);

    // const char *renderer = hashmap_get(app->config, "renderer");
    // const char *renderer = hashmap_get(app->config, "renderer");
    // if (!renderer || strcmp(renderer, "default")) {
    //     app->renderer = ui_get_default_renderer();
    // } else {
    //     app->renderer = ui_get_renderer(renderer);
    // }
    app->renderer = ui_get_default_renderer();
    app->renderer->init(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, title);
    ui_ctx_init(&app->ui, app->renderer);

    *appstate = app;
    LOGI("Application startup complete");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    App_t *app = (App_t *)appstate;
    (void)app;
    static uint32_t last_ticks = 0;
    static bool initialized = false;

    uint32_t now = SDL_GetTicks();
    if (!initialized) {
        last_ticks = now;
        initialized = true;
    }

    /* use global dt if set (ms); otherwise compute from ticks */
    float dt_s = 0.0f;
    if (dt > 0) {
        dt_s = dt / 1000.0f;
    } else {
        dt_s = (now - last_ticks) / 1000.0f;
    }
    if (dt_s > 0.05f)
        dt_s = 0.05f; /* clamp large frames */
    last_ticks = now;

    app->renderer->begin_frame();
    ui_ctx_begin_frame(&app->ui);

    static float freq = 440.f;
    static float cutoff = 10.f;
    static bool osc_on = false;

    ui_layout_begin_row(&app->ui, 20.f, 20.f, 160.f, 32.f, 8.f);
    ui_toggle(&app->ui, 0, 0, 0, 0, "OSC", &osc_on);
    ui_slider_f(&app->ui, 0, 0, 0, 0, "Freq", &freq, 20.f, 20000.f, "%.0f Hz");
    ui_layout_end_row(&app->ui);

    ui_knob(&app->ui, 20.f, 70.f, 64.f, "Cutoff", &cutoff, 20.f, 20000.f, 200.f, "%.0f");

    // End frame (renderer should draw in ui_ctx_end_frame)
    ui_ctx_end_frame(&app->ui);
    app->renderer->end_frame();

    LOGD("Frame dt_ms: %.1f volume: %.1f freq: %.1f paused:%d", dt_s * 1000.0f, cutoff, freq,
         osc_on);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    App_t *app = (App_t *)appstate;
    ui_ctx_feed_event(&app->ui, event);
    char buf[256];
    SDL_GetEventDescription(event, buf, sizeof(buf));
    LOGD("Received event: %s", buf);
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN: {
        app->buttonstates[event->key.scancode] = true;
        break;
    }
    case SDL_EVENT_KEY_UP: {
        app->buttonstates[event->key.scancode] = false;
        break;
    }

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    App_t *app = (App_t *)appstate;
    LOGD("Exiting");
    if (app->config) {
        hashmap_free(app->config);
    }
    if (app->stream) {
        Pm_Close(app->stream);
    }
    Pm_Terminate();
    if (app->renderer) {
        app->renderer->shutdown();
    }
    free(app);

    log_shutdown();
}

void create_app_argparse(ArgParse *ap) {
    argparse_add_value(ap, "renderer", "rendering backedn used", false, true, 0, "default");
    argparse_add_value(ap, "window-width", "", false, true, 'w', DEFAULT_WINDOW_WIDTH);
    argparse_add_value(ap, "window-height", "", false, true, 'h', DEFAULT_WINDOW_HEIGHT);
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
 *
 *  PmEvent buffer[32];
 *  int count = Pm_Read(app->stream, buffer, 32);
 *  if (count < 0) {
 *      fprintf(stderr, "Pm_Read error: %d\n", count);
 *      return SDL_APP_FAILURE;
 *  }
 *  for (int i = 0; i < count; ++i) {
 *      midi_message_unpacked_t msg =
 *      midi_from_uint32(buffer[i].message).msg;
 *
 *     printf("message: data1: %u, data2: %u, padding: %u, status: %u\n",
 *            msg.data1, msg.data2, msg._pad, msg.status.raw);
 *
 *     switch (msg.status.type) {
 *     case 0x8:
 *         printf("Note Off  ch=%u note=%u vel=%u\n", msg.status.channel + 1,
 *                msg.data1, msg.data2);
 *         break;
 *     case 0x9:
 *         printf("Note On   ch=%u note=%u vel=%u\n", msg.status.channel + 1,
 *                msg.data1, msg.data2);
 *         break;
 *     case 0xB:
 *         printf("Control    ch=%u ctrl=%u val=%u\n", msg.status.channel +
 *         1,
 *                msg.data1, msg.data2);
 *         break;
 *     case 0xC:
 *         printf("ProgramCh  ch=%u prog=%u\n", msg.status.channel + 1,
 *                msg.data1);
 *         break;
 *     default:
 *         printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n",
 *                msg.status.type, msg.status.channel + 1, msg.data1,
 *                msg.data2);
 *     }
 */
