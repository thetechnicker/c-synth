#define _GNU_SOURCE
#include "log.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static SDL_IOStream *log_file = NULL;
static SDL_Mutex *log_mutex = NULL;
static log_level_t global_min_level = LOG_DEBUG;
static bool color_enabled = true;
static bool use_sdl_ticks = true;

/* ANSI colors */
static const char *level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
static const char *level_colors[] = {
    "\x1b[90m", /* TRACE - bright black */
    "\x1b[36m", /* DEBUG - cyan */
    "\x1b[32m", /* INFO  - green */
    "\x1b[33m", /* WARN  - yellow */
    "\x1b[31m", /* ERROR - red */
    "\x1b[35m"  /* FATAL - magenta */
};
static const char *color_reset = "\x1b[0m";

bool log_init(const char *filename, log_level_t min_level) {
    global_min_level = min_level;
    if (filename) {
        /* SDL_IOFromFile is cross-platform — no fopen_s vs fopen needed. */
        log_file = SDL_IOFromFile(filename, "a");
        if (!log_file)
            return false;
    }
    if (!log_mutex) {
        log_mutex = SDL_CreateMutex();
        if (!log_mutex) {
            if (log_file) {
                SDL_CloseIO(log_file);
                log_file = NULL;
            }
            return false;
        }
    }
    return true;
}

void log_shutdown(void) {
    if (log_mutex) {
        SDL_LockMutex(log_mutex);
        if (log_file) {
            SDL_FlushIO(log_file);
            SDL_CloseIO(log_file);
            log_file = NULL;
        }
        SDL_UnlockMutex(log_mutex);
        SDL_DestroyMutex(log_mutex);
        log_mutex = NULL;
    }
}

void log_set_color(bool enabled) { color_enabled = enabled; }
void log_set_use_sdl_ticks(bool enabled) { use_sdl_ticks = enabled; }

static void current_time_iso(char *out, size_t outlen) {
    if (use_sdl_ticks) {
        uint32_t ms = SDL_GetTicks();
        uint32_t s = ms / 1000;
        uint32_t rem = ms % 1000;
        SDL_snprintf(out, outlen, "%u.%03u", s, rem);
    } else {
        /* SDL_GetCurrentTime + SDL_TimeToDateTime replace gmtime_r/gmtime_s. */
        SDL_Time ticks;
        SDL_DateTime dt;
        if (SDL_GetCurrentTime(&ticks) && SDL_TimeToDateTime(ticks, &dt, false)) {
            SDL_snprintf(out, outlen, "%04d-%02d-%02dT%02d:%02d:%02dZ", dt.year, dt.month, dt.day,
                         dt.hour, dt.minute, dt.second);
        } else {
            SDL_strlcpy(out, "unknown", outlen);
        }
    }
}

void log_log(log_level_t level, const char *file, int line, const char *func, const char *fmt,
             ...) {
    if (level < global_min_level)
        return;
    if (!log_mutex) {
        /* lazy init default mutex if user forgot to call log_init */
        log_mutex = SDL_CreateMutex();
        if (!log_mutex)
            return;
    }

    SDL_LockMutex(log_mutex);

    /* format message */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* time */
    char ts[64];
    current_time_iso(ts, sizeof(ts));

    /* Console output */
    if (color_enabled) {
        SDL_Log("%s[%s] %s:%d | [%s]: %s%s", level_colors[level], level_names[level], file, line,
                func, msg, color_reset);
    } else {
        SDL_Log("[%s] %s:%d | [%s]: %s", level_names[level], file, line, func, msg);
    }

    /* JSONL file output */
    if (log_file) {
        size_t len = strlen(msg);
        char *esc = SDL_malloc(len * 2 + 1);
        if (esc) {
            char *p = esc;
            for (size_t i = 0; i < len; ++i) {
                unsigned char c = (unsigned char)msg[i];
                if (c == '\\') {
                    *p++ = '\\';
                    *p++ = '\\';
                } else if (c == '\"') {
                    *p++ = '\\';
                    *p++ = '\"';
                } else if (c == '\n') {
                    *p++ = '\\';
                    *p++ = 'n';
                } else if (c == '\r') {
                    *p++ = '\\';
                    *p++ = 'r';
                } else if (c == '\t') {
                    *p++ = '\\';
                    *p++ = 't';
                } else if (c < 0x20) {
                    int written = SDL_snprintf(p, 7, "\\u%04x", c);
                    p += written;
                } else {
                    *p++ = c;
                }
            }
            *p = '\0';

            SDL_IOprintf(
                log_file,
                "{\"ts\":\"%s\",\"level\":\"%s\",\"file\":\"%s\",\"line\":%d,\"msg\":\"%s\"}\n", ts,
                level_names[level], file, line, esc);
            SDL_FlushIO(log_file);
            SDL_free(esc);
        } else {
            SDL_IOprintf(
                log_file,
                "{\"ts\":\"%s\",\"level\":\"%s\",\"file\":\"%s\",\"line\":%d,\"msg\":\"%s\"}\n", ts,
                level_names[level], file, line, msg);
            SDL_FlushIO(log_file);
        }
    }

    if (level == LOG_FATAL) {
        SDL_UnlockMutex(log_mutex);
        log_shutdown();
        SDL_Quit();
        abort();
    }

    SDL_UnlockMutex(log_mutex);
}
