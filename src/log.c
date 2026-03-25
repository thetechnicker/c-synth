#define _GNU_SOURCE
#include "log.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *log_file = NULL;
static SDL_Mutex *log_mutex = NULL;
static log_level_t global_min_level = LOG_DEBUG;
static bool color_enabled = true;
static bool use_sdl_ticks = true;

/* ANSI colors */
static const char *level_names[] = {"TRACE", "DEBUG", "INFO",
                                    "WARN",  "ERROR", "FATAL"};
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
        fopen_s(&log_file, filename, "a");
        if (!log_file)
            return false;
        /* set line buffering */
        setvbuf(log_file, NULL, _IOLBF, 0);
    }
    if (!log_mutex) {
        log_mutex = SDL_CreateMutex();
        if (!log_mutex) {
            if (log_file) {
                fclose(log_file);
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
            fflush(log_file);
            fclose(log_file);
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
        /* convert ticks to seconds+ms relative to SDL init */
        uint32_t s = ms / 1000;
        uint32_t rem = ms % 1000;
        snprintf(out, outlen, "%u.%03u", s, rem);
    } else {
        time_t t = time(NULL);
        struct tm tm;
        gmtime_s(&tm, &t);
        strftime(out, outlen, "%Y-%m-%dT%H:%M:%SZ", &tm);
    }
}

void log_log(log_level_t level, const char *file, int line, const char *fmt,
             ...) {
    if (level < global_min_level)
        return;
    if (!log_mutex) {
        /* lazy init default mutex if user forgot to call init */
        log_mutex = SDL_CreateMutex();
        if (!log_mutex)
            return;
    }

    SDL_LockMutex(log_mutex);

    /* format message */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* time */
    char ts[64];
    current_time_iso(ts, sizeof(ts));

    /* Console output */
    if (color_enabled) {
        fprintf(stdout, "%s[%s] %s:%d: %s%s\n", level_colors[level],
                level_names[level], file, line, msg, color_reset);
    } else {
        fprintf(stdout, "[%s] %s:%d: %s\n", level_names[level], file, line,
                msg);
    }
    fflush(stdout);

    /* JSONL file output */
    if (log_file) {
        /* keep file entries small; escape JSON string for message */
        /* simple JSON escaping (handles backslash and quotes and control chars)
         */
        size_t len = strlen(msg);
        char *esc = malloc(len * 2 + 1);
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
                    /* control -> \u00XX */
                    int written = sprintf(p, "\\u%04x", c);
                    p += written;
                } else {
                    *p++ = c;
                }
            }
            *p = '\0';

            /* file, line, level, time, message, source file */
            fprintf(log_file,
                    "{\"ts\":\"%s\",\"level\":\"%s\",\"file\":\"%s\",\"line\":%"
                    "d,\"msg\":\"%s\"}\n",
                    ts, level_names[level], file, line, esc);
            fflush(log_file);
            free(esc);
        } else {
            /* fallback: write unescaped message */
            fprintf(log_file,
                    "{\"ts\":\"%s\",\"level\":\"%s\",\"file\":\"%s\",\"line\":%"
                    "d,\"msg\":\"%s\"}\n",
                    ts, level_names[level], file, line, msg);
            fflush(log_file);
        }
    }

    /* If fatal, optionally abort */
    if (level == LOG_FATAL) {
        SDL_UnlockMutex(log_mutex);
        log_shutdown();
        abort();
    }

    SDL_UnlockMutex(log_mutex);
}
