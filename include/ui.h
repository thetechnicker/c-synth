#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t ui_color_t; /* packed RGBA8: 0xRRGGBBAA */

#define UI_RGBA(r, g, b, a)                                                                        \
    (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | ((uint32_t)(a)))
#define UI_RGB(r, g, b) UI_RGBA(r, g, b, 0xFF)

typedef struct {
    float x, y;
} ui_vec2_t;
typedef struct {
    float x, y, w, h;
} ui_rect_t;

/* opaque font and texture handles */
typedef struct ui_texture_t {
    uint64_t id;
} ui_texture_t;
typedef struct ui_font_t {
    uint64_t id;
    int glyph_w, glyph_h;
} ui_font_t;

/* Renderer interface */
typedef struct ui_renderer_t {
    const char *name;
    bool (*init)(int width, int height, const char *title);
    void (*begin_frame)(void);
    void (*end_frame)(void);
    void (*shutdown)(void);

    /* basic draws */
    void (*draw_rect)(float x, float y, float w, float h, ui_color_t color);
    void (*draw_line)(float x1, float y1, float x2, float y2, ui_color_t color, float thickness);
    void (*draw_text)(float x, float y, const char *text, ui_color_t color, ui_font_t font);
    void (*draw_image)(float x, float y, float w, float h, ui_texture_t texture, ui_color_t tint);

    /* textures */
    ui_texture_t (*create_texture)(const void *pixels, int w, int h, int channels);
    void (*destroy_texture)(ui_texture_t);

    /* optional: framebuffer/resize */
    void (*on_resize)(int width, int height);
} ui_renderer_t;


#if defined(_MSC_VER)
  /* MSVC: place a pointer to the function in .CRT$XCU */
  #pragma section(".CRT$XCU", read)
  typedef void (__cdecl *ctor_fn_t)(void);

  /* Helper to avoid name-pasting issues in C++ */
  #ifdef __cplusplus
    #define CRT_CTOR_NAME(fn) fn##_msvc_ctor
  #else
    #define CRT_CTOR_NAME(fn) fn##_msvc_ctor
  #endif

  #define REGISTER_CONSTRUCTOR(fn)                                      \
    extern void fn(void);                                               \
    __declspec(allocate(".CRT$XCU")) static ctor_fn_t CRT_CTOR_NAME(fn) = fn

#else /* GCC / Clang and compatibles */
  #define REGISTER_CONSTRUCTOR(fn) \
    static void fn(void) __attribute__((constructor)); \
    static void fn(void)
#endif

/* registry */
bool ui_register_renderer(const ui_renderer_t *renderer);
const ui_renderer_t *ui_get_renderer(const char *name);
const ui_renderer_t *ui_get_default_renderer(void);

#endif
