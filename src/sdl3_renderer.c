/*
 * sdl3_renderer.c — SDL3 GPU (primary) + SDL_Renderer (fallback) backend
 *
 * Targets SDL 3.x. Verify enum/struct field names against your installed
 * SDL3 headers; the GPU API surface was still being finalised in early
 * SDL 3.x releases.
 *
 * -----------------------------------------------------------------------
 * SHADER SOURCES — compile once with glslc (from the Vulkan SDK):
 *
 *   glslc color.vert   -o color.vert.spv
 *   glslc color.frag   -o color.frag.spv
 *   glslc texture.vert -o texture.vert.spv
 *   glslc texture.frag -o texture.frag.spv
 *
 * The four .spv files must live next to the executable at runtime.
 *
 * ---- color.vert ---- (vertex layout: vec2 pos, vec4 col — stride 24 B)
 *
 *   #version 450
 *   layout(push_constant) uniform PC { float inv_w; float inv_h; } pc;
 *   layout(location = 0) in  vec2 a_pos;
 *   layout(location = 1) in  vec4 a_col;
 *   layout(location = 0) out vec4 v_col;
 *   void main() {
 *       gl_Position = vec4(a_pos.x * pc.inv_w * 2.0 - 1.0,
 *                          1.0 - a_pos.y * pc.inv_h * 2.0, 0.0, 1.0);
 *       v_col = a_col;
 *   }
 *
 * ---- color.frag ----
 *
 *   #version 450
 *   layout(location = 0) in  vec4 v_col;
 *   layout(location = 0) out vec4 o_col;
 *   void main() { o_col = v_col; }
 *
 * ---- texture.vert ---- (vertex layout: vec2 pos, vec2 uv, vec4 col — stride 32 B)
 *
 *   #version 450
 *   layout(push_constant) uniform PC { float inv_w; float inv_h; } pc;
 *   layout(location = 0) in  vec2 a_pos;
 *   layout(location = 1) in  vec2 a_uv;
 *   layout(location = 2) in  vec4 a_col;
 *   layout(location = 0) out vec2 v_uv;
 *   layout(location = 1) out vec4 v_col;
 *   void main() {
 *       gl_Position = vec4(a_pos.x * pc.inv_w * 2.0 - 1.0,
 *                          1.0 - a_pos.y * pc.inv_h * 2.0, 0.0, 1.0);
 *       v_uv  = a_uv;
 *       v_col = a_col;
 *   }
 *
 * ---- texture.frag ----
 *
 *   #version 450
 *   layout(set = 2, binding = 0) uniform sampler2D u_tex;
 *   layout(location = 0) in  vec2 v_uv;
 *   layout(location = 1) in  vec4 v_col;
 *   layout(location = 0) out vec4 o_col;
 *   void main() { o_col = texture(u_tex, v_uv) * v_col; }
 *
 * -----------------------------------------------------------------------
 * NOTE: SPIR-V targets Vulkan. For Metal or D3D12 supply MSL / DXIL
 * variants, pass the matching SDL_GPUShaderFormat to SDL_CreateGPUDevice()
 * and update the .format field in load_spv(). SDL_shadercross can
 * automate cross-compilation at runtime.
 * -----------------------------------------------------------------------
 */

#include "log.h"
#include "ui.h"
#include "ui_font_builtin.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* =========================================================
 * Function signatures
 * ========================================================= */

static void unpack(ui_color_t c, float *r, float *gn, float *b, float *a);
static inline Uint8 uf(float v);
static inline ui_texture_t pack_gpu(SDL_GPUTexture *t);
static inline ui_texture_t pack_rdr(SDL_Texture *t);
static inline SDL_GPUTexture *as_gpu(ui_texture_t t);
static inline SDL_Texture *as_rdr(ui_texture_t t);

static SDL_GPUShader *load_spv(const char *path, SDL_GPUShaderStage stage, Uint32 n_uniform,
                               Uint32 n_sampler);
static SDL_GPUColorTargetDescription make_blend_target(SDL_GPUTextureFormat fmt);
static SDL_GPUGraphicsPipeline *make_color_pipe(SDL_GPUTextureFormat fmt);
static SDL_GPUGraphicsPipeline *make_tex_pipe(SDL_GPUTextureFormat fmt);

static bool gpu_setup(void);
static void gpu_teardown(void);

static void push_cv_quad(float x, float y, float w, float h, ui_color_t c);
static void push_cv_line(float x1, float y1, float x2, float y2, float thick, ui_color_t c);
static void push_tv_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         ui_color_t tint);
static void push_text_verts(float x, float y, const char *text, ui_color_t col, ui_font_t font);

static void gpu_flush(void);
static void rdr_thick_line(float x1, float y1, float x2, float y2, float thick, ui_color_t c);
static void rdr_flush(void);

static void sdl3_draw_rect(float x, float y, float w, float h, ui_color_t color);
static void sdl3_draw_line(float x1, float y1, float x2, float y2, ui_color_t color,
                           float thickness);
static void sdl3_draw_text(float x, float y, const char *text, ui_color_t color, ui_font_t font);
static void sdl3_draw_image(float x, float y, float w, float h, ui_texture_t texture,
                            ui_color_t tint);

static Uint8 *to_rgba(const void *pixels, int w, int h, int channels);
static ui_texture_t sdl3_create_texture(const void *pixels, int w, int h, int channels);
static void sdl3_destroy_texture(ui_texture_t t);

static bool sdl3_init(int width, int height, const char *title);
static void sdl3_begin_frame(void);
static void sdl3_end_frame(void);
static void sdl3_shutdown(void);
static void sdl3_on_resize(int w, int h);

ui_font_t ui_font_builtin(void);

/* =========================================================
 * Compile-time tunables
 * ========================================================= */

/* Maximum deferred draw commands per frame. */
#define MAX_CMDS 4096

/* String pool — all draw_text strings are copied here. */
#define STR_POOL_SIZE (MAX_CMDS * 64)

/*
 * Per-frame vertex budgets.
 *
 * Color path:  rect  → 6 verts, line → 6 verts.
 * Texture path: image → 6 verts, text → 6 verts per glyph.
 *              Assume up to 32 glyphs per text command on average.
 */
#define MAX_COLOR_VERTS (MAX_CMDS * 12)   /*  ~1 MB  */
#define MAX_TEX_VERTS (MAX_CMDS * 32 * 6) /*  ~24 MB */

/* ASCII glyph range blit-mapped from the font atlas. */
#define GLYPH_FIRST 32
#define GLYPH_LAST 126
#define GLYPH_COLS 10 /* columns packed into the atlas */

/*
 * Top bit of ui_texture_t.id tags SDL_Renderer textures so
 * destroy_texture can dispatch correctly without querying state.
 * Safe on all current 64-bit platforms (AMD64 uses ≤48-bit VA,
 * ARM64 ≤52-bit VA).
 */
#define RENDERER_TEX_BIT (UINT64_C(1) << 63)

/* =========================================================
 * Vertex types
 * ========================================================= */

/* 24 bytes — color pipeline */
typedef struct {
    float x, y, r, g, b, a;
} cv_t;

/* 32 bytes — texture pipeline */
typedef struct {
    float x, y, u, v, r, g, b, a;
} tv_t;

/* =========================================================
 * Deferred draw command
 * ========================================================= */

typedef enum { CMD_RECT, CMD_LINE, CMD_TEXT, CMD_IMAGE } cmd_type_t;

typedef struct {
    cmd_type_t type;
    union {
        struct {
            float x, y, w, h;
            ui_color_t col;
        } rect;

        struct {
            float x1, y1, x2, y2, thick;
            ui_color_t col;
        } line;

        struct {
            float x, y;
            ui_color_t col;
            ui_font_t font;
            Uint32 soff; /* byte offset into spool[] */
        } text;

        struct {
            float x, y, w, h;
            ui_texture_t tex;
            ui_color_t tint;
        } image;
    };
} draw_cmd_t;

/* =========================================================
 * Push-constant block (both vertex shaders share the layout)
 * ========================================================= */

typedef struct {
    float inv_w, inv_h;
} push_const_t;

/* =========================================================
 * Backend context
 * ========================================================= */

typedef enum { PATH_GPU, PATH_RDR } render_path_t;

typedef struct {

    /* ---------- window ---------- */
    SDL_Window *win;
    int width, height;
    render_path_t path;

    /* ---------- GPU path ---------- */
    SDL_GPUDevice *gpu;
    SDL_GPUGraphicsPipeline *cpipe; /* color   pipeline */
    SDL_GPUGraphicsPipeline *tpipe; /* texture pipeline */
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *cvbuf;         /* GPU vertex buffer — color   */
    SDL_GPUBuffer *tvbuf;         /* GPU vertex buffer — texture */
    SDL_GPUTransferBuffer *ctbuf; /* transfer buffer  — color    */
    SDL_GPUTransferBuffer *ttbuf; /* transfer buffer  — texture  */

    /* per-frame GPU state */
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *sc; /* current swapchain texture   */
    Uint32 sc_w, sc_h;

    /* ---------- SDL_Renderer path ---------- */
    SDL_Renderer *rdr;

    /* ---------- deferred command list ---------- */
    draw_cmd_t cmds[MAX_CMDS];
    Uint32 ncmds;
    char spool[STR_POOL_SIZE];
    Uint32 spool_used;

    /* ---------- per-frame vertex scratch (GPU path) ---------- */
    cv_t cverts[MAX_COLOR_VERTS];
    Uint32 ncverts;
    tv_t tverts[MAX_TEX_VERTS];
    Uint32 ntverts;

    /*
     * Vertex ranges per command, filled in the first pass of gpu_flush().
     * Stored in the context to avoid large stack allocations.
     */
    Uint32 cv_off[MAX_CMDS];
    Uint32 cv_cnt[MAX_CMDS];
    Uint32 tv_off[MAX_CMDS];
    Uint32 tv_cnt[MAX_CMDS];

    /* ---------- Build in defaults ---------- */
    ui_texture_t builtin_font_tex;

} ctx_t;

static ctx_t ctx; /* zero-initialised at program start */

/* =========================================================
 * Colour helpers
 * ========================================================= */

static inline void unpack(ui_color_t c, float *r, float *gn, float *b, float *a) {
    *r = ((c >> 24) & 0xFF) / 255.0f;
    *gn = ((c >> 16) & 0xFF) / 255.0f;
    *b = ((c >> 8) & 0xFF) / 255.0f;
    *a = ((c) & 0xFF) / 255.0f;
}

static inline Uint8 uf(float v) { return (Uint8)(v * 255.0f + 0.5f); }

/* =========================================================
 * Texture handle packing / unpacking
 * ========================================================= */

static inline ui_texture_t pack_gpu(SDL_GPUTexture *t) {
    return (ui_texture_t){(Uint64)(uintptr_t)t};
}

static inline ui_texture_t pack_rdr(SDL_Texture *t) {
    return (ui_texture_t){(Uint64)(uintptr_t)t | RENDERER_TEX_BIT};
}

static inline SDL_GPUTexture *as_gpu(ui_texture_t t) { return (SDL_GPUTexture *)(uintptr_t)t.id; }

static inline SDL_Texture *as_rdr(ui_texture_t t) {
    return (SDL_Texture *)(uintptr_t)(t.id & ~RENDERER_TEX_BIT);
}

/* =========================================================
 * GPU: shader loading from pre-compiled SPIR-V files
 * ========================================================= */

static SDL_GPUShader *load_spv(const char *path, SDL_GPUShaderStage stage, Uint32 n_uniform,
                               Uint32 n_sampler) {
    size_t sz = 0;
    void *code = SDL_LoadFile(path, &sz);
    if (!code) {
        LOGW("cannot load shader '%s': %s", path, SDL_GetError());
        return NULL;
    }

    SDL_GPUShaderCreateInfo ci = {
        .code = code,
        .code_size = sz,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = stage,
        .num_uniform_buffers = n_uniform,
        .num_samplers = n_sampler,
    };

    SDL_GPUShader *sh = SDL_CreateGPUShader(ctx.gpu, &ci);
    SDL_free(code);

    if (!sh)
        LOGW("SDL_CreateGPUShader(%s): %s", path, SDL_GetError());
    return sh;
}

/* =========================================================
 * GPU: pipeline creation
 * ========================================================= */

static SDL_GPUColorTargetDescription make_blend_target(SDL_GPUTextureFormat fmt) {
    return (SDL_GPUColorTargetDescription){
        .format = fmt,
        .blend_state =
            {
                .enable_blend = true,
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            },
    };
}

static SDL_GPUGraphicsPipeline *make_color_pipe(SDL_GPUTextureFormat fmt) {
    SDL_GPUShader *vs = load_spv("shaders/color.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *fs = load_spv("shaders/color.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vs || !fs)
        goto fail;

    SDL_GPUVertexAttribute attrs[] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = 0},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = 8},
    };
    SDL_GPUVertexBufferDescription vbd = {
        .slot = 0,
        .pitch = sizeof(cv_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };
    SDL_GPUColorTargetDescription ctd = make_blend_target(fmt);

    SDL_GPUGraphicsPipelineCreateInfo pci = {
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = &vbd,
                .num_vertex_buffers = 1,
                .vertex_attributes = attrs,
                .num_vertex_attributes = 2,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info =
            {
                .color_target_descriptions = &ctd,
                .num_color_targets = 1,
            },
    };

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(ctx.gpu, &pci);
    SDL_ReleaseGPUShader(ctx.gpu, vs);
    SDL_ReleaseGPUShader(ctx.gpu, fs);
    if (!p)
        LOGW("color pipeline: %s", SDL_GetError());
    return p;

fail:
    SDL_ReleaseGPUShader(ctx.gpu, vs);
    SDL_ReleaseGPUShader(ctx.gpu, fs);
    return NULL;
}

static SDL_GPUGraphicsPipeline *make_tex_pipe(SDL_GPUTextureFormat fmt) {
    SDL_GPUShader *vs = load_spv("shaders/texture.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *fs = load_spv("shaders/texture.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    if (!vs || !fs)
        goto fail;

    SDL_GPUVertexAttribute attrs[] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = 0},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = 8},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = 16},
    };
    SDL_GPUVertexBufferDescription vbd = {
        .slot = 0,
        .pitch = sizeof(tv_t),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };
    SDL_GPUColorTargetDescription ctd = make_blend_target(fmt);

    SDL_GPUGraphicsPipelineCreateInfo pci = {
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = &vbd,
                .num_vertex_buffers = 1,
                .vertex_attributes = attrs,
                .num_vertex_attributes = 3,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info =
            {
                .color_target_descriptions = &ctd,
                .num_color_targets = 1,
            },
    };

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(ctx.gpu, &pci);
    SDL_ReleaseGPUShader(ctx.gpu, vs);
    SDL_ReleaseGPUShader(ctx.gpu, fs);
    if (!p)
        LOGW("texture pipeline: %s", SDL_GetError());
    return p;

fail:
    SDL_ReleaseGPUShader(ctx.gpu, vs);
    SDL_ReleaseGPUShader(ctx.gpu, fs);
    return NULL;
}

/* =========================================================
 * GPU: device / resource init and teardown
 * ========================================================= */

static bool gpu_setup(void) {
    ctx.gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
    if (!ctx.gpu)
        return false; /* no Vulkan — fall back to SDL_Renderer */

    if (!SDL_ClaimWindowForGPUDevice(ctx.gpu, ctx.win)) {
        LOGW("SDL_ClaimWindowForGPUDevice: %s", SDL_GetError());
        goto fail;
    }

    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(ctx.gpu, ctx.win);

    ctx.cpipe = make_color_pipe(fmt);
    ctx.tpipe = make_tex_pipe(fmt);
    if (!ctx.cpipe || !ctx.tpipe)
        goto fail;

    /* nearest-neighbour sampler — keeps pixel art / bitmap fonts crisp */
    ctx.sampler = SDL_CreateGPUSampler(
        ctx.gpu, &(SDL_GPUSamplerCreateInfo){
                     .min_filter = SDL_GPU_FILTER_NEAREST,
                     .mag_filter = SDL_GPU_FILTER_NEAREST,
                     .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
                     .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                     .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                     .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                 });
    if (!ctx.sampler)
        goto fail;

    /* GPU-side vertex buffers */
    const Uint32 cv_bytes = MAX_COLOR_VERTS * (Uint32)sizeof(cv_t);
    const Uint32 tv_bytes = MAX_TEX_VERTS * (Uint32)sizeof(tv_t);

    ctx.cvbuf = SDL_CreateGPUBuffer(
        ctx.gpu, &(SDL_GPUBufferCreateInfo){.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = cv_bytes});
    ctx.tvbuf = SDL_CreateGPUBuffer(
        ctx.gpu, &(SDL_GPUBufferCreateInfo){.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = tv_bytes});
    if (!ctx.cvbuf || !ctx.tvbuf)
        goto fail;

    /* persistent upload transfer buffers (reused every frame) */
    ctx.ctbuf = SDL_CreateGPUTransferBuffer(
        ctx.gpu, &(SDL_GPUTransferBufferCreateInfo){.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                    .size = cv_bytes});
    ctx.ttbuf = SDL_CreateGPUTransferBuffer(
        ctx.gpu, &(SDL_GPUTransferBufferCreateInfo){.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                    .size = tv_bytes});
    if (!ctx.ctbuf || !ctx.ttbuf)
        goto fail;

    return true;

fail:
    LOGW("GPU setup failed: %s", SDL_GetError());
    /* Full cleanup happens in gpu_teardown(), called from sdl3_shutdown(). */
    return false;
}

static void gpu_teardown(void) {
    if (!ctx.gpu)
        return;

    SDL_WaitForGPUIdle(ctx.gpu);

#define RELEASE_IF(fn, ptr)                                                                        \
    do {                                                                                           \
        if (ctx.ptr) {                                                                             \
            fn(ctx.gpu, ctx.ptr);                                                                  \
            ctx.ptr = NULL;                                                                        \
        }                                                                                          \
    } while (0)

    RELEASE_IF(SDL_ReleaseGPUTransferBuffer, ctbuf);
    RELEASE_IF(SDL_ReleaseGPUTransferBuffer, ttbuf);
    RELEASE_IF(SDL_ReleaseGPUBuffer, cvbuf);
    RELEASE_IF(SDL_ReleaseGPUBuffer, tvbuf);
    RELEASE_IF(SDL_ReleaseGPUSampler, sampler);
    RELEASE_IF(SDL_ReleaseGPUGraphicsPipeline, cpipe);
    RELEASE_IF(SDL_ReleaseGPUGraphicsPipeline, tpipe);

#undef RELEASE_IF

    SDL_ReleaseWindowFromGPUDevice(ctx.gpu, ctx.win);
    SDL_DestroyGPUDevice(ctx.gpu);
    ctx.gpu = NULL;
}

/* =========================================================
 * Vertex scratch helpers
 * ========================================================= */

/* Two triangles forming an axis-aligned quad → 6 color verts. */
static void push_cv_quad(float x, float y, float w, float h, ui_color_t c) {
    if (ctx.ncverts + 6 > MAX_COLOR_VERTS)
        return;
    float r, gn, b, a;
    unpack(c, &r, &gn, &b, &a);
    cv_t *v = ctx.cverts + ctx.ncverts;
    v[0] = (cv_t){x, y, r, gn, b, a};
    v[1] = (cv_t){x + w, y, r, gn, b, a};
    v[2] = (cv_t){x + w, y + h, r, gn, b, a};
    v[3] = (cv_t){x, y, r, gn, b, a};
    v[4] = (cv_t){x + w, y + h, r, gn, b, a};
    v[5] = (cv_t){x, y + h, r, gn, b, a};
    ctx.ncverts += 6;
}

/* Thick line as a rotated quad → 6 color verts. */
static void push_cv_line(float x1, float y1, float x2, float y2, float thick, ui_color_t c) {
    if (ctx.ncverts + 6 > MAX_COLOR_VERTS)
        return;
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f)
        return;
    float nx = -dy / len * (thick * 0.5f);
    float ny = dx / len * (thick * 0.5f);
    float r, gn, b, a;
    unpack(c, &r, &gn, &b, &a);
    cv_t p0 = {x1 + nx, y1 + ny, r, gn, b, a};
    cv_t p1 = {x1 - nx, y1 - ny, r, gn, b, a};
    cv_t p2 = {x2 - nx, y2 - ny, r, gn, b, a};
    cv_t p3 = {x2 + nx, y2 + ny, r, gn, b, a};
    cv_t *v = ctx.cverts + ctx.ncverts;
    v[0] = p0;
    v[1] = p1;
    v[2] = p2;
    v[3] = p0;
    v[4] = p2;
    v[5] = p3;
    ctx.ncverts += 6;
}

/* Axis-aligned textured quad → 6 tex verts. */
static void push_tv_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         ui_color_t tint) {
    if (ctx.ntverts + 6 > MAX_TEX_VERTS)
        return;
    float r, gn, b, a;
    unpack(tint, &r, &gn, &b, &a);
    tv_t *v = ctx.tverts + ctx.ntverts;
    v[0] = (tv_t){x, y, u0, v0, r, gn, b, a};
    v[1] = (tv_t){x + w, y, u1, v0, r, gn, b, a};
    v[2] = (tv_t){x + w, y + h, u1, v1, r, gn, b, a};
    v[3] = (tv_t){x, y, u0, v0, r, gn, b, a};
    v[4] = (tv_t){x + w, y + h, u1, v1, r, gn, b, a};
    v[5] = (tv_t){x, y + h, u0, v1, r, gn, b, a};
    ctx.ntverts += 6;
}

/*
 * Blit each ASCII glyph from the atlas.
 *
 * Atlas layout assumed: left-to-right, top-to-bottom, GLYPH_COLS columns,
 * each cell is font.glyph_w × font.glyph_h pixels.
 * Atlas pixel dimensions: (GLYPH_COLS * gw) × (ceil(range/GLYPH_COLS) * gh).
 */

static void push_text_verts(float x, float y, const char *text, ui_color_t col, ui_font_t font) {
    const int gw = font.glyph_w;
    const int gh = font.glyph_h;
    const int cols = font.atlas_cols;
    const int rows = font.atlas_rows;
    const float inv_cols = 1.0f / (float)cols;
    const float inv_rows = 1.0f / (float)rows;
    const int max_slot = cols * rows;
    float cx = x;

    for (const char *p = text; *p; ++p) {
        int slot = (unsigned char)*p - 0x20;
        if (slot < 0 || slot >= max_slot) {
            cx += gw;
            continue;
        }

        float u0 = (float)(slot % cols) * inv_cols;
        float v0 = (float)(slot / cols) * inv_rows;
        float u1 = u0 + inv_cols;
        float v1 = v0 + inv_rows;

        push_tv_quad(cx, y, (float)gw, (float)gh, u0, v0, u1, v1, col);
        cx += gw;
    }
}

/* =========================================================
 * GPU: per-frame flush
 *
 * Pass 1 — convert every deferred command to vertices in CPU scratch.
 * Pass 2 — upload non-empty vertex arrays via a copy pass.
 * Pass 3 — open a render pass and replay commands with pipeline switches.
 * ========================================================= */

static void gpu_flush(void) {
    ctx.ncverts = 0;
    ctx.ntverts = 0;

    /* ---------- Pass 1: build vertices ---------- */
    for (Uint32 i = 0; i < ctx.ncmds; ++i) {
        draw_cmd_t *c = &ctx.cmds[i];
        ctx.cv_off[i] = ctx.ncverts;
        ctx.tv_off[i] = ctx.ntverts;

        switch (c->type) {
        case CMD_RECT:
            push_cv_quad(c->rect.x, c->rect.y, c->rect.w, c->rect.h, c->rect.col);
            break;
        case CMD_LINE:
            push_cv_line(c->line.x1, c->line.y1, c->line.x2, c->line.y2, c->line.thick,
                         c->line.col);
            break;
        case CMD_TEXT:
            push_text_verts(c->text.x, c->text.y, ctx.spool + c->text.soff, c->text.col,
                            c->text.font);
            break;
        case CMD_IMAGE:
            push_tv_quad(c->image.x, c->image.y, c->image.w, c->image.h, 0.0f, 0.0f, 1.0f, 1.0f,
                         c->image.tint);
            break;
        }

        ctx.cv_cnt[i] = ctx.ncverts - ctx.cv_off[i];
        ctx.tv_cnt[i] = ctx.ntverts - ctx.tv_off[i];
    }

    /* ---------- Pass 2: upload (copy pass) ---------- */
    const bool has_cv = ctx.ncverts > 0;
    const bool has_tv = ctx.ntverts > 0;

    if (has_cv || has_tv) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(ctx.cmd);

        if (has_cv) {
            const Uint32 bytes = ctx.ncverts * (Uint32)sizeof(cv_t);
            void *ptr = SDL_MapGPUTransferBuffer(ctx.gpu, ctx.ctbuf, false);
            memcpy(ptr, ctx.cverts, bytes);
            SDL_UnmapGPUTransferBuffer(ctx.gpu, ctx.ctbuf);
            SDL_UploadToGPUBuffer(
                cp, &(SDL_GPUTransferBufferLocation){.transfer_buffer = ctx.ctbuf, .offset = 0},
                &(SDL_GPUBufferRegion){.buffer = ctx.cvbuf, .offset = 0, .size = bytes}, false);
        }

        if (has_tv) {
            const Uint32 bytes = ctx.ntverts * (Uint32)sizeof(tv_t);
            void *ptr = SDL_MapGPUTransferBuffer(ctx.gpu, ctx.ttbuf, false);
            memcpy(ptr, ctx.tverts, bytes);
            SDL_UnmapGPUTransferBuffer(ctx.gpu, ctx.ttbuf);
            SDL_UploadToGPUBuffer(
                cp, &(SDL_GPUTransferBufferLocation){.transfer_buffer = ctx.ttbuf, .offset = 0},
                &(SDL_GPUBufferRegion){.buffer = ctx.tvbuf, .offset = 0, .size = bytes}, false);
        }

        SDL_EndGPUCopyPass(cp);
    }

    /* ---------- Pass 3: render pass ---------- */
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(ctx.cmd,
                                                   &(SDL_GPUColorTargetInfo){
                                                       .texture = ctx.sc,
                                                       .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
                                                       .load_op = SDL_GPU_LOADOP_CLEAR,
                                                       .store_op = SDL_GPU_STOREOP_STORE,
                                                   },
                                                   1, NULL);

    if (!rp) {
        LOGW("BeginGPURenderPass: %s", SDL_GetError());
        return;
    }

    const push_const_t pc = {
        1.0f / (float)ctx.sc_w,
        1.0f / (float)ctx.sc_h,
    };

    SDL_GPUGraphicsPipeline *active = NULL;

    for (Uint32 i = 0; i < ctx.ncmds; ++i) {
        draw_cmd_t *c = &ctx.cmds[i];
        bool tex = (c->type == CMD_TEXT || c->type == CMD_IMAGE);
        SDL_GPUGraphicsPipeline *want = tex ? ctx.tpipe : ctx.cpipe;
        Uint32 voff = tex ? ctx.tv_off[i] : ctx.cv_off[i];
        Uint32 vcnt = tex ? ctx.tv_cnt[i] : ctx.cv_cnt[i];

        if (!vcnt)
            continue;

        if (active != want) {
            SDL_BindGPUGraphicsPipeline(rp, want);
            active = want;
            /*
             * Push the screen->NDC scale once per pipeline bind.
             * pc is the same for the whole frame, so one push per
             * pipeline change is sufficient.
             */
            SDL_PushGPUVertexUniformData(ctx.cmd, 0, &pc, sizeof(pc));
        }

        if (tex) {
            /* Resolve the GPU texture from font.id or image.tex.id. */
            ui_texture_t th =
                (c->type == CMD_IMAGE) ? c->image.tex : (ui_texture_t){c->text.font.id};

            SDL_BindGPUFragmentSamplers(rp, 0,
                                        &(SDL_GPUTextureSamplerBinding){
                                            .texture = as_gpu(th),
                                            .sampler = ctx.sampler,
                                        },
                                        1);

            SDL_BindGPUVertexBuffers(rp, 0,
                                     &(SDL_GPUBufferBinding){
                                         .buffer = ctx.tvbuf,
                                         .offset = voff * (Uint32)sizeof(tv_t),
                                     },
                                     1);
        } else {
            SDL_BindGPUVertexBuffers(rp, 0,
                                     &(SDL_GPUBufferBinding){
                                         .buffer = ctx.cvbuf,
                                         .offset = voff * (Uint32)sizeof(cv_t),
                                     },
                                     1);
        }

        SDL_DrawGPUPrimitives(rp, vcnt, 1, 0, 0);
    }

    SDL_EndGPURenderPass(rp);
}

/* =========================================================
 * SDL_Renderer: per-frame flush
 * ========================================================= */

/*
 * Thick line via SDL_RenderGeometry — same rotated-quad geometry as the
 * GPU path, avoiding the need for a stencil or multi-pixel line hack.
 * SDL3 SDL_Vertex.color is SDL_FColor (floats, not bytes).
 */
static void rdr_thick_line(float x1, float y1, float x2, float y2, float thick, ui_color_t c) {
    float r, gn, b, a;
    unpack(c, &r, &gn, &b, &a);
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f)
        return;
    float nx = -dy / len * (thick * 0.5f), ny = dx / len * (thick * 0.5f);

    SDL_FPoint pts[4] = {
        {x1 + nx, y1 + ny},
        {x1 - nx, y1 - ny},
        {x2 - nx, y2 - ny},
        {x2 + nx, y2 + ny},
    };
    SDL_FColor col = {r, gn, b, a};
    SDL_Vertex verts[6];
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; ++k) {
        verts[k].position = pts[order[k]];
        verts[k].color = col;
        verts[k].tex_coord = (SDL_FPoint){0.0f, 0.0f};
    }
    SDL_RenderGeometry(ctx.rdr, NULL, verts, 6, NULL, 0);
}

static void rdr_flush(void) {
    for (Uint32 i = 0; i < ctx.ncmds; ++i) {
        draw_cmd_t *c = &ctx.cmds[i];

        switch (c->type) {

        case CMD_RECT: {
            float r, gn, b, a;
            unpack(c->rect.col, &r, &gn, &b, &a);
            SDL_SetRenderDrawColor(ctx.rdr, uf(r), uf(gn), uf(b), uf(a));
            SDL_RenderFillRect(ctx.rdr, &(SDL_FRect){c->rect.x, c->rect.y, c->rect.w, c->rect.h});
            break;
        }

        case CMD_LINE:
            if (c->line.thick <= 1.5f) {
                float r, gn, b, a;
                unpack(c->line.col, &r, &gn, &b, &a);
                SDL_SetRenderDrawColor(ctx.rdr, uf(r), uf(gn), uf(b), uf(a));
                SDL_RenderLine(ctx.rdr, c->line.x1, c->line.y1, c->line.x2, c->line.y2);
            } else {
                rdr_thick_line(c->line.x1, c->line.y1, c->line.x2, c->line.y2, c->line.thick,
                               c->line.col);
            }
            break;

        case CMD_TEXT: {
            SDL_Texture *atlas = as_rdr((ui_texture_t){c->text.font.id});
            const int gw = c->text.font.glyph_w;
            const int gh = c->text.font.glyph_h;
            float r, gn, b, a;
            unpack(c->text.col, &r, &gn, &b, &a);
            SDL_SetTextureColorMod(atlas, uf(r), uf(gn), uf(b));
            SDL_SetTextureAlphaMod(atlas, uf(a));

            float cx = c->text.x;
            const char *p = ctx.spool + c->text.soff;
            for (; *p; ++p) {
                int ch = (unsigned char)*p;
                if (ch < GLYPH_FIRST || ch > GLYPH_LAST) {
                    cx += gw;
                    continue;
                }
                int idx = ch - GLYPH_FIRST;
                SDL_FRect src = {
                    (float)((idx % GLYPH_COLS) * gw),
                    (float)((idx / GLYPH_COLS) * gh),
                    (float)gw,
                    (float)gh,
                };
                SDL_FRect dst = {cx, c->text.y, (float)gw, (float)gh};
                SDL_RenderTexture(ctx.rdr, atlas, &src, &dst);
                cx += gw;
            }
            break;
        }

        case CMD_IMAGE: {
            SDL_Texture *tex = as_rdr(c->image.tex);
            float r, gn, b, a;
            unpack(c->image.tint, &r, &gn, &b, &a);
            SDL_SetTextureColorMod(tex, uf(r), uf(gn), uf(b));
            SDL_SetTextureAlphaMod(tex, uf(a));
            SDL_RenderTexture(ctx.rdr, tex, NULL,
                              &(SDL_FRect){c->image.x, c->image.y, c->image.w, c->image.h});
            break;
        }

        } /* switch */
    }
}

/* =========================================================
 * Unified interface — draw calls (push to command list)
 * ========================================================= */

static void sdl3_draw_rect(float x, float y, float w, float h, ui_color_t color) {
    if (ctx.ncmds >= MAX_CMDS)
        return;
    draw_cmd_t *c = &ctx.cmds[ctx.ncmds++];
    *c = (draw_cmd_t){.type = CMD_RECT, .rect = {x, y, w, h, color}};
}

static void sdl3_draw_line(float x1, float y1, float x2, float y2, ui_color_t color,
                           float thickness) {
    if (ctx.ncmds >= MAX_CMDS)
        return;
    draw_cmd_t *c = &ctx.cmds[ctx.ncmds++];
    *c = (draw_cmd_t){.type = CMD_LINE, .line = {x1, y1, x2, y2, thickness, color}};
}

static void sdl3_draw_text(float x, float y, const char *text, ui_color_t color, ui_font_t font) {
    if (ctx.ncmds >= MAX_CMDS)
        return;
    size_t len = strlen(text) + 1;
    if (ctx.spool_used + (Uint32)len > STR_POOL_SIZE)
        return;

    draw_cmd_t *c = &ctx.cmds[ctx.ncmds++];
    c->type = CMD_TEXT;
    c->text.x = x;
    c->text.y = y;
    c->text.col = color;
    c->text.font = font;
    c->text.soff = ctx.spool_used;
    memcpy(ctx.spool + ctx.spool_used, text, len);
    ctx.spool_used += (Uint32)len;
}

static void sdl3_draw_image(float x, float y, float w, float h, ui_texture_t texture,
                            ui_color_t tint) {
    if (ctx.ncmds >= MAX_CMDS)
        return;
    draw_cmd_t *c = &ctx.cmds[ctx.ncmds++];
    *c = (draw_cmd_t){.type = CMD_IMAGE, .image = {x, y, w, h, texture, tint}};
}

/* =========================================================
 * Unified interface — texture management
 * ========================================================= */

/*
 * Convert pixel data to RGBA8 when the source uses fewer channels:
 *   channels == 1  →  grayscale interpreted as alpha mask (white RGB)
 *   channels == 3  →  RGB  padded to RGBA with full alpha
 *   channels == 4  →  passed through unchanged
 *
 * Returns a heap-allocated RGBA buffer (caller must SDL_free), or NULL
 * when channels == 4 (caller uses the original pointer directly).
 */
static Uint8 *to_rgba(const void *pixels, int w, int h, int channels) {
    if (channels == 4)
        return NULL; /* no conversion needed */
    Uint8 *out = SDL_malloc((size_t)(w * h * 4));
    if (!out)
        return NULL;
    const Uint8 *src = pixels;
    Uint8 *dst = out;
    if (channels == 3) {
        for (int i = 0; i < w * h; ++i, src += 3, dst += 4) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 0xFF;
        }
    } else /* channels == 1 */ {
        for (int i = 0; i < w * h; ++i, ++src, dst += 4) {
            dst[0] = dst[1] = dst[2] = 0xFF;
            dst[3] = *src;
        }
    }
    return out;
}

static ui_texture_t sdl3_create_texture(const void *pixels, int w, int h, int channels) {
    Uint8 *tmp = to_rgba(pixels, w, h, channels);
    const void *rgba = tmp ? tmp : pixels;
    ui_texture_t result = {0};

    if (ctx.path == PATH_GPU) {
        SDL_GPUTexture *tex =
            SDL_CreateGPUTexture(ctx.gpu, &(SDL_GPUTextureCreateInfo){
                                              .type = SDL_GPU_TEXTURETYPE_2D,
                                              .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                              .width = (Uint32)w,
                                              .height = (Uint32)h,
                                              .layer_count_or_depth = 1,
                                              .num_levels = 1,
                                          });
        if (!tex) {
            LOGW("CreateGPUTexture: %s", SDL_GetError());
            goto done;
        }

        const Uint32 bytes = (Uint32)(w * h * 4);

        /*
         * Use a temporary transfer buffer for the one-shot texture upload.
         * Persistent transfer buffers (ctbuf/ttbuf) are reserved for
         * per-frame vertex data.
         */
        SDL_GPUTransferBuffer *tb =
            SDL_CreateGPUTransferBuffer(ctx.gpu, &(SDL_GPUTransferBufferCreateInfo){
                                                     .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                     .size = bytes,
                                                 });
        if (!tb) {
            SDL_ReleaseGPUTexture(ctx.gpu, tex);
            LOGW("CreateGPUTransferBuffer (tex): %s", SDL_GetError());
            goto done;
        }

        void *ptr = SDL_MapGPUTransferBuffer(ctx.gpu, tb, false);
        memcpy(ptr, rgba, bytes);
        SDL_UnmapGPUTransferBuffer(ctx.gpu, tb);

        SDL_GPUCommandBuffer *tcmd = SDL_AcquireGPUCommandBuffer(ctx.gpu);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(tcmd);
        SDL_UploadToGPUTexture(cp,
                               &(SDL_GPUTextureTransferInfo){
                                   .transfer_buffer = tb,
                                   .offset = 0,
                                   .pixels_per_row = (Uint32)w,
                                   .rows_per_layer = (Uint32)h,
                               },
                               &(SDL_GPUTextureRegion){
                                   .texture = tex,
                                   .w = (Uint32)w,
                                   .h = (Uint32)h,
                                   .d = 1,
                               },
                               false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(tcmd);
        SDL_ReleaseGPUTransferBuffer(ctx.gpu, tb);

        result = pack_gpu(tex);

    } else {
        SDL_Texture *tex =
            SDL_CreateTexture(ctx.rdr, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) {
            LOGW("CreateTexture: %s", SDL_GetError());
            goto done;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tex, NULL, rgba, w * 4);
        result = pack_rdr(tex);
    }

done:
    SDL_free(tmp);
    return result;
}

static void sdl3_destroy_texture(ui_texture_t t) {
    if (!t.id)
        return;
    if (ctx.path == PATH_GPU)
        SDL_ReleaseGPUTexture(ctx.gpu, as_gpu(t));
    else
        SDL_DestroyTexture(as_rdr(t));
}

/* =========================================================
 * Unified interface — init / begin / end / shutdown
 * ========================================================= */

static bool sdl3_init(int width, int height, const char *title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOGW("SDL_Init: %s", SDL_GetError());
        return false;
    }

    ctx.width = width;
    ctx.height = height;
    ctx.win = SDL_CreateWindow(title, width, height, 0);
    if (!ctx.win) {
        LOGW("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }

    if (gpu_setup()) {
        LOGD("using SDL_GPU path");
        ctx.path = PATH_GPU;

        static uint8_t atlas_px[UI_FONT_BUILTIN_ATLAS_W * UI_FONT_BUILTIN_ATLAS_H];
        ui_font_builtin_render_atlas(atlas_px);

        ctx.builtin_font_tex =
            sdl3_create_texture(atlas_px, UI_FONT_BUILTIN_ATLAS_W, UI_FONT_BUILTIN_ATLAS_H, 1);

        return true;
    }

    LOGW("SDL_GPU unavailable, falling back to SDL_Renderer");
    ctx.rdr = SDL_CreateRenderer(ctx.win, NULL);
    if (!ctx.rdr) {
        LOGW("SDL_CreateRenderer: %s", SDL_GetError());
        SDL_DestroyWindow(ctx.win);
        ctx.win = NULL;
        return false;
    }
    SDL_SetRenderDrawBlendMode(ctx.rdr, SDL_BLENDMODE_BLEND);
    ctx.path = PATH_RDR;
    {
        static uint8_t atlas_px[UI_FONT_BUILTIN_ATLAS_W * UI_FONT_BUILTIN_ATLAS_H];
        ui_font_builtin_render_atlas(atlas_px);
        ctx.builtin_font_tex =
            sdl3_create_texture(atlas_px, UI_FONT_BUILTIN_ATLAS_W, UI_FONT_BUILTIN_ATLAS_H, 1);
    }
    return true;
}

static void sdl3_begin_frame(void) {
    ctx.ncmds = 0;
    ctx.spool_used = 0;

    if (ctx.path == PATH_GPU) {
        ctx.cmd = SDL_AcquireGPUCommandBuffer(ctx.gpu);
        if (!ctx.cmd) {
            LOGW("AcquireGPUCommandBuffer: %s", SDL_GetError());
            return;
        }
        ctx.sc = NULL;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(ctx.cmd, ctx.win, &ctx.sc, &ctx.sc_w,
                                                   &ctx.sc_h)) {
            LOGW("WaitAndAcquireGPUSwapchainTexture: %s", SDL_GetError());
        }
        if (ctx.sc) {
            /* Keep logical size in sync with actual swapchain dimensions. */
            ctx.width = (int)ctx.sc_w;
            ctx.height = (int)ctx.sc_h;
        }
    } else {
        SDL_SetRenderDrawColor(ctx.rdr, 0, 0, 0, 255);
        SDL_RenderClear(ctx.rdr);
    }
}

static void sdl3_end_frame(void) {
    if (ctx.path == PATH_GPU) {
        if (ctx.cmd) {
            if (ctx.sc)
                gpu_flush();
            SDL_SubmitGPUCommandBuffer(ctx.cmd);
            ctx.cmd = NULL;
            ctx.sc = NULL;
        }
    } else {
        rdr_flush();
        SDL_RenderPresent(ctx.rdr);
    }
}

static void sdl3_shutdown(void) {
    if (ctx.path == PATH_GPU) {
        gpu_teardown();
    } else {
        if (ctx.rdr) {
            SDL_DestroyRenderer(ctx.rdr);
            ctx.rdr = NULL;
        }
    }
    if (ctx.win) {
        SDL_DestroyWindow(ctx.win);
        ctx.win = NULL;
    }
    SDL_Quit();
}

static void sdl3_on_resize(int w, int h) {
    ctx.width = w;
    ctx.height = h;
    /*
     * Both SDL_GPU and SDL_Renderer handle swapchain / viewport resize
     * automatically; no explicit action is required here. This hook
     * exists for consumers that need to update projection matrices or
     * layout caches on resize.
     */
}

// expose to callers:
ui_font_t ui_font_builtin(void) {
    return (ui_font_t){
        .id = ctx.builtin_font_tex.id,
        .glyph_w = UI_FONT_BUILTIN_GLYPH_W,
        .glyph_h = UI_FONT_BUILTIN_GLYPH_H,
        .atlas_cols = UI_FONT_BUILTIN_ATLAS_COLS,
        .atlas_rows = UI_FONT_BUILTIN_ATLAS_H / UI_FONT_BUILTIN_GLYPH_H,
    };
}

/* =========================================================
 * Renderer descriptor
 * ========================================================= */

static const ui_renderer_t sdl3_renderer = {
    .name = "sdl3",
    .init = sdl3_init,
    .begin_frame = sdl3_begin_frame,
    .end_frame = sdl3_end_frame,
    .shutdown = sdl3_shutdown,
    .draw_rect = sdl3_draw_rect,
    .draw_line = sdl3_draw_line,
    .draw_text = sdl3_draw_text,
    .draw_image = sdl3_draw_image,
    .create_texture = sdl3_create_texture,
    .destroy_texture = sdl3_destroy_texture,
    .get_buildin_font = ui_font_builtin,
    .on_resize = sdl3_on_resize,
};

/* =========================================================
 * Auto-registration via the REGISTER_CONSTRUCTOR macro
 * ========================================================= */

REGISTER_CONSTRUCTOR(sdl3_renderer_register) { ui_register_renderer(&sdl3_renderer); }
