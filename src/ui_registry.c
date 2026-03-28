#include "ui.h"
#include "log.h"
#include <string.h>
#include <stdbool.h>

#define UI_MAX_RENDERERS 16

static const ui_renderer_t *g_registered_renderers[UI_MAX_RENDERERS];
static size_t g_renderer_count = 0;

bool ui_register_renderer(const ui_renderer_t *r) {
    if (!r || !r->name) {
        LOGW("ui_register_renderer: null renderer or name");
        return false;
    }

    for (size_t i = 0; i < g_renderer_count; ++i) {
        if (strcmp(g_registered_renderers[i]->name, r->name) == 0) {
            LOGW("ui_register_renderer: renderer '%s' already registered", r->name);
            return false; /* duplicate */
        }
    }

    if (g_renderer_count >= UI_MAX_RENDERERS) {
        LOGE("ui_register_renderer: registry full (max=%d), cannot register '%s'", UI_MAX_RENDERERS, r->name);
        return false;
    }

    g_registered_renderers[g_renderer_count++] = r;
    LOGI("ui_register_renderer: registered renderer '%s' (count=%zu)", r->name, g_renderer_count);
    return true;
}

const ui_renderer_t *ui_get_renderer(const char *name) {
    if (!name) {
        LOGW("ui_get_renderer: null name");
        return NULL;
    }
    for (size_t i = 0; i < g_renderer_count; ++i) {
        if (strcmp(g_registered_renderers[i]->name, name) == 0) {
            LOGD("ui_get_renderer: found renderer '%s'", name);
            return g_registered_renderers[i];
        }
    }
    LOGW("ui_get_renderer: renderer '%s' not found", name);
    return NULL;
}

const ui_renderer_t *ui_get_default_renderer(void) {
    if (g_renderer_count == 0) {
        LOGW("ui_get_default_renderer: no renderers registered");
        return NULL;
    }
    LOGD("ui_get_default_renderer: returning '%s'", g_registered_renderers[0]->name);
    return g_registered_renderers[0];
}
