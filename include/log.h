#ifndef LOG_H
#define LOG_H

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/* Initialize logger.
 * - filename: path to JSONL file (created/append). Pass NULL to disable file
 * output.
 * - min_level: minimum level to print/store.
 * Returns true on success.
 */
bool log_init(const char *filename, log_level_t min_level);

/* Shutdown logger and flush file. */
void log_shutdown(void);

/* Set whether console output uses ANSI colors (default true). */
void log_set_color(bool enabled);

/* Set whether include SDL ticks as timestamp in logs (default true). */
void log_set_use_sdl_ticks(bool enabled);

/* Log functions */
void log_log(log_level_t level, const char *file, int line, const char *fmt,
             ...);

/* Convenience macros capturing file/line */
#define LOGT(...) log_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...) log_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) log_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) log_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGF(...) log_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif /* LOG_H */
