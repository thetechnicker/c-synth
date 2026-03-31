/*
 * ui_widgets.c — immediate-mode widget system for c-synth
 *
 * All drawing goes through the ui_renderer_t interface.
 * No external dependencies beyond ui.h, ui_widgets.h, and the C runtime.
 */

#include "ui_widgets.h"
#include "log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* =========================================================
 * Theme definitions
 * ========================================================= */

const ui_theme_t UI_THEME_DARK = {
    .bg = UI_RGB(20, 20, 30),
    .surface = UI_RGB(45, 45, 60),
    .surface_ho = UI_RGB(60, 60, 80),
    .surface_ac = UI_RGB(80, 140, 200),
    .accent = UI_RGB(80, 140, 200),
    .accent_dim = UI_RGB(50, 90, 140),
    .text = UI_RGB(220, 220, 220),
    .text_dim = UI_RGB(140, 140, 150),
    .on = UI_RGB(60, 200, 80),
    .off = UI_RGB(80, 40, 40),
    .border = UI_RGB(70, 70, 90),
    .dpi_scale = 1.0f,
};

const ui_theme_t UI_THEME_LIGHT = {
    .bg = UI_RGB(240, 240, 245),
    .surface = UI_RGB(210, 210, 220),
    .surface_ho = UI_RGB(190, 195, 215),
    .surface_ac = UI_RGB(70, 130, 200),
    .accent = UI_RGB(70, 130, 200),
    .accent_dim = UI_RGB(120, 160, 210),
    .text = UI_RGB(30, 30, 40),
    .text_dim = UI_RGB(100, 100, 115),
    .on = UI_RGB(30, 160, 60),
    .off = UI_RGB(180, 80, 80),
    .border = UI_RGB(170, 170, 185),
    .dpi_scale = 1.0f,
};

const ui_theme_t UI_THEME_SYNTHWAVE = {
    .bg = UI_RGB(10, 5, 30),
    .surface = UI_RGB(30, 15, 55),
    .surface_ho = UI_RGB(50, 25, 80),
    .surface_ac = UI_RGB(200, 50, 200),
    .accent = UI_RGB(200, 50, 200),
    .accent_dim = UI_RGB(120, 20, 140),
    .text = UI_RGB(255, 220, 255),
    .text_dim = UI_RGB(160, 100, 180),
    .on = UI_RGB(0, 255, 180),
    .off = UI_RGB(100, 10, 60),
    .border = UI_RGB(100, 30, 120),
    .dpi_scale = 1.0f,
};

static const ui_theme_t *s_theme = &UI_THEME_DARK;

void ui_set_theme(const ui_theme_t *theme) { s_theme = theme ? theme : &UI_THEME_DARK; }

const ui_theme_t *ui_get_theme(void) { return s_theme; }

/* =========================================================
 * Internal helpers
 * ========================================================= */

static float wheel_sensitivity = 0.02f;

static inline bool pt_in_rect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static inline float tw(const ui_ctx_t *ctx, const char *s) {
    return (float)(strlen(s) * (size_t)ctx->font.glyph_w);
}
static inline float th(const ui_ctx_t *ctx) { return (float)ctx->font.glyph_h; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline float mapf(float v, float alo, float ahi, float blo, float bhi) {
    return blo + (v - alo) / (ahi - alo) * (bhi - blo);
}

static void draw_border(const ui_ctx_t *ctx, float x, float y, float w, float h, ui_color_t col) {
    const ui_renderer_t *r = ctx->renderer;
    r->draw_rect(x, y, w, 1.f, col);
    r->draw_rect(x, y + h - 1, w, 1.f, col);
    r->draw_rect(x, y, 1.f, h, col);
    r->draw_rect(x + w - 1, y, 1.f, h, col);
}

/* Compute the effective ID for the current scope stack. */
static ui_id_t scoped_id(const ui_ctx_t *ctx, const char *label) {
    ui_id_t h = ui_id(label);
    for (int i = 0; i < ctx->id_stack_top; ++i)
        h = (h ^ ctx->id_stack[i]) * 0x9e3779b97f4a7c15ULL;
    return h ? h : 1;
}

/* =========================================================
 * Lifecycle
 * ========================================================= */

void ui_ctx_init(ui_ctx_t *ctx, const ui_renderer_t *renderer) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->renderer = renderer;
    ctx->font = renderer->get_buildin_font();
}

void ui_ctx_feed_event(ui_ctx_t *ctx, const SDL_Event *event) {
    switch (event->type) {

    case SDL_EVENT_MOUSE_MOTION:
        ctx->mouse_dy += event->motion.yrel;
        ctx->mouse_dx += event->motion.xrel;
        ctx->mouse_x = event->motion.x;
        ctx->mouse_y = event->motion.y;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            ctx->mouse_down = true;
            ctx->mouse_pressed = true;
            ctx->mouse_x = event->button.x;
            ctx->mouse_y = event->button.y;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            ctx->mouse_down = false;
            ctx->mouse_released = true;
            ctx->mouse_x = event->button.x;
            ctx->mouse_y = event->button.y;
        }
        break;

    case SDL_EVENT_FINGER_DOWN: {
        SDL_Window *win = SDL_GetWindowFromID(event->tfinger.windowID);
        int w = 0, h = 0;
        if (win)
            SDL_GetWindowSize(win, &w, &h);
        ctx->mouse_x = (w > 0) ? event->tfinger.x * w : event->tfinger.x;
        ctx->mouse_y = (h > 0) ? event->tfinger.y * h : event->tfinger.y;
        ctx->mouse_down = true;
        ctx->mouse_pressed = true;
        break;
    }

    case SDL_EVENT_FINGER_MOTION: {
        SDL_Window *win = SDL_GetWindowFromID(event->tfinger.windowID);
        int w = 0, h = 0;
        if (win)
            SDL_GetWindowSize(win, &w, &h);
        ctx->mouse_dx += (w > 0) ? event->tfinger.dx * w : event->tfinger.dx;
        ctx->mouse_dy += (h > 0) ? event->tfinger.dy * h : event->tfinger.dy;
        ctx->mouse_x = (w > 0) ? event->tfinger.x * w : event->tfinger.x;
        ctx->mouse_y = (h > 0) ? event->tfinger.y * h : event->tfinger.y;
        break;
    }

    case SDL_EVENT_FINGER_UP: {
        SDL_Window *win = SDL_GetWindowFromID(event->tfinger.windowID);
        int w = 0, h = 0;
        if (win)
            SDL_GetWindowSize(win, &w, &h);
        ctx->mouse_x = (w > 0) ? event->tfinger.x * w : event->tfinger.x;
        ctx->mouse_y = (h > 0) ? event->tfinger.y * h : event->tfinger.y;
        ctx->mouse_down = false;
        ctx->mouse_released = true;
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL:
        ctx->mouse_wx += event->wheel.x;
        ctx->mouse_wy += event->wheel.y;
        ctx->disc_mouse_wx += event->wheel.integer_x;
        ctx->disc_mouse_wy += event->wheel.integer_y;
        break;

    /* --- Keyboard text input --- */
    case SDL_EVENT_TEXT_INPUT:
        /* SDL delivers printable characters here as UTF-8. */
        SDL_strlcpy(ctx->key_text, event->text.text, sizeof(ctx->key_text));
        break;

    case SDL_EVENT_KEY_DOWN:
        ctx->key_mod = event->key.mod;
        switch (event->key.scancode) {
        case SDL_SCANCODE_BACKSPACE:
            ctx->key_backspace = true;
            break;
        case SDL_SCANCODE_DELETE:
            ctx->key_delete = true;
            break;
        case SDL_SCANCODE_LEFT:
            ctx->key_left = true;
            break;
        case SDL_SCANCODE_RIGHT:
            ctx->key_right = true;
            break;
        case SDL_SCANCODE_UP:
            ctx->key_up = true;
            break;
        case SDL_SCANCODE_DOWN:
            ctx->key_down = true;
            break;
        case SDL_SCANCODE_HOME:
            ctx->key_home = true;
            break;
        case SDL_SCANCODE_END:
            ctx->key_end = true;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            ctx->key_enter = true;
            break;
        case SDL_SCANCODE_ESCAPE:
            ctx->key_escape = true;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}

void ui_ctx_begin_frame(ui_ctx_t *ctx) {
    ctx->hot = 0;
    ctx->mouse_pressed = false;
    ctx->mouse_released = false;
    ctx->mouse_dy = 0.f;
    ctx->mouse_dx = 0.f;
    ctx->mouse_wx = 0.f;
    ctx->mouse_wy = 0.f;
    /* clear per-frame keyboard state */
    ctx->key_text[0] = '\0';
    ctx->key_backspace = false;
    ctx->key_delete = false;
    ctx->key_left = false;
    ctx->key_right = false;
    ctx->key_up = false;
    ctx->key_down = false;
    ctx->key_home = false;
    ctx->key_end = false;
    ctx->key_enter = false;
    ctx->key_escape = false;
}

void ui_ctx_end_frame(ui_ctx_t *ctx) {
    if (!ctx->mouse_down)
        ctx->active = 0;
    /* If something was clicked that is NOT a text widget, drop focus. */
    // if (ctx->mouse_pressed && ctx->active != ctx->focused)
    //     ctx->focused = 0;
}

/* =========================================================
 * Scope / ID stack
 * ========================================================= */

void ui_layout_push_id(ui_ctx_t *ctx, ui_id_t seed) {
    if (ctx->id_stack_top < UI_LAYOUT_ID_STACK_DEPTH)
        ctx->id_stack[ctx->id_stack_top++] = seed;
}

void ui_layout_pop_id(ui_ctx_t *ctx) {
    if (ctx->id_stack_top > 0)
        ctx->id_stack_top--;
}

/* =========================================================
 * Layout
 * ========================================================= */

void ui_layout_begin_row(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                         float padding) {
    memset(&ctx->layout, 0, sizeof(ctx->layout));
    ctx->layout.x = x;
    ctx->layout.y = y;
    ctx->layout.item_w = item_w;
    ctx->layout.item_h = item_h;
    ctx->layout.padding = padding;
    ctx->layout.cursor_x = x;
    ctx->layout.cursor_y = y;
    ctx->layout.dir = UI_LAYOUT_HORIZONTAL;
    ctx->layout.active = true;
}

void ui_layout_begin_column(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                            float padding) {
    memset(&ctx->layout, 0, sizeof(ctx->layout));
    ctx->layout.x = x;
    ctx->layout.y = y;
    ctx->layout.item_w = item_w;
    ctx->layout.item_h = item_h;
    ctx->layout.padding = padding;
    ctx->layout.cursor_x = x;
    ctx->layout.cursor_y = y;
    ctx->layout.dir = UI_LAYOUT_VERTICAL;
    ctx->layout.active = true;
}

void ui_layout_row(ui_ctx_t *ctx, float x, float y, int col_count, const float *widths,
                   float item_h) {
    memset(&ctx->layout, 0, sizeof(ctx->layout));
    ctx->layout.x = x;
    ctx->layout.y = y;
    ctx->layout.item_h = item_h;
    ctx->layout.cursor_x = x;
    ctx->layout.cursor_y = y;
    ctx->layout.dir = UI_LAYOUT_HORIZONTAL;
    ctx->layout.active = true;
    int n = col_count < UI_LAYOUT_COL_MAX ? col_count : UI_LAYOUT_COL_MAX;
    ctx->layout.col_count = n;
    for (int i = 0; i < n; ++i)
        ctx->layout.col_widths[i] = widths[i];
}

void ui_layout_next(ui_ctx_t *ctx) {
    if (!ctx->layout.active)
        return;

    if (ctx->layout.col_count > 0) {
        /* multi-column mode */
        float w = ctx->layout.col_widths[ctx->layout.col_cursor];
        ctx->layout.cursor_x += w + ctx->layout.padding;
        ctx->layout.col_cursor = (ctx->layout.col_cursor + 1) % ctx->layout.col_count;
    } else if (ctx->layout.dir == UI_LAYOUT_HORIZONTAL) {
        ctx->layout.cursor_x += ctx->layout.item_w + ctx->layout.padding;
    } else {
        ctx->layout.cursor_y += ctx->layout.item_h + ctx->layout.padding;
    }
}

void ui_layout_end_row(ui_ctx_t *ctx) { ctx->layout.active = false; }
void ui_layout_end_column(ui_ctx_t *ctx) { ctx->layout.active = false; }

bool ui_layout_cell(const ui_ctx_t *ctx, ui_rect_t *r) {
    if (!ctx->layout.active)
        return false;

    r->x = ctx->layout.cursor_x;
    r->y = ctx->layout.cursor_y;
    r->h = ctx->layout.item_h;

    if (ctx->layout.col_count > 0) {
        r->w = ctx->layout.col_widths[ctx->layout.col_cursor];
    } else {
        r->w = ctx->layout.item_w;
    }

    return true;
}

/* =========================================================
 * ui_label
 * ========================================================= */

void ui_label(ui_ctx_t *ctx, float x, float y, const char *text, ui_color_t color) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y + (cell.h - th(ctx)) * 0.5f;
        ui_layout_next(ctx);
    }
    ctx->renderer->draw_text(x, y, text, color, ctx->font);
}

/* =========================================================
 * ui_button
 * ========================================================= */

bool ui_button(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }

    ui_id_t id = scoped_id(ctx, label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, w, h);
    bool clicked = false;

    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed)
        ctx->active = id;
    if (ctx->active == id && ctx->mouse_released) {
        if (hovered)
            clicked = true;
        ctx->active = 0;
    }

    ui_color_t bg = (ctx->active == id) ? UI_COL_SURFACE_AC
                    : hovered           ? UI_COL_SURFACE_HO
                                        : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, w, h, bg);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    float tw_ = tw(ctx, label), th_ = th(ctx);
    ctx->renderer->draw_text(x + (w - tw_) * 0.5f, y + (h - th_) * 0.5f, label, UI_COL_TEXT,
                             ctx->font);
    return clicked;
}

/* =========================================================
 * ui_toggle
 * ========================================================= */

bool ui_toggle(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, bool *value) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }

    ui_id_t id = scoped_id(ctx, label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, w, h);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed)
        ctx->active = id;
    if (ctx->active == id && ctx->mouse_released) {
        if (hovered) {
            *value = !*value;
            changed = true;
        }
        ctx->active = 0;
    }

    ui_color_t base = *value ? UI_COL_ON : UI_COL_OFF;
    ui_color_t bg = hovered ? UI_COL_SURFACE_HO : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, w, h, bg);

    float led = h * 0.45f;
    float lx = x + h * 0.2f;
    float ly = y + (h - led) * 0.5f;
    ctx->renderer->draw_rect(lx, ly, led, led, base);

    ctx->renderer->draw_text(lx + led + h * 0.25f, y + (h - th(ctx)) * 0.5f, label, UI_COL_TEXT,
                             ctx->font);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);
    return changed;
}

/* =========================================================
 * ui_slider_f
 * ========================================================= */

bool ui_slider_f(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, float *value,
                 float min, float max, const char *fmt) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }
    if (!fmt)
        fmt = "%.3g";

    ui_id_t id = scoped_id(ctx, label);
    float val_lbl_w = 8.0f * (float)ctx->font.glyph_w + 4.f;
    float bar_w = (w - val_lbl_w < 10.f) ? 10.f : w - val_lbl_w;
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, bar_w, h);
    bool focused = (ctx->focused == id);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed) {
        ctx->focused = id;
        focused = true;
    }

    if (focused) {
        ctx->active = id;
    }

    if (focused && (ctx->key_enter || ctx->key_escape || !hovered)) {
        ctx->focused = 0;
        focused = false;
    }
    if (focused) {
        int x;
        if (ctx->key_up || ctx->key_right)
            x = 1;
        else if (ctx->key_down || ctx->key_left)
            x = -1;
        else
            x = 0;
        float factor;
        if (ctx->key_up || ctx->key_down)
            factor = 0.1f;
        else if (ctx->key_left || ctx->key_right)
            factor = 0.01f;
        else
            factor = 1.0f;

        float d = x * factor;
        float newv = clampf(*value + d, min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->hot == id && ctx->mouse_wy != 0.f) {
        float newv = clampf(*value + ctx->mouse_wy * wheel_sensitivity * (max - min), min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->active == id && ctx->mouse_down) {
        float newv = min + clampf((ctx->mouse_x - x) / bar_w, 0.f, 1.f) * (max - min);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->mouse_released && ctx->active == id)
        ctx->active = 0;

    ctx->renderer->draw_rect(x, y, bar_w, h, UI_COL_SURFACE);
    float fill = clampf(mapf(*value, min, max, 0.f, bar_w), 0.f, bar_w);
    if (fill > 0.f)
        ctx->renderer->draw_rect(x, y, fill, h,
                                 (ctx->active == id) ? UI_COL_ACCENT : UI_COL_ACCENT_DIM);
    ctx->renderer->draw_rect(x + fill - 1.f, y, 2.f, h, UI_COL_TEXT);
    draw_border(ctx, x, y, bar_w, h, hovered ? UI_COL_SURFACE_HO : UI_COL_BORDER);

    float th_ = th(ctx);
    if (h >= th_ * 2.f + 2.f)
        ctx->renderer->draw_text(x + 2.f, y + 2.f, label, UI_COL_TEXT_DIM, ctx->font);

    char buf[32];
    snprintf(buf, sizeof(buf), fmt, (double)*value);
    ctx->renderer->draw_text(x + bar_w + 4.f, y + (h - th_) * 0.5f, buf, UI_COL_TEXT, ctx->font);
    return changed;
}

/* =========================================================
 * ui_slider_i
 * ========================================================= */

bool ui_slider_i(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, int *value,
                 int min, int max, const char *fmt) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }
    if (!fmt)
        fmt = "%d";

    ui_id_t id = scoped_id(ctx, label);
    float val_lbl_w = 8.0f * (float)ctx->font.glyph_w + 4.f;
    float bar_w = (w - val_lbl_w < 10.f) ? 10.f : w - val_lbl_w;
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, bar_w, h);
    bool focused = (ctx->focused == id);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed) {
        ctx->focused = id;
        focused = true;
    }

    if (focused) {
        ctx->active = id;
    }

    if (focused && (ctx->key_enter || ctx->key_escape || !hovered)) {
        ctx->focused = 0;
        focused = false;
    }
    if (focused) {
        int x;
        if (ctx->key_up || ctx->key_right)
            x = 1;
        else if (ctx->key_down || ctx->key_left)
            x = -1;
        else
            x = 0;
        int factor;
        if (ctx->key_up || ctx->key_down)
            factor = 10;
        else
            factor = 1;

        int d = x * factor;
        int newv = (int)clampf((float)(*value + d), (float)min, (float)max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }

    if (ctx->hot == id && ctx->mouse_wy != 0.f) {
        int newv =
            (int)clampf((float)*value + ctx->disc_mouse_wy * wheel_sensitivity * (float)(max - min),
                        (float)min, (float)max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->active == id && ctx->mouse_down) {
        int newv = (int)(min + clampf((ctx->mouse_x - x) / bar_w, 0.f, 1.f) * (max - min) + 0.5f);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->mouse_released && ctx->active == id)
        ctx->active = 0;

    ctx->renderer->draw_rect(x, y, bar_w, h, UI_COL_SURFACE);
    float fill = clampf(mapf((float)*value, (float)min, (float)max, 0.f, bar_w), 0.f, bar_w);
    if (fill > 0.f)
        ctx->renderer->draw_rect(x, y, fill, h,
                                 (ctx->active == id) ? UI_COL_ACCENT : UI_COL_ACCENT_DIM);
    ctx->renderer->draw_rect(x + fill - 1.f, y, 2.f, h, UI_COL_TEXT);
    draw_border(ctx, x, y, bar_w, h, hovered ? UI_COL_SURFACE_HO : UI_COL_BORDER);

    float th_ = th(ctx);
    if (h >= th_ * 2.f + 2.f)
        ctx->renderer->draw_text(x + 2.f, y + 2.f, label, UI_COL_TEXT_DIM, ctx->font);

    char buf[32];
    snprintf(buf, sizeof(buf), fmt, *value);
    ctx->renderer->draw_text(x + bar_w + 4.f, y + (h - th_) * 0.5f, buf, UI_COL_TEXT, ctx->font);
    return changed;
}

/* =========================================================
 * ui_knob
 * ========================================================= */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define KNOB_ARC_SEGS 32

static void draw_knob_arc(const ui_ctx_t *ctx, float cx, float cy, float r, float start_rad,
                          float end_rad, ui_color_t col, float thick) {
    float px = cx + cosf(start_rad) * r;
    float py = cy + sinf(start_rad) * r;
    for (int i = 1; i <= KNOB_ARC_SEGS; ++i) {
        float a = start_rad + (end_rad - start_rad) * ((float)i / KNOB_ARC_SEGS);
        float nx = cx + cosf(a) * r;
        float ny = cy + sinf(a) * r;
        ctx->renderer->draw_line(px, py, nx, ny, col, thick);
        px = nx;
        py = ny;
    }
}

bool ui_knob(ui_ctx_t *ctx, float x, float y, float diameter, const char *label, float *value,
             float min, float max, float sensitivity, const char *fmt) {
    if (!fmt)
        fmt = "%.3g";

    float r = diameter * 0.5f, cx = x + r, cy = y + r;
    ui_id_t id = scoped_id(ctx, label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, diameter, diameter);
    bool focused = (ctx->focused == id);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed) {
        ctx->focused = id;
        focused = true;
    }

    if (focused) {
        ctx->active = id;
    }

    if (focused && (ctx->key_enter || ctx->key_escape || !hovered)) {
        ctx->focused = 0;
        focused = false;
    }
    if (focused) {
        int x;
        if (ctx->key_up || ctx->key_right)
            x = 1;
        else if (ctx->key_down || ctx->key_left)
            x = -1;
        else
            x = 0;
        float factor;
        if (ctx->key_up || ctx->key_down)
            factor = 0.1f;
        else if (ctx->key_left || ctx->key_right)
            factor = 0.01f;
        else
            factor = 1.0f;

        float d = x * factor;
        float newv = clampf(*value + d, min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }

    if (ctx->hot == id && ctx->mouse_wy != 0.f) {
        float newv = clampf(*value + ctx->mouse_wy * wheel_sensitivity * (max - min), min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->active == id && ctx->mouse_down && ctx->mouse_dy != 0.f) {
        float newv = clampf(*value - ctx->mouse_dy * sensitivity, min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }
    if (ctx->mouse_released && ctx->active == id)
        ctx->active = 0;

    const float start_ang = (float)(5.0 * M_PI / 6.0);
    const float sweep = (float)(4.0 * M_PI / 3.0);
    float t = (max > min) ? clampf((*value - min) / (max - min), 0.f, 1.f) : 0.f;
    float val_ang = start_ang + sweep * t;

    ui_color_t body = (ctx->active == id) ? UI_COL_SURFACE_AC
                      : hovered           ? UI_COL_SURFACE_HO
                                          : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, diameter, diameter, body);

    float track_r = r * 0.78f;
    draw_knob_arc(ctx, cx, cy, track_r, start_ang, start_ang + sweep, UI_COL_BORDER, 2.f);
    if (t > 0.f)
        draw_knob_arc(ctx, cx, cy, track_r, start_ang, val_ang, UI_COL_ACCENT, 2.5f);

    ctx->renderer->draw_line(cx, cy, cx + cosf(val_ang) * (r * 0.60f),
                             cy + sinf(val_ang) * (r * 0.60f), UI_COL_TEXT, 2.f);

    float th_ = th(ctx);
    float lw = tw(ctx, label);
    float lby = y + diameter + 3.f;
    ctx->renderer->draw_text(cx - lw * 0.5f, lby, label, UI_COL_TEXT_DIM, ctx->font);

    char buf[32];
    snprintf(buf, sizeof(buf), fmt, (double)*value);
    float vw = tw(ctx, buf);
    ctx->renderer->draw_text(cx - vw * 0.5f, lby + th_ + 2.f, buf, UI_COL_TEXT, ctx->font);
    return changed;
}

/* =========================================================
 * ui_separator
 * ========================================================= */

void ui_separator(ui_ctx_t *ctx, float x, float y, float len, float thickness, bool vertical) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        thickness = vertical ? thickness : (thickness > 0.f ? thickness : 2.f);
        len = vertical ? cell.h : cell.w;
        ui_layout_next(ctx);
    }
    if (thickness <= 0.f)
        thickness = 2.f;

    if (vertical)
        ctx->renderer->draw_rect(x + (thickness < 2.f ? 0.f : (thickness - 2.f) * 0.5f), y,
                                 thickness, len, UI_COL_BORDER);
    else
        ctx->renderer->draw_rect(x, y + (thickness < 2.f ? 0.f : (thickness - 2.f) * 0.5f), len,
                                 thickness, UI_COL_BORDER);
}

/* =========================================================
 * ui_scope / ui_scope_end
 * ========================================================= */

void ui_scope(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label) {
    /* Push scoped ID so widget IDs inside are namespaced. */
    ui_layout_push_id(ctx, ui_id(label));

    /* Draw the panel border. */
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    /* Draw the label over the top edge, with a small background to
     * interrupt the border line so it looks like a classic group box. */
    float lw = tw(ctx, label);
    float th_ = th(ctx);
    float lx = x + 8.f;
    float ly = y - th_ * 0.5f;
    float pad = 3.f;
    /* background strip to break the border */
    ctx->renderer->draw_rect(lx - pad, ly, lw + pad * 2.f, th_, UI_COL_BG);
    ctx->renderer->draw_text(lx, ly, label, UI_COL_TEXT_DIM, ctx->font);
}

void ui_scope_end(ui_ctx_t *ctx) { ui_layout_pop_id(ctx); }

/* =========================================================
 * ui_dropdown
 * ========================================================= */

/*
 * We need persistent open/close state across frames.  Since this is an
 * immediate-mode system with no retained widget storage we use the
 * ctx->active field: when a dropdown header is pressed, active is set to
 * its ID and stays there until the user picks an item or clicks elsewhere.
 * The open list is drawn in the same frame using whatever Z order the
 * renderer provides (last drawn = on top).
 */
bool ui_dropdown(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label,
                 const char *const *items, int count, int *selected) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }

    ui_id_t id = scoped_id(ctx, label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, w, h);
    bool is_open = (ctx->active == id);
    bool changed = false;

    /* Toggle open on header click. */
    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed) {
        ctx->active = is_open ? 0 : id;
        is_open = !is_open;
    }

    /* Draw header */
    ui_color_t bg = is_open ? UI_COL_SURFACE_AC : hovered ? UI_COL_SURFACE_HO : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, w, h, bg);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    /* Selected item label */
    const char *sel_text = (*selected >= 0 && *selected < count) ? items[*selected] : label;
    float th_ = th(ctx);
    ctx->renderer->draw_text(x + 4.f, y + (h - th_) * 0.5f, sel_text, UI_COL_TEXT, ctx->font);

    /* Arrow indicator (▾) — drawn as two lines forming a chevron */
    float ax = x + w - h * 0.6f;
    float ay = y + h * 0.35f;
    float aw = h * 0.35f;
    float ah = h * 0.3f;
    ctx->renderer->draw_line(ax, ay, ax + aw * 0.5f, ay + ah, UI_COL_TEXT_DIM, 1.5f);
    ctx->renderer->draw_line(ax + aw * 0.5f, ay + ah, ax + aw, ay, UI_COL_TEXT_DIM, 1.5f);

    /* Draw open list */
    if (is_open) {
        float ly = y + h;
        float list_h = (float)count * h;
        /* Background panel for the list */
        ctx->renderer->draw_rect(x, ly, w, list_h, UI_COL_SURFACE);
        draw_border(ctx, x, ly, w, list_h, UI_COL_BORDER);

        for (int i = 0; i < count; ++i) {
            float iy = ly + (float)i * h;
            bool ih = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, iy, w, h);

            if (ih) {
                ctx->renderer->draw_rect(x, iy, w, h, UI_COL_SURFACE_HO);
                if (ctx->mouse_pressed) {
                    *selected = i;
                    ctx->active = 0; /* close */
                    changed = true;
                }
            } else if (i == *selected) {
                ctx->renderer->draw_rect(x, iy, w, h, UI_COL_ACCENT_DIM);
            }

            ctx->renderer->draw_text(x + 4.f, iy + (h - th_) * 0.5f, items[i], UI_COL_TEXT,
                                     ctx->font);
        }

        /* Close if mouse pressed outside the whole dropdown */
        if (ctx->mouse_pressed && !pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, w, h + list_h)) {
            ctx->active = 0;
        }
    }

    return changed;
}

/* =========================================================
 * ui_text_input
 * ========================================================= */

bool ui_text_input(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, char *buf,
                   int buf_len) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        x = cell.x;
        y = cell.y;
        w = cell.w;
        h = cell.h;
        ui_layout_next(ctx);
    }

    ui_id_t id = scoped_id(ctx, label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, w, h);
    bool focused = (ctx->focused == id);
    bool changed = false;

    /* Gain focus on click. */
    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed) {
        SDL_StartTextInput(ctx->renderer->get_window());
        ctx->focused = id;
        focused = true;
    }

    /* Lose focus on Enter or Escape. */
    if (focused && (ctx->key_enter || ctx->key_escape || (!hovered && ctx->mouse_down))) {
        SDL_StopTextInput(ctx->renderer->get_window());
        ctx->focused = 0;
        focused = false;
    }

    /* --- Text editing --- */
    if (focused) {
        int len = (int)strlen(buf);

        /* Append printable text from SDL_EVENT_TEXT_INPUT */
        if (ctx->key_text[0] != '\0') {
            int tlen = (int)strlen(ctx->key_text);
            if (len + tlen < buf_len - 1) {
                SDL_strlcat(buf, ctx->key_text, (size_t)buf_len);
                changed = true;
            }
        }

        /* Backspace: remove last character (simple, no cursor tracking) */
        if (ctx->key_backspace && len > 0) {
            buf[len - 1] = '\0';
            changed = true;
        }

        /* Delete: same as backspace for now (full cursor tracking is a
         * significant addition; add when needed). */
        if (ctx->key_delete && len > 0) {
            buf[len - 1] = '\0';
            changed = true;
        }
    }

    /* --- Drawing --- */
    ui_color_t bg = focused ? UI_COL_SURFACE_AC : hovered ? UI_COL_SURFACE_HO : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, w, h, bg);
    draw_border(ctx, x, y, w, h,
                focused ? UI_COL_ACCENT : (hovered ? UI_COL_SURFACE_HO : UI_COL_BORDER));

    float th_ = th(ctx);
    float text_x = x + 4.f;
    float text_y = y + (h - th_) * 0.5f;

    /* Draw label above if there is room */
    if (h >= th_ * 2.f + 2.f)
        ctx->renderer->draw_text(x + 2.f, y + 2.f, label, UI_COL_TEXT_DIM, ctx->font);

    /* Content — clip display text to fit inside the field.
     * Show end of the string when it overflows (common for input fields). */
    int len = (int)strlen(buf);
    int glyph = ctx->font.glyph_w;
    int max_ch = (int)((w - 8.f) / (float)glyph);
    const char *display = buf;
    if (len > max_ch && max_ch > 0)
        display = buf + (len - max_ch);

    ctx->renderer->draw_text(text_x, text_y, display, UI_COL_TEXT, ctx->font);

    /* Blinking cursor (simple: always visible for now) */
    if (focused) {
        int disp_len = (int)strlen(display);
        float cur_x = text_x + (float)(disp_len * glyph);
        /* only draw if inside the field */
        if (cur_x < x + w - 2.f)
            ctx->renderer->draw_rect(cur_x, text_y, 1.f, th_, UI_COL_TEXT);
    }

    return changed;
}

/* =========================================================
 * ui_waveform_display
 * ========================================================= */

void ui_waveform_display(ui_ctx_t *ctx, float x, float y, float w, float h, const float *samples,
                         int count, ui_color_t color) {
    if (!samples || count <= 0)
        return;

    /* Background */
    ctx->renderer->draw_rect(x, y, w, h, UI_COL_SURFACE);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    /* Centre line */
    float mid_y = y + h * 0.5f;
    ctx->renderer->draw_line(x, mid_y, x + w, mid_y, UI_COL_BORDER, 1.f);

    /* Waveform — map samples to pixel positions and draw line segments */
    float x_scale = w / (float)(count - 1 > 0 ? count - 1 : 1);
    float y_scale = h * 0.5f;

    float px = x;
    float py = mid_y - clampf(samples[0], -1.f, 1.f) * y_scale;

    for (int i = 1; i < count; ++i) {
        float nx = x + (float)i * x_scale;
        float ny = mid_y - clampf(samples[i], -1.f, 1.f) * y_scale;
        ctx->renderer->draw_line(px, py, nx, ny, color, 1.5f);
        px = nx;
        py = ny;
    }
}

/* =========================================================
 * ui_spectrum_display
 * ========================================================= */

void ui_spectrum_display(ui_ctx_t *ctx, float x, float y, float w, float h, const float *bins,
                         int count, ui_color_t color) {
    if (!bins || count <= 0)
        return;

    /* Background */
    ctx->renderer->draw_rect(x, y, w, h, UI_COL_SURFACE);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    /* Draw bars — each bin occupies an equal slice of the width. */
    float bar_w = w / (float)count;
    float bar_gap = bar_w > 3.f ? 1.f : 0.f;
    float inner_bar = bar_w - bar_gap;
    if (inner_bar < 1.f)
        inner_bar = 1.f;

    for (int i = 0; i < count; ++i) {
        float mag = clampf(bins[i], 0.f, 1.f);
        float bar_h = mag * h;
        float bx = x + (float)i * bar_w;
        float by = y + h - bar_h;
        ctx->renderer->draw_rect(bx, by, inner_bar, bar_h, color);
    }
}
