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
 * Layout helpers:
 *   Horizontal row:
 *     ui_layout_begin_row(&ctx, x, y, item_w, item_h, padding)
 *     ... widgets ...
 *     ui_layout_end_row(&ctx)
 *
 *   Vertical column:
 *     ui_layout_begin_column(&ctx, x, y, item_w, item_h, padding)
 *     ... widgets ...
 *     ui_layout_end_column(&ctx)
 *
 *   Multi-column fixed row:
 *     float widths[] = {120.f, 200.f, 80.f};
 *     ui_layout_row(&ctx, x, y, 3, widths, item_h)
 *     ... widgets ...
 *     ui_layout_end_row(&ctx)
 *
 *   Scope nesting (for unique IDs across repeated widget labels):
 *     ui_layout_push_id(&ctx, ui_id("synth_osc1"))
 *     ... widgets ...
 *     ui_layout_pop_id(&ctx)
 */

#include "ui.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* =========================================================
 * Widget ID
 * ========================================================= */

typedef uint64_t ui_id_t;

/* djb2 hash of a string label */
static inline ui_id_t ui_id(const char *label) {
    ui_id_t h = 5381;
    for (unsigned char c; (c = (unsigned char)*label) != 0; ++label)
        h = ((h << 5) + h) ^ c;
    return h ? h : 1;
}

#define UI_ID(label) ui_id(label)

/* =========================================================
 * Theming
 * ========================================================= */

typedef struct {
    /* Backgrounds */
    ui_color_t bg;         /* window background                */
    ui_color_t surface;    /* widget background (normal)       */
    ui_color_t surface_ho; /* widget background (hovered)      */
    ui_color_t surface_ac; /* widget background (active/press) */

    /* Accent fills */
    ui_color_t accent;     /* slider/knob fill, active bar     */
    ui_color_t accent_dim; /* slider/knob fill (inactive)      */

    /* Text */
    ui_color_t text;     /* primary text                     */
    ui_color_t text_dim; /* secondary / label text           */

    /* State */
    ui_color_t on;  /* toggle ON colour                 */
    ui_color_t off; /* toggle OFF colour                */

    /* Borders */
    ui_color_t border; /* widget border                    */

    /* DPI / font scale (1.0 = no scaling) */
    float dpi_scale;
} ui_theme_t;

/* Three built-in themes — defined in ui_widgets.c */
extern const ui_theme_t UI_THEME_DARK;
extern const ui_theme_t UI_THEME_LIGHT;
extern const ui_theme_t UI_THEME_SYNTHWAVE;

/* Set the active theme.  Pass NULL to revert to UI_THEME_DARK. */
void ui_set_theme(const ui_theme_t *theme);

/* Internal accessor — widgets use this instead of raw macro constants. */
const ui_theme_t *ui_get_theme(void);

/* Convenience accessors used by widget drawing code */
#define UI_COL_BG (ui_get_theme()->bg)
#define UI_COL_SURFACE (ui_get_theme()->surface)
#define UI_COL_SURFACE_HO (ui_get_theme()->surface_ho)
#define UI_COL_SURFACE_AC (ui_get_theme()->surface_ac)
#define UI_COL_ACCENT (ui_get_theme()->accent)
#define UI_COL_ACCENT_DIM (ui_get_theme()->accent_dim)
#define UI_COL_TEXT (ui_get_theme()->text)
#define UI_COL_TEXT_DIM (ui_get_theme()->text_dim)
#define UI_COL_ON (ui_get_theme()->on)
#define UI_COL_OFF (ui_get_theme()->off)
#define UI_COL_BORDER (ui_get_theme()->border)

/* =========================================================
 * Layout state
 * ========================================================= */

#define UI_LAYOUT_ID_STACK_DEPTH 16
#define UI_LAYOUT_COL_MAX 16

typedef enum {
    UI_LAYOUT_HORIZONTAL = 0,
    UI_LAYOUT_VERTICAL = 1,
} ui_layout_dir_t;

typedef struct {
    /* origin and per-cell sizing */
    float x, y;
    float item_w;
    float item_h;
    float padding;

    /* cursor (advances along the primary axis) */
    float cursor_x;
    float cursor_y;

    /* flow direction */
    ui_layout_dir_t dir;

    /* multi-column mode: widths[col_count] overrides item_w */
    int col_count;
    float col_widths[UI_LAYOUT_COL_MAX];
    int col_cursor; /* which column the next cell falls in  */

    /* auto-size: if item_w==0 the widget supplies its own width */
    float min_w, max_w; /* 0 = unconstrained                    */

    bool active;
} ui_layout_t;

/* =========================================================
 * Context
 * ========================================================= */

#define UI_TEXT_BUF_MAX 256

typedef struct {
    /* --- renderer (set once at init) --- */
    const ui_renderer_t *renderer;
    ui_font_t font;

    /* --- mouse state --- */
    float mouse_x, mouse_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;
    float mouse_dy;
    float mouse_dx;
    float mouse_wx;
    float mouse_wy;
    int disc_mouse_wx;
    int disc_mouse_wy;

    /* --- keyboard / text state --- */
    ui_id_t focused;    /* widget that has keyboard focus    */
    char key_text[8];   /* UTF-8 from SDL_EVENT_TEXT_INPUT   */
    bool key_backspace; /* backspace pressed this frame      */
    bool key_delete;    /* delete pressed this frame         */
    bool key_left;      /* left arrow pressed this frame     */
    bool key_right;     /* right arrow pressed this frame    */
    bool key_up;        /* up arrow pressed this frame       */
    bool key_down;      /* down arrow pressed this frame     */
    bool key_home;      /* home pressed this frame           */
    bool key_end;       /* end pressed this frame            */
    bool key_enter;     /* enter/return pressed this frame   */
    bool key_escape;    /* escape pressed this frame         */
    SDL_Keymod key_mod; /* modifier state (shift, ctrl, …)   */

    /* --- hot / active widget --- */
    ui_id_t hot;
    ui_id_t active;

    /* --- scope ID stack (for push/pop) --- */
    ui_id_t id_stack[UI_LAYOUT_ID_STACK_DEPTH];
    int id_stack_top;

    /* --- layout --- */
    ui_layout_t layout;
} ui_ctx_t;

/* =========================================================
 * Lifecycle
 * ========================================================= */

void ui_ctx_init(ui_ctx_t *ctx, const ui_renderer_t *renderer);
void ui_ctx_feed_event(ui_ctx_t *ctx, const SDL_Event *event);
void ui_ctx_begin_frame(ui_ctx_t *ctx);
void ui_ctx_end_frame(ui_ctx_t *ctx);

/* =========================================================
 * Scope / ID nesting
 * ========================================================= */

/*
 * Push an extra ID seed onto the stack.  All widget IDs created while
 * the seed is on the stack are hashed together with it, so two widgets
 * with the same label string in different scopes get unique IDs.
 *
 * Example:
 *   ui_layout_push_id(&ctx, ui_id("osc1"));
 *   ui_slider_f(&ctx, 0,0,0,0, "Freq", &osc1_freq, 20,20000, NULL);
 *   ui_layout_pop_id(&ctx);
 *   ui_layout_push_id(&ctx, ui_id("osc2"));
 *   ui_slider_f(&ctx, 0,0,0,0, "Freq", &osc2_freq, 20,20000, NULL);
 *   ui_layout_pop_id(&ctx);
 */
void ui_layout_push_id(ui_ctx_t *ctx, ui_id_t seed);
void ui_layout_pop_id(ui_ctx_t *ctx);

/* =========================================================
 * Layout helpers
 * ========================================================= */

/* Horizontal row — all cells share item_w × item_h. */
void ui_layout_begin_row(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                         float padding);

/* Vertical column — all cells share item_w × item_h. */
void ui_layout_begin_column(ui_ctx_t *ctx, float x, float y, float item_w, float item_h,
                            float padding);

/*
 * Multi-column fixed-width row.
 * widths[col_count] gives the width of each column; item_h is the row height.
 * Successive widgets consume widths[0], widths[1], … in order.
 */
void ui_layout_row(ui_ctx_t *ctx, float x, float y, int col_count, const float *widths,
                   float item_h);

/* Advance the cursor by one cell (called internally by each widget). */
void ui_layout_next(ui_ctx_t *ctx);

/* End any active layout. */
void ui_layout_end_row(ui_ctx_t *ctx);
void ui_layout_end_column(ui_ctx_t *ctx); /* alias for clarity */

/*
 * Fill r with the rect for the next layout cell.
 * Returns false if no layout is active.
 */
bool ui_layout_cell(const ui_ctx_t *ctx, ui_rect_t *r);

/* =========================================================
 * Widgets
 * ========================================================= */

/* ui_label — static text. */
void ui_label(ui_ctx_t *ctx, float x, float y, const char *text, ui_color_t color);

/* ui_button — returns true on click. */
bool ui_button(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label);

/* ui_toggle — LED-style on/off; returns true when value changed. */
bool ui_toggle(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, bool *value);

/* ui_slider_f — float slider [min,max]; returns true when changed. */
bool ui_slider_f(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, float *value,
                 float min, float max, const char *fmt);

/* ui_slider_i — integer slider [min,max]; returns true when changed. */
bool ui_slider_i(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, int *value,
                 int min, int max, const char *fmt);

/* ui_knob — rotary control; returns true when changed. */
bool ui_knob(ui_ctx_t *ctx, float x, float y, float diameter, const char *label, float *value,
             float min, float max, float sensitivity, const char *fmt);

/*
 * ui_separator — horizontal or vertical dividing line.
 * x,y,len,thickness: geometry.  Pass (0,0,0,0) inside a layout to use
 * the layout cell (separator spans the full cell width or height).
 * vertical: false = horizontal bar, true = vertical bar.
 */
void ui_separator(ui_ctx_t *ctx, float x, float y, float len, float thickness, bool vertical);

/*
 * ui_scope / ui_scope_end — draw a labelled panel box around a group of
 * widgets.  ui_scope pushes a named ID and draws the panel outline;
 * ui_scope_end pops the ID.
 *
 * x,y,w,h: the bounding rect of the panel.
 * label: drawn in the top-left corner of the border.
 *
 * Widgets drawn between ui_scope / ui_scope_end are NOT automatically
 * clipped to the panel; this is purely a visual grouping aid.
 */
void ui_scope(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label);
void ui_scope_end(ui_ctx_t *ctx);

/*
 * ui_dropdown — drop-down selector.
 * items:    array of string pointers (the option labels).
 * count:    number of items.
 * selected: pointer to the currently selected index [0, count-1].
 * Returns true when the selection changes.
 *
 * The dropdown is two-state: closed (shows selected item + arrow) and open
 * (shows a flat list below the header).  Clicking outside while open closes
 * it.  The open list is drawn on top; call ui_dropdown last in a frame if
 * it may overlap other widgets.
 */
bool ui_dropdown(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label,
                 const char *const *items, int count, int *selected);

/*
 * ui_text_input — single-line editable text field.
 * buf:     the text buffer (null-terminated, modified in place).
 * buf_len: total size of buf in bytes (including the null terminator).
 *
 * Clicking the field gives it keyboard focus.  While focused:
 *   - Printable characters are appended (or inserted at cursor).
 *   - Backspace deletes the character before the cursor.
 *   - Delete removes the character after the cursor.
 *   - Left / Right arrows move the cursor.
 *   - Home / End jump to start / end.
 *   - Enter or Escape removes focus.
 *
 * Returns true every frame the buffer has been modified.
 */
bool ui_text_input(ui_ctx_t *ctx, float x, float y, float w, float h, const char *label, char *buf,
                   int buf_len);

/*
 * ui_waveform_display — oscilloscope-style waveform viewer.
 * samples: array of float samples in [-1.0, +1.0].
 * count:   number of samples to display (mapped across the full width).
 * color:   waveform line colour.
 */
void ui_waveform_display(ui_ctx_t *ctx, float x, float y, float w, float h, const float *samples,
                         int count, ui_color_t color);

/*
 * ui_spectrum_display — FFT magnitude bar display.
 * bins:  array of magnitude values in [0.0, 1.0] (normalised).
 * count: number of frequency bins.
 * color: bar fill colour.
 */
void ui_spectrum_display(ui_ctx_t *ctx, float x, float y, float w, float h, const float *bins,
                         int count, ui_color_t color);

#endif /* UI_WIDGETS_H */
