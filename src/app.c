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
#include <string.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_timer.h>

#define MAX_TITLE_LEN 256
#define DEFAULT_WINDOW_WIDTH 900
#define DEFAULT_WINDOW_HEIGHT 900

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * Demo state — one value per widget shown
 * ========================================================= */

/* Oscillator section */
static bool osc_on = false;
static float osc_freq = 440.f;
static int osc_wave = 0;        /* dropdown selection   */
static float osc_pw = 0.5f;     /* pulse width knob     */
static float osc_volume = 0.8f; /* knob                 */

/* Filter section */
static bool filter_on = true;
static float flt_cutoff = 2000.f;
static float flt_res = 0.3f;
static int flt_poles = 4; /* integer slider       */

/* Misc */
static bool reverb_on = false;
static float reverb_mix = 0.25f;
static char patch_name[64] = "Init Patch";

/* Theme selector */
static int theme_sel = 0;

/* Waveform / spectrum buffers */
#define WAVE_SAMPLES 256
#define SPEC_BINS 64
static float s_wave[WAVE_SAMPLES];
static float s_spec[SPEC_BINS];

/* Waveform names for the dropdown */
static const char *const k_wave_names[] = {"Sine", "Square", "Sawtooth", "Triangle", "Noise"};
#define WAVE_COUNT ((int)(sizeof(k_wave_names) / sizeof(k_wave_names[0])))

/* Theme names */
static const char *const k_theme_names[] = {"Dark", "Light", "Synthwave"};
#define THEME_COUNT 3

/* =========================================================
 * Procedural waveform / spectrum generation
 * ========================================================= */

static void update_waveform(float t) {
    for (int i = 0; i < WAVE_SAMPLES; ++i) {
        float phase = (float)i / WAVE_SAMPLES;
        float x = 2.f * (float)M_PI * phase;
        float s = 0.f;
        switch (osc_wave) {
        case 0: /* Sine */
            s = sinf(x + t);
            break;
        case 1: /* Square */
            s = (sinf(x + t) >= 0.f) ? 1.f : -1.f;
            /* naive pulse width */
            s = (phase < osc_pw) ? 1.f : -1.f;
            (void)s; /* re-assign cleanly */
            s = (phase < osc_pw) ? 1.f : -1.f;
            break;
        case 2: /* Sawtooth */
            s = 2.f * (phase)-1.f;
            break;
        case 3: /* Triangle */
            s = (phase < 0.5f) ? (4.f * phase - 1.f) : (3.f - 4.f * phase);
            break;
        case 4: /* Noise — pseudo-random but stable per index */
            s = sinf((float)i * 1.7f + t * 13.f) * cosf((float)i * 3.1f + t * 7.f);
            break;
        default:
            s = 0.f;
        }
        s_wave[i] = s * osc_volume * (osc_on ? 1.f : 0.1f);
    }
}

static void update_spectrum(float t) {
    for (int i = 0; i < SPEC_BINS; ++i) {
        float bin = (float)i / SPEC_BINS;
        /* Simple animated fake spectrum: a peak near the fundamental plus harmonics */
        float fund = expf(-50.f * (bin - 0.05f) * (bin - 0.05f));
        float harm = 0.f;
        for (int h = 2; h <= 8; h += 2)
            harm += expf(-200.f * (bin - 0.05f * h) * (bin - 0.05f * h)) * (1.f / h);
        float noise = 0.02f * fabsf(sinf((float)i * 2.3f + t));
        float cutoff_fade = (flt_cutoff / 20000.f);
        float rolloff = (bin < cutoff_fade)
                            ? 1.f
                            : expf(-8.f * (bin - cutoff_fade) / (1.f - cutoff_fade + 0.001f));
        s_spec[i] = (fund + harm + noise) * rolloff * (osc_on ? 1.f : 0.05f);
        if (s_spec[i] > 1.f)
            s_spec[i] = 1.f;
    }
}

/* =========================================================
 * SDL app callbacks
 * ========================================================= */

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    log_init("app.log.jsonl", LOG_DEBUG);
    LOGI("Application is starting up ...");
    LOGD("Application data at: %s", SDL_GetBasePath());
    LOGD("Current workdir is: %s", SDL_GetCurrentDirectory());

    ArgParse *ap = malloc(sizeof(ArgParse));
    CHECK_ERR_SDL(argparse_init(ap, "C-Synth", NULL, NULL, 0));
    create_app_argparse(ap);
    HashMap *map = hashmap_new(0);

    SDL_AppResult res = argparse_parse(argc, argv, ap, map);
    if (res != SDL_APP_CONTINUE)
        return res;

    App_t *app = malloc(sizeof(App_t));
    memset(app, 0, sizeof(App_t));
    if (!app) {
        LOGE("Cannot create app: %s", strerror(ENOMEM));
        return SDL_APP_FAILURE;
    }
    app->config = map;

    char title[MAX_TITLE_LEN];
    snprintf(title, sizeof(title), "C-Synth widget demo — %s (%s)%s", PROJECT_GIT_DESCRIBE,
             PROJECT_GIT_COMMIT, PROJECT_GIT_DIRTY ? "-dirty" : "");

    ArgParseResult **arg_renderer = hashmap_get(app->config, "renderer");
    char *renderer_name = (*arg_renderer)->s;
    if (!renderer_name || strcmp(renderer_name, "default") == 0)
        app->renderer = ui_get_default_renderer();
    else
        app->renderer = ui_get_renderer(renderer_name);
    if (!app->renderer) {
        LOGW("fallback to default renderer");
        app->renderer = ui_get_default_renderer();
    }

    app->renderer->init(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, title);
    ui_ctx_init(&app->ui, app->renderer);

    /* Start on the dark theme */
    ui_set_theme(&UI_THEME_DARK);

    *appstate = app;
    LOGI("Application startup complete");
    return SDL_APP_CONTINUE;
}

/* -------------------------------------------------------------------------
 * Main render loop — draws every widget at least once
 * ---------------------------------------------------------------------- */
SDL_AppResult SDL_AppIterate(void *appstate) {
    App_t *app = (App_t *)appstate;

    static uint64_t last_ticks = 0;
    static bool init_done = false;
    uint64_t now = SDL_GetTicks();
    if (!init_done) {
        last_ticks = now;
        init_done = true;
    }

    float dt_s = (float)(now - last_ticks) / 1000.f;
    if (dt_s > 0.05f)
        dt_s = 0.05f;
    last_ticks = now;

    /* Running time for animation */
    static float t = 0.f;
    t += dt_s;

    /* Apply selected theme */
    const ui_theme_t *themes[THEME_COUNT] = {&UI_THEME_DARK, &UI_THEME_LIGHT, &UI_THEME_SYNTHWAVE};
    ui_set_theme(themes[theme_sel]);

    /* Update fake audio data */
    update_waveform(t);
    update_spectrum(t);

    /* ---- begin rendering ---- */
    app->renderer->begin_frame();
    ui_ctx_t *ui = &app->ui;

    /* Layout constants */
    // const float  = 24.f;
    const float MARGIN = 32.f;
    const float COL_W = 420.f; /* left column width  */
    const float ROW_H = 32.f;
    const float PAD = 8.f;
    const float FULL_WIDTH = DEFAULT_WINDOW_WIDTH - MARGIN * 2.f;
    // const float SCOPE_PAD = 14.f;
    float y = MARGIN;

    ui_area_t theme_area = ui_scope(ui, MARGIN, y, FULL_WIDTH, ROW_H + PAD * 2.f, "Theme");
    {
        float col_w3 = (DEFAULT_WINDOW_WIDTH - MARGIN * 2.f - PAD * 2.f) / 3.f;
        float tw3[3] = {col_w3, col_w3, col_w3};
        ui_layout_row(ui, theme_area.x + PAD, theme_area.y + PAD, 3, tw3, ROW_H, 0);
        for (int i = 0; i < THEME_COUNT; ++i) {
            /* Use push_id so buttons with same label hash differently */
            ui_layout_push_id(ui, (ui_id_t)(uintptr_t)i);
            if (ui_button(ui, 0, 0, 0, 0, k_theme_names[i]))
                theme_sel = i;
            ui_layout_pop_id(ui);
        }
        ui_layout_end_row(ui);
    }
    ui_scope_end(ui);

    y = theme_area.y + theme_area.h + MARGIN;
    float scope_h = ROW_H * 4.f + PAD * 5.f + PAD * 2.f;
    ui_area_t osc_area = ui_scope(ui, MARGIN, y, COL_W, scope_h, "Oscillator");
    y = osc_area.y + PAD;
    /* Row 1: toggle + float slider */
    {
        float ws[2] = {100.f, COL_W - 100.f - PAD};
        ui_layout_row(ui, osc_area.x + PAD, y, 2, ws, ROW_H, PAD);
        if (ui_toggle(ui, 0, 0, 0, 0, "On", &osc_on))
            LOGD("Oscillator toggled: %d", osc_on);
        if (ui_slider_f(ui, 0, 0, 0, 0, "Freq", &osc_freq, 20.f, 20000.f, "%.0f Hz"))
            LOGD("Freq: %.0f", osc_freq);
        ui_area_t row = ui_layout_end_row(ui);
        y = row.y + row.h + PAD;
    }

    /* Row 2: waveform dropdown + pulse width slider */
    {
        float ws[2] = {140.f, COL_W - 140.f - PAD};
        ui_layout_row(ui, osc_area.x + PAD, y, 2, ws, ROW_H, PAD);
        if (ui_dropdown(ui, 0, 0, 0, 0, "Wave", k_wave_names, WAVE_COUNT, &osc_wave))
            LOGD("Wave: %d", osc_wave);
        if (ui_slider_f(ui, 0, 0, 0, 0, "PW", &osc_pw, 0.05f, 0.95f, "%.2f"))
            LOGD("PW: %.2f", osc_pw);
        ui_area_t row = ui_layout_end_row(ui);
        y = row.y + row.h + PAD;
    }
    /* Row 3: two knobs (volume + detune) */
    {
        float ws[2] = {140.f, COL_W - 140.f - PAD};
        ui_area_t row_s = ui_layout_row(ui, osc_area.x + PAD, y, 2, ws, ROW_H, PAD);
        static float detune = 0.f;
        if (ui_knob(ui, row_s.x, row_s.y, 64.f, "Volume", &osc_volume, 0.f, 1.f, 0.005f, "%.2f"))
            LOGD("Volume: %.2f", osc_volume);
        if (ui_knob(ui, row_s.x + 80.f, row_s.y, 64.f, "Detune", &detune, -50.f, 50.f, 0.5f,
                    "%+.1f ct"))
            LOGD("Detune: %.1f", detune);
        ui_area_t row = ui_layout_end_row(ui);
        y = row.y + row.h + PAD;
    }
    ui_scope_end(ui);

    y = theme_area.y + theme_area.h + MARGIN;
    float x = osc_area.x + osc_area.w + MARGIN;
    float w = DEFAULT_WINDOW_WIDTH - (x + MARGIN);

    float filter_scope_h = ROW_H * 3.f + PAD * 4.f + 14.f;
    ui_area_t filter_area = ui_scope(ui, x, y, w, filter_scope_h, "Filter");
    {
        float ws[2] = {100.f, w - 100.f - PAD};
        ui_layout_row(ui, x + PAD, filter_area.y + PAD, 2, ws, ROW_H, PAD);
        if (ui_toggle(ui, 0, 0, 0, 0, "On", &filter_on))
            LOGD("Filter on: %d", filter_on);
        if (ui_slider_f(ui, 0, 0, 0, 0, "Cutoff", &flt_cutoff, 20.f, 20000.f, "%.0f Hz"))
            LOGD("Cutoff: %.0f", flt_cutoff);
        ui_area_t row = ui_layout_end_row(ui);
        y = row.y + row.h + PAD;
    }

    ///* Row 2: resonance float slider */
    if (ui_slider_f(ui, x + PAD, y, w, ROW_H, "Resonance", &flt_res, 0.f, 1.f, "%.2f"))
        LOGD("Res: %.2f", flt_res);

    /* Row 3: poles integer slider */
    if (ui_slider_i(ui, x + PAD, y + ROW_H + PAD, w, ROW_H, "Poles", &flt_poles, 1, 8, "%d"))
        LOGD("Poles: %d", flt_poles);

    ui_scope_end(ui);

    y = filter_area.y + filter_area.h + MARGIN;
    float fx_h = ROW_H * 2.f + PAD * 3.f;
    ui_area_t effect_area = ui_scope(ui, filter_area.x, y, filter_area.w, fx_h, "Effects");

    ui_layout_begin_column(ui, effect_area.x + PAD, effect_area.y + PAD, effect_area.w - PAD * 2.f,
                           ROW_H, PAD);
    if (ui_toggle(ui, 0, 0, 0, 0, "Reverb", &reverb_on))
        LOGD("Reverb: %d", reverb_on);
    if (ui_slider_f(ui, 0, 0, 0, 0, "Mix", &reverb_mix, 0.f, 1.f, "%.2f"))
        LOGD("Reverb mix: %.2f", reverb_mix);
    ui_layout_end_column(ui);

    ui_scope_end(ui);

    y = fmax(osc_area.y + osc_area.h + MARGIN, effect_area.y + effect_area.h + MARGIN);
    ui_separator(ui, MARGIN, y, DEFAULT_WINDOW_WIDTH - MARGIN * 2.f, 2.f, false);
    y += 10.f;

    ui_label(ui, MARGIN, y + (ROW_H - (float)ui->font.glyph_h) * 0.5f,
             "Patch name:", UI_COL_TEXT_DIM);
    if (ui_text_input(ui, MARGIN + 90.f, y, DEFAULT_WINDOW_WIDTH - MARGIN * 2.f - 90.f, ROW_H,
                      "Patch", patch_name, sizeof(patch_name)))
        LOGD("Patch name: %s", patch_name);
    y += ROW_H + PAD;

    float disp_w = (DEFAULT_WINDOW_WIDTH - MARGIN * 3.f) * 0.5f;
    float disp_h = 100.f;

    ui_scope(ui, MARGIN, y, disp_w, disp_h + 20.f, "Waveform");
    ui_waveform_display(ui, MARGIN + 4.f, y + 16.f, disp_w - 8.f, disp_h, s_wave, WAVE_SAMPLES,
                        UI_COL_ACCENT);
    ui_scope_end(ui);

    float spec_x = MARGIN + disp_w + PAD;
    float spec_w = DEFAULT_WINDOW_WIDTH - spec_x - MARGIN;
    ui_scope(ui, spec_x, y, spec_w, disp_h + 20.f, "Spectrum");
    ui_spectrum_display(ui, spec_x + 4.f, y + 16.f, spec_w - 8.f, disp_h, s_spec, SPEC_BINS,
                        UI_COL_ACCENT);
    ui_scope_end(ui);

    y += disp_h + 24.f + PAD;

    ui_separator(ui, MARGIN, y, DEFAULT_WINDOW_WIDTH - MARGIN * 2.f, 2.f, false);
    y += 10.f;

    /* Two panels side by side with a vertical separator between them */
    {
        float half = (DEFAULT_WINDOW_WIDTH - MARGIN * 2.f - 10.f) * 0.5f;

        /* Left: label grid using layout column */
        ui_layout_begin_column(ui, MARGIN, y, half, 20.f, 4.f);
        ui_label(ui, 0, 0, "layout_begin_column demo", UI_COL_TEXT_DIM);
        ui_label(ui, 0, 0, "each label placed by the layout engine", UI_COL_TEXT_DIM);
        ui_label(ui, 0, 0, "no manual x/y coordinates needed", UI_COL_TEXT_DIM);
        ui_layout_end_column(ui);

        /* Vertical separator */
        ui_separator(ui, MARGIN + half + 4.f, y, 64.f, 2.f, true);

        /* Right: second instance of the same labels but with scoped IDs */
        ui_layout_push_id(ui, ui_id("right_panel"));
        ui_layout_begin_column(ui, MARGIN + half + 10.f, y, half, 20.f, 4.f);
        ui_label(ui, 0, 0, "ui_layout_push_id demo", UI_COL_TEXT_DIM);
        ui_label(ui, 0, 0, "same label strings - unique widget IDs", UI_COL_TEXT_DIM);
        ui_label(ui, 0, 0, "push/pop prevents hash collisions", UI_COL_TEXT_DIM);
        ui_layout_end_column(ui);
        ui_layout_pop_id(ui);
    }
    /* ---- end frame ---- */
    ui_ctx_end_frame(ui);
    app->renderer->end_frame();

    /* begin_frame must come after end_frame */
    ui_ctx_begin_frame(ui);
    return SDL_APP_CONTINUE;
}

/* =========================================================
 * Event handler
 * ========================================================= */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    App_t *app = (App_t *)appstate;
    ui_ctx_feed_event(&app->ui, event);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
        app->buttonstates[event->key.scancode] = true;
        break;
    case SDL_EVENT_KEY_UP:
        app->buttonstates[event->key.scancode] = false;
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

/* =========================================================
 * Shutdown
 * ========================================================= */
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    App_t *app = (App_t *)appstate;
    LOGD("Exiting");
    if (app->config)
        hashmap_free(app->config);
    if (app->stream)
        Pm_Close(app->stream);
    Pm_Terminate();
    if (app->renderer)
        app->renderer->shutdown();
    free(app);
    log_shutdown();
}

/* =========================================================
 * Argparse setup
 * ========================================================= */
void create_app_argparse(ArgParse *ap) {
    argparse_add_value(ap, "renderer", "rendering backend used", false, true, 0, "default");
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
 * app->stream = stream;
 */
