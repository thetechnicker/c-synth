#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

/*
 * ui_widgets.h — immediate-mode widget system for c-synth
 *
 * Usage per frame:
 *   1. Feed SDL events:  ui_ctx_feed_event(&ctx, &event)   (in SDL_AppEvent)
 *   2. Begin frame:      ui_ctx_begin_frame(&ctx)           (before first widget)
 *   3. Call widgets:     ui_button(&ctx, ...) etc.
 *   4. End frame:        ui_ctx_end_frame(&ctx)             (after last widget)
 *
 * Layout helpers (optional — use when you don't want to hand-place everything):
 *   ui_layout_begin_row(&ctx, x, y, item_w, item_h, padding)
 *   ... widgets ...
 *   ui_layout_end_row(&ctx)
 */

#include "ui.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* =========================================================
 * Widget ID
 * ========================================================= */

typedef uint64_t ui_id_t;

/* djb2 hash of a string label — call with a unique per-widget string */
static inline ui_id_t ui_id(const char *label) {
    ui_id_t h = 5381;
    for (unsigned char c; (c = (unsigned char)*label) != 0; ++label)
        h = ((h << 5) + h) ^ c;
    return h ? h : 1; /* never return 0 — that means "none" */
}

#define UI_ID(label) ui_id(label)

/* =========================================================
 * Layout state (one active row at a time)
 * ========================================================= */

typedef struct {
    float x, y;     /* origin of the current row           */
    float item_w;   /* fixed width per cell                 */
    float item_h;   /* fixed height per cell                */
    float padding;  /* horizontal gap between cells         */
    float cursor_x; /* advances as widgets are placed       */
    bool active;    /* true while inside begin/end_row      */
} ui_layout_t;

/* =========================================================
 * Context
 * ========================================================= */

typedef struct {
    /* --- renderer (set once at init) --- */
    const ui_renderer_t *renderer;
    ui_font_t font;

    /* --- mouse state (updated by ui_ctx_feed_event) --- */
    float mouse_x, mouse_y;
    bool mouse_down;     /* button is currently held          */
    bool mouse_pressed;  /* went down this frame              */
    bool mouse_released; /* went up this frame                */
    float mouse_dy;      /* raw Y delta this frame (for knob) */
    float mouse_dx;      /* raw Y delta this frame (for knob) */
    float mouse_wx;      /* raw Y delta this frame (for knob) */
    float mouse_wy;      /* raw Y delta this frame (for knob) */

    /* --- hot / active widget --- */
    ui_id_t hot;    /* widget the mouse is over          */
    ui_id_t active; /* widget currently being interacted */

    /* --- layout --- */
    ui_layout_t layout;
} ui_ctx_t;

/* =========================================================
 * Lifecycle
 * ========================================================= */

/* Call once after the renderer is initialised. */
void ui_ctx_init(ui_ctx_t *ctx, const ui_renderer_t *renderer);

/* Call from SDL_AppEvent for every event. */
void ui_ctx_feed_event(ui_ctx_t *ctx, const SDL_Event *event);

/* Call at the start of SDL_AppIterate, before any widgets. */
void ui_ctx_begin_frame(ui_ctx_t *ctx);

/* Call at the end of SDL_AppIterate, after all widgets. */
void ui_ctx_end_frame(ui_ctx_t *ctx);

/* =========================================================
 * Layout helpers
 * ========================================================= */

/*
 * Begin a horizontal row.  All widgets placed between begin/end_row
 * use the layout cursor instead of a manually supplied position.
 * Pass item_w=0 to use the widget's own requested width.
 */
void ui_layout_begin_row(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                         float padding);

/* Advance the cursor by one cell.  Called internally by each widget
 * when a layout is active; you can also call it to skip a cell. */
void ui_layout_next(ui_ctx_t *ctx);

/* Close the current row. */
void ui_layout_end_row(ui_ctx_t *ctx);

/*
 * Fill r with the rect for the next layout cell.
 * Returns false if no layout is active (caller should use its own pos).
 */
bool ui_layout_cell(const ui_ctx_t *ctx, ui_rect_t *r);

/* =========================================================
 * Theme colours (overridable — change before drawing)
 * ========================================================= */

/* Backgrounds */
#define UI_COL_BG UI_RGB(30, 30, 40)
#define UI_COL_SURFACE UI_RGB(45, 45, 60)
#define UI_COL_SURFACE_HO UI_RGB(60, 60, 80)   /* hovered  */
#define UI_COL_SURFACE_AC UI_RGB(80, 140, 200) /* active   */

/* Accent / fill */
#define UI_COL_ACCENT UI_RGB(80, 140, 200)
#define UI_COL_ACCENT_DIM UI_RGB(50, 90, 140)

/* Text */
#define UI_COL_TEXT UI_RGB(220, 220, 220)
#define UI_COL_TEXT_DIM UI_RGB(140, 140, 150)

/* Toggle on/off */
#define UI_COL_ON UI_RGB(60, 200, 80)
#define UI_COL_OFF UI_RGB(80, 40, 40)

/* Borders */
#define UI_COL_BORDER UI_RGB(70, 70, 90)

/* =========================================================
 * Widgets
 * ========================================================= */

/*
 * ui_label — static text.
 * x, y: top-left of the text.  Pass (0,0) when inside a row layout.
 */
void ui_label(ui_ctx_t *ctx, float x, float y, const char *text, ui_color_t color);

/*
 * ui_button — clickable rectangle with a centred label.
 * Returns true on the frame the button is clicked (mouse released inside).
 * x, y, w, h: ignored when inside an active row layout.
 */
bool ui_button(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label);

/*
 * ui_toggle — LED-style on/off button.
 * *value is flipped when clicked.  Returns true when the value changed.
 */
bool ui_toggle(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, bool *value);

/*
 * ui_slider_f — horizontal fill bar for a float in [min, max].
 * Always draws the current numeric value as text to the right of the bar.
 * *value is clamped to [min, max].  Returns true when the value changed.
 *
 * fmt: printf format string for the value label, e.g. "%.2f" or "%+.1f".
 *      Pass NULL to use "%.3g".
 */
bool ui_slider_f(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, float *value,
                 float min, float max, const char *fmt);

/*
 * ui_knob — rotary control.
 * Drag upward to increase, downward to decrease.
 * sensitivity: value-units per pixel of drag (e.g. (max-min)/200.0f).
 * Draws a small arc + pointer and the current value below.
 * Returns true when the value changed.
 */
bool ui_knob(ui_ctx_t *ctx, float x, float y, float diameter, const char *label, float *value,
             float min, float max, float sensitivity, const char *fmt);

#endif /* UI_WIDGETS_H */
