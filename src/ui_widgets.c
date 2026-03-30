/*
 * ui_widgets.c — immediate-mode widget system for c-synth
 *
 * All drawing goes through the ui_renderer_t interface already in place.
 * No external dependencies beyond ui.h, ui_widgets.h, and the C runtime.
 */

#include "ui_widgets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* =========================================================
 * Internal helpers
 * ========================================================= */

/* True if (px, py) is inside the rect defined by (x, y, w, h). */
static inline bool pt_in_rect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/*
 * Measure text width in pixels using the builtin bitmap font.
 * Each glyph is font.glyph_w pixels wide.
 */
static inline float text_w(const ui_ctx_t *ctx, const char *s) {
    return (float)(strlen(s) * (size_t)ctx->font.glyph_w);
}

static inline float text_h(const ui_ctx_t *ctx) { return (float)ctx->font.glyph_h; }

/* Clamp a float to [lo, hi]. */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Linear map: v in [alo, ahi] → [blo, bhi]. */
static inline float mapf(float v, float alo, float ahi, float blo, float bhi) {
    return blo + (v - alo) / (ahi - alo) * (bhi - blo);
}

/*
 * Draw a one-pixel border around a rect.
 * Uses four thin rects instead of lines so both render backends agree.
 */
static void draw_border(const ui_ctx_t *ctx, float x, float y, float w, float h, ui_color_t col) {
    const ui_renderer_t *r = ctx->renderer;
    r->draw_rect(x, y, w, 1.f, col);         /* top    */
    r->draw_rect(x, y + h - 1, w, 1.f, col); /* bottom */
    r->draw_rect(x, y, 1.f, h, col);         /* left   */
    r->draw_rect(x + w - 1, y, 1.f, h, col); /* right  */
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
        ctx->mouse_dy += event->motion.yrel; /* accumulate for knobs */
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
    case SDL_EVENT_MOUSE_WHEEL:
        ctx->mouse_wx += event->wheel.mouse_x; /* accumulate for knobs */
        ctx->mouse_wy += event->wheel.mouse_y; /* accumulate for knobs */
        break;
    default:
        break;
    }
}

void ui_ctx_begin_frame(ui_ctx_t *ctx) {
    /* Reset per-frame transient state.  Hot is rebuilt each frame from
     * widget hit-tests; active persists until the mouse is released. */
    ctx->hot = 0;
    ctx->mouse_pressed = false;
    ctx->mouse_released = false;
    ctx->mouse_dy = 0.f;
}

void ui_ctx_end_frame(ui_ctx_t *ctx) {
    /* If the mouse button was released this frame and nothing consumed it,
     * clear the active widget so a stale drag cannot persist. */
    if (!ctx->mouse_down)
        ctx->active = 0;
}

/* =========================================================
 * Layout
 * ========================================================= */

void ui_layout_begin_row(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                         float padding) {
    ctx->layout.x = x;
    ctx->layout.y = y;
    ctx->layout.item_w = item_w;
    ctx->layout.item_h = item_h;
    ctx->layout.padding = padding;
    ctx->layout.cursor_x = x;
    ctx->layout.active = true;
}

void ui_layout_next(ui_ctx_t *ctx) {
    if (ctx->layout.active)
        ctx->layout.cursor_x += ctx->layout.item_w + ctx->layout.padding;
}

void ui_layout_end_row(ui_ctx_t *ctx) { ctx->layout.active = false; }

bool ui_layout_cell(const ui_ctx_t *ctx, ui_rect_t *r) {
    if (!ctx->layout.active)
        return false;
    r->x = ctx->layout.cursor_x;
    r->y = ctx->layout.y;
    r->w = ctx->layout.item_w;
    r->h = ctx->layout.item_h;
    return true;
}

/* =========================================================
 * ui_label
 * ========================================================= */

void ui_label(ui_ctx_t *ctx, float x, float y, const char *text, ui_color_t color) {
    ui_rect_t cell;
    if (ui_layout_cell(ctx, &cell)) {
        /* centre the label vertically in the cell */
        x = cell.x;
        y = cell.y + (cell.h - text_h(ctx)) * 0.5f;
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

    ui_id_t id = UI_ID(label);
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

    /* pick colour */
    ui_color_t bg = (ctx->active == id) ? UI_COL_SURFACE_AC
                    : hovered           ? UI_COL_SURFACE_HO
                                        : UI_COL_SURFACE;

    ctx->renderer->draw_rect(x, y, w, h, bg);
    draw_border(ctx, x, y, w, h, UI_COL_BORDER);

    /* centred label text */
    float tw = text_w(ctx, label);
    float th = text_h(ctx);
    ctx->renderer->draw_text(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, UI_COL_TEXT,
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

    ui_id_t id = UI_ID(label);
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

    /* LED colour: green = on, dark red = off */
    ui_color_t base = *value ? UI_COL_ON : UI_COL_OFF;
    ui_color_t bg = hovered ? UI_COL_SURFACE_HO : UI_COL_SURFACE;

    ctx->renderer->draw_rect(x, y, w, h, bg);

    /* small LED indicator on the left */
    float led_size = h * 0.45f;
    float led_x = x + h * 0.2f;
    float led_y = y + (h - led_size) * 0.5f;
    ctx->renderer->draw_rect(led_x, led_y, led_size, led_size, base);

    /* label to the right of the LED */
    float th = text_h(ctx);
    ctx->renderer->draw_text(led_x + led_size + h * 0.25f, y + (h - th) * 0.5f, label, UI_COL_TEXT,
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

    ui_id_t id = UI_ID(label);

    /* Reserve space on the right for the value label.
     * Estimate worst-case: 8 chars * glyph_w + small margin. */
    float val_label_w = 8.0f * (float)ctx->font.glyph_w + 4.f;
    float bar_w = w - val_label_w;
    if (bar_w < 10.f)
        bar_w = 10.f;

    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, bar_w, h);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed)
        ctx->active = id;

    if (ctx->active == id && ctx->mouse_down) {
        /* map mouse X position directly to value */
        float t = clampf((ctx->mouse_x - x) / bar_w, 0.f, 1.f);
        float newv = min + t * (max - min);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }

    if (ctx->mouse_released && ctx->active == id)
        ctx->active = 0;

    /* draw track */
    ctx->renderer->draw_rect(x, y, bar_w, h, UI_COL_SURFACE);

    /* draw fill */
    float fill = clampf(mapf(*value, min, max, 0.f, bar_w), 0.f, bar_w);
    if (fill > 0.f)
        ctx->renderer->draw_rect(x, y, fill, h,
                                 (ctx->active == id) ? UI_COL_ACCENT : UI_COL_ACCENT_DIM);

    /* thumb line */
    ctx->renderer->draw_rect(x + fill - 1.f, y, 2.f, h, UI_COL_TEXT);

    draw_border(ctx, x, y, bar_w, h, hovered ? UI_COL_SURFACE_HO : UI_COL_BORDER);

    /* label above (small, dim) — only if it fits */
    float th = text_h(ctx);
    if (h >= th * 2.f + 2.f) {
        ctx->renderer->draw_text(x + 2.f, y + 2.f, label, UI_COL_TEXT_DIM, ctx->font);
    }

    /* numeric value to the right */
    char val_buf[32];
    snprintf(val_buf, sizeof(val_buf), fmt, (double)*value);
    float val_x = x + bar_w + 4.f;
    float val_y = y + (h - th) * 0.5f;
    ctx->renderer->draw_text(val_x, val_y, val_buf, UI_COL_TEXT, ctx->font);

    return changed;
}

/* =========================================================
 * ui_knob
 * ========================================================= */

/*
 * Draws a circular knob:
 *   - filled arc from ~7 o'clock (min) to current value position
 *   - pointer line from centre toward the rim at the current angle
 *   - label below
 *   - numeric value below the label
 *
 * Angle convention: 0° = right; knob sweeps 240° from 7 o'clock
 * (150° measured clockwise from the positive X axis) around to 5 o'clock (30°).
 * The 60° gap sits at the bottom.
 *
 * All drawing is done with draw_line (already available in the renderer).
 * We approximate arcs with 32 line segments.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define KNOB_ARC_SEGS 32

static void draw_knob_arc(const ui_ctx_t *ctx, float cx, float cy, float r, float start_rad,
                          float end_rad, ui_color_t col, float thick) {
    float prev_x = cx + cosf(start_rad) * r;
    float prev_y = cy + sinf(start_rad) * r;
    for (int i = 1; i <= KNOB_ARC_SEGS; ++i) {
        float t = (float)i / KNOB_ARC_SEGS;
        float ang = start_rad + (end_rad - start_rad) * t;
        float nx = cx + cosf(ang) * r;
        float ny = cy + sinf(ang) * r;
        ctx->renderer->draw_line(prev_x, prev_y, nx, ny, col, thick);
        prev_x = nx;
        prev_y = ny;
    }
}

bool ui_knob(ui_ctx_t *ctx, float x, float y, float diameter, const char *label, float *value,
             float min, float max, float sensitivity, const char *fmt) {
    if (!fmt)
        fmt = "%.3g";

    float r = diameter * 0.5f;
    float cx = x + r;
    float cy = y + r;

    ui_id_t id = UI_ID(label);
    bool hovered = pt_in_rect(ctx->mouse_x, ctx->mouse_y, x, y, diameter, diameter);
    bool changed = false;

    if (hovered)
        ctx->hot = id;
    if (hovered && ctx->mouse_pressed)
        ctx->active = id;

    if (ctx->active == id && ctx->mouse_down && ctx->mouse_dy != 0.f) {
        /* Drag upward (negative yrel) → increase value */
        float delta = -ctx->mouse_dy * sensitivity;
        float newv = clampf(*value + delta, min, max);
        if (newv != *value) {
            *value = newv;
            changed = true;
        }
    }

    if (ctx->mouse_released && ctx->active == id)
        ctx->active = 0;

    /* --- drawing --- */

    /* angles: SDL screen coords have Y going down, so "up" on screen is
     * a negative Y direction.  Knob start = 7-o'clock, end = 5-o'clock.
     * In standard math angles (CCW positive from +X):
     *   7-o'clock ≈ 210° = 7π/6 ≈ 3.665 rad
     *   5-o'clock ≈ 330° = 11π/6 ≈ 5.760 rad   (wraps CW)
     * In SDL screen angles (CW positive from +X) the same points are:
     *   start = 150° in screen-space (π - 30°)  = 5π/6
     *   end   = 30°  in screen-space             = π/6
     * We sweep CW (increasing angle in screen space) across the bottom,
     * going through 90° (6 o'clock = π/2).
     * Total sweep = 240° = 4π/3.
     */
    const float start_ang = (float)(5.0 * M_PI / 6.0); /* 150° screen    */
    const float sweep = (float)(4.0 * M_PI / 3.0);     /* 240° total     */
    float t = (max > min) ? clampf((*value - min) / (max - min), 0.f, 1.f) : 0.f;
    float val_ang = start_ang + sweep * t;

    /* background circle (filled approximation via many thin lines) — kept
     * simple: just draw the body as a rect for now since the renderer has
     * no fill_circle primitive.  A square body with a circular feel. */
    ui_color_t body_col = (ctx->active == id) ? UI_COL_SURFACE_AC
                          : hovered           ? UI_COL_SURFACE_HO
                                              : UI_COL_SURFACE;
    ctx->renderer->draw_rect(x, y, diameter, diameter, body_col);

    /* track arc (dim) */
    float track_r = r * 0.78f;
    draw_knob_arc(ctx, cx, cy, track_r, start_ang, start_ang + sweep, UI_COL_BORDER, 2.f);

    /* value arc (accent) */
    if (t > 0.f)
        draw_knob_arc(ctx, cx, cy, track_r, start_ang, val_ang, UI_COL_ACCENT, 2.5f);

    /* pointer line from centre to rim */
    float ptr_x = cx + cosf(val_ang) * (r * 0.60f);
    float ptr_y = cy + sinf(val_ang) * (r * 0.60f);
    ctx->renderer->draw_line(cx, cy, ptr_x, ptr_y, UI_COL_TEXT, 2.f);

    /* label below the knob body */
    float th = text_h(ctx);
    float lw = text_w(ctx, label);
    float ly = y + diameter + 3.f;
    ctx->renderer->draw_text(cx - lw * 0.5f, ly, label, UI_COL_TEXT_DIM, ctx->font);

    /* numeric value below the label */
    char val_buf[32];
    snprintf(val_buf, sizeof(val_buf), fmt, (double)*value);
    float vw = text_w(ctx, val_buf);
    ctx->renderer->draw_text(cx - vw * 0.5f, ly + th + 2.f, val_buf, UI_COL_TEXT, ctx->font);

    return changed;
}
