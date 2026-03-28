#include "app.h"
#include "argparse.h"
#include "log.h"
#include "ui.h"
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

    *appstate = app;
    LOGI("Application startup complete");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    App_t *app = (App_t *)appstate;
    static uint32_t last_ticks = 0;
    static float x = DEFAULT_WINDOW_WIDTH * 0.5f;
    static float y = DEFAULT_WINDOW_HEIGHT * 0.35f;
    static float vx = 120.0f; /* px/sec */
    static float vy = 0.0f;
    static float gravity = 800.0f;
    static float bounce = -0.65f;
    static float wobble = 0.0f; /* for face squash/tilt */
    static bool initialized = false;

    uint32_t now = SDL_GetTicks();
    if (!initialized) {
        last_ticks = now;
        initialized = true;
    }
    float dt_s = (now - last_ticks) / 1000.0f;
    if (dt_s > 0.05f)
        dt_s = 0.05f; /* clamp big frames */
    last_ticks = now;

    /* physics: horizontal wrap, vertical bounce on "floor" */
    x += vx * dt_s;
    vy += gravity * dt_s;
    y += vy * dt_s;

    /* floor at 80% window height */
    float floor_y = DEFAULT_WINDOW_HEIGHT * 0.80f;
    if (y > floor_y) {
        y = floor_y;
        vy *= bounce;
        /* create wobble proportional to impact */
        wobble = fminf(1.0f, fabsf(vy) / 600.0f);
    }

    /* simple horizontal wrap */
    if (x < 0)
        x = DEFAULT_WINDOW_WIDTH;
    if (x > DEFAULT_WINDOW_WIDTH)
        x = 0;

    /* gentler horizontal speed oscillation for funky hopping */
    vx += sinf(now / 500.0f) * 8.0f;

    /* decay wobble */
    wobble *= 0.9f;

    app->renderer->begin_frame();

    /* background */
    app->renderer->draw_rect(0, 0, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, UI_RGB(20, 30, 48));

    /* ground strip */
    app->renderer->draw_rect(0, floor_y + 40, DEFAULT_WINDOW_WIDTH,
                             DEFAULT_WINDOW_HEIGHT - (floor_y + 40), UI_RGB(18, 110, 50));

    /* compute face size and squash based on vertical speed / wobble */
    float face_base_size = 140.0f;
    float squash = 1.0f - wobble * 0.35f - fminf(0.2f, fabsf(vy) / 400.0f);
    float stretch = 1.0f + (1.0f - squash) * 0.6f;
    float w = face_base_size * (1.0f + 0.08f * sinf(now / 120.0f));
    float h = face_base_size * (squash * 0.9f + stretch * 0.1f);

    /* face bounding box (centered on x,y) */
    float fx = x - w * 0.5f;
    float fy = y - h * 0.5f;

    /* wobbly face color that changes with time */
    uint32_t face_r = (uint32_t)(220 + 20 * sinf(now / 300.0f));
    uint32_t face_g = (uint32_t)(200 + 40 * cosf(now / 700.0f));
    uint32_t face_b = 80;
    ui_color_t face_col = UI_RGB(face_r & 0xFF, face_g & 0xFF, face_b);

    /* draw face as a rounded-looking rectangle: central rect + soft edges by overlapping smaller
     * rects */
    app->renderer->draw_rect(fx, fy, w, h, face_col);

    /* subtle highlight */
    app->renderer->draw_rect(fx + w * 0.08f, fy + h * 0.06f, w * 0.35f, h * 0.18f,
                             UI_RGBA(255, 255, 255, 24));

    /* eyes */
    float eye_w = w * 0.13f;
    float eye_h = h * 0.18f;
    float eye_y = fy + h * 0.32f;
    float eye_xl = fx + w * 0.27f - eye_w * 0.5f;
    float eye_xr = fx + w * 0.73f - eye_w * 0.5f;

    /* blink effect: use sine to occasionally close */
    float blink = (sinf(now / 350.0f) > 0.95f) ? 0.08f : 1.0f;
    app->renderer->draw_rect(eye_xl, eye_y, eye_w, eye_h * blink, UI_RGB(10, 10, 10));
    app->renderer->draw_rect(eye_xr, eye_y, eye_w, eye_h * blink, UI_RGB(10, 10, 10));

    /* pupils that track horizontal motion a little */
    float px_offset = fmaxf(-eye_w * 0.12f, fminf(eye_w * 0.12f, vx / 400.0f * eye_w));
    app->renderer->draw_rect(eye_xl + eye_w * 0.25f + px_offset, eye_y + eye_h * 0.25f,
                             eye_w * 0.25f, eye_h * 0.5f * blink, UI_RGB(255, 255, 255));
    app->renderer->draw_rect(eye_xr + eye_w * 0.25f + px_offset, eye_y + eye_h * 0.25f,
                             eye_w * 0.25f, eye_h * 0.5f * blink, UI_RGB(255, 255, 255));

    /* silly nose */
    float nose_w = w * 0.08f;
    float nose_h = h * 0.06f;
    app->renderer->draw_rect(fx + w * 0.5f - nose_w * 0.5f, fy + h * 0.47f, nose_w, nose_h,
                             UI_RGB(255, 120, 60));

    /* mouth as an arc made of short lines — more amplitude when in the air */
    int segments = 18;
    float mouth_r = w * 0.32f;
    float mouth_cx = fx + w * 0.5f;
    float mouth_cy = fy + h * 0.68f + (vy < 0 ? -5.0f : 6.0f) * (1.0f - squash);
    float smile_amplitude = 1.0f + fminf(0.8f, fabsf(vy) / 400.0f);
    float start_ang = M_PI * 0.2f;
    float end_ang = M_PI * 0.8f;
    ui_color_t mouth_col = UI_RGB(30, 10, 10);
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / segments;
        float t1 = (float)(i + 1) / segments;
        float a0 = start_ang + (end_ang - start_ang) * t0;
        float a1 = start_ang + (end_ang - start_ang) * t1;
        float r0 = mouth_r * (1.0f + 0.06f * sinf(now / 140.0f + i));
        float r1 = mouth_r * (1.0f + 0.06f * sinf(now / 140.0f + i + 1));
        float x0 = mouth_cx + cosf(a0) * r0;
        float y0 = mouth_cy + sinf(a0) * r0 * 0.35f * smile_amplitude;
        float x1 = mouth_cx + cosf(a1) * r1;
        float y1 = mouth_cy + sinf(a1) * r1 * 0.35f * smile_amplitude;
        app->renderer->draw_line(x0, y0, x1, y1, mouth_col, 4.0f);
    }

    /* silly eyebrow wiggle (lines) */
    float brow_y = fy + h * 0.18f - (1.0f - squash) * 6.0f;
    app->renderer->draw_line(eye_xl - 4, brow_y, eye_xl + eye_w + 4,
                             brow_y - 6.0f * sinf(now / 180.0f), UI_RGB(10, 10, 10), 3.0f);
    app->renderer->draw_line(eye_xr - 4, brow_y - 6.0f * sinf(now / 160.0f + 1.2f),
                             eye_xr + eye_w + 4, brow_y, UI_RGB(10, 10, 10), 3.0f);

    /* a small shadow under the face */
    float sh_w = w * 1.05f;
    float sh_h = h * 0.12f * (1.0f - wobble * 0.6f);
    app->renderer->draw_rect(fx + (w - sh_w) * 0.5f, floor_y + 8.0f, sh_w, sh_h,
                             UI_RGBA(10, 10, 10, 96));

    /* a funny little caption made from blocks (since font may be unavailable) */
    const char *caption = "BOING!";
    /* draw caption as block letters using simple rectangles */
    float cap_w = 220.0f;
    float cap_h = 36.0f;
    float cap_x = DEFAULT_WINDOW_WIDTH * 0.5f - cap_w * 0.5f;
    float cap_y = 18.0f + 6.0f * sinf(now / 400.0f);
    app->renderer->draw_rect(cap_x - 8, cap_y - 8, cap_w + 16, cap_h + 16, UI_RGBA(0, 0, 0, 64));
    /* each letter block */
    int n = 6;
    for (int i = 0; i < n; ++i) {
        float bx = cap_x + i * (cap_w / n);
        float by = cap_y + ((i + now / 120) % 2 ? -3.0f : 3.0f);
        /* color cycling */
        uint8_t r = (uint8_t)(180 + 40 * sinf(now / 200.0f + i));
        uint8_t g = (uint8_t)(80 + 80 * cosf(now / 300.0f + i));
        uint8_t b = (uint8_t)(200 - 50 * sinf(now / 250.0f + i));
        app->renderer->draw_rect(bx, by, cap_w / n - 6, cap_h, UI_RGB(r, g, b));
    }

    app->renderer->end_frame();

    LOGD("Frame ticks: %u dt_ms: %.1f vx: %.1f vy: %.1f wobble: %.2f", now, dt_s * 1000.0f, vx, vy,
         wobble);
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
