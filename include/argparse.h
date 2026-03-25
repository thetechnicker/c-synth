#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* If you use SDL_AppResult, include SDL headers in the translation unit that
 * calls argparse_parse (this header only references the type). */
#include <SDL3/SDL.h>/* optional here; caller can include instead */

/* Existing Value / ArgDef types (unchanged) --------------------------------*/

typedef enum {
    VAL_FLOAT,
    VAL_INT,
    VAL_CHAR,
    VAL_STRING, /* points into argv[] — valid for the program's lifetime */
    VAL_BOOL,
} ValueType;

typedef struct {
    ValueType type;
    union {
        float f;
        int i;
        char c;
        char *s;
        bool b;
    } val;
} Value;

typedef struct {
    char *long_name;
    bool has_abrv;
    char overwrite_abrv;
    ValueType type;
    Value *out;
    bool required;
} KeywordArg;

typedef struct {
    char *long_name;
    bool has_abrv;
    char overwrite_abrv;
    bool *out;
    bool required;
} Flag;

typedef struct {
    ValueType type;
    Value *out;
    bool required;
} PositionalArg;

typedef struct {
    char *long_name;
    bool has_abrv;
    char overwrite_abrv;
} FlagEntry;

typedef struct {
    char *list_name;
    FlagEntry flags[32];
    uint32_t count;
    uint32_t *out;
    bool required;
} FlagList;

typedef enum {
    ARG_KEYWORD,
    ARG_FLAG,
    ARG_POSITIONAL,
    ARG_FLAG_LIST,
} ArgType;

typedef struct {
    ArgType type;
    union {
        KeywordArg keyword;
        Flag flag;
        PositionalArg positional;
        FlagList flag_list;
    };
} ArgDef;

typedef struct {
    ArgDef *args;
    size_t len;
    size_t cap;
} ArgParse;

/* New error type returned by builder functions and by parser ----------------*/

typedef enum {
    APERR_OK = 0,
    APERR_OOM,
    APERR_INVALID_ARG,   /* invalid parameter passed to builder */
    APERR_OVERFLOW,      /* array full / too many flags in list */
    APERR_NOT_FOUND,     /* flaglist label not found (for add_flaglist_flag) */
    APERR_TYPE_MISMATCH, /* caller-provided out pointer type mismatch
                            (best-effort) */
    APERR_OTHER,
} ArgParseErrCode;

typedef struct {
    ArgParseErrCode code;
    const char *msg;  /* brief message (string literal or owned by caller) */
    const char *file; /* __FILE__ at point of creation */
    int line;         /* __LINE__ at point of creation */
} ArgParseError;

/* pointer variant */
#define ARGPARSE_ERROR_PRINT(errp)                                             \
    do {                                                                       \
        const ArgParseError *e_ = (errp);                                      \
        fprintf(stderr,                                                        \
                "ArgParseError: code=%d, msg=\"%s\", file=\"%s\", line=%d\n",  \
                (int)(e_ ? e_->code : 0),                                      \
                (e_ && e_->msg) ? e_->msg : "(null)",                          \
                (e_ && e_->file) ? e_->file : "(null)", (e_ ? e_->line : 0));  \
    } while (0)

/* value variant (takes an ArgParseError value) */
#define ARGPARSE_ERROR_PRINT_VAL(ev)                                           \
    do {                                                                       \
        const ArgParseError *e_ = &(ev);                                       \
        fprintf(stderr,                                                        \
                "ArgParseError: code=%d, msg=\"%s\", file=\"%s\", line=%d\n",  \
                (int)e_->code, e_->msg ? e_->msg : "(null)",                   \
                e_->file ? e_->file : "(null)", e_->line);                     \
    } while (0)

/* Builder/pipeline API
 *
 * General usage model:
 *   - Allocate an array ArgDef defs[N]; zero-init it.
 *   - Call add_* with &defs[idx], or for add_flaglist_flag supply defs and n to
 *     search for the FlagList by its long_name.
 *   - Builders write into the provided ArgDef slot (overwriting it).
 *
 * All builder functions return ArgParseErrCode; if non-APERR_OK and `err` is
 * non-NULL it will be filled with diagnostic info (msg/file/line).
 *
 * NOTE: many parameters are pointer types that must remain valid (e.g. value
 *       pointers and long_name strings) for the lifetime of the parser run.
 */

ArgParseErrCode argparse_init(ArgParse *ap, size_t capacity,
                              ArgParseError *err);

void argparse_free(ArgParse *ap);

/* initialize/overwrite a slot as a keyword argument:
 *  - label: long option name (null-terminated)
 *  - has_abrv, overwrite_abrv: as before
 *  - out: Value* (non-NULL recommended) — parser writes only on explicit match
 *  - required: whether presence is mandatory
 */
ArgParseErrCode add_kw_argument(ArgParse *ap, const char *label, bool has_abrv,
                                char overwrite_abrv, ValueType type, Value *out,
                                bool required,
                                ArgParseError *err); /* nullable */

/* initialize/overwrite a slot as a flag argument:
 *  - out: bool* (non-NULL recommended)
 */
ArgParseErrCode add_flag(ArgParse *ap, const char *label, bool has_abrv,
                         char overwrite_abrv, bool *out, bool required,
                         ArgParseError *err);

/* initialize/overwrite a slot as a positional argument:
 *  - out: Value* (non-NULL recommended)
 */
ArgParseErrCode add_positional_argument(ArgParse *ap, ValueType type,
                                        Value *out, bool required,
                                        ArgParseError *err);

/* initialize/overwrite a slot as a flaglist:
 *  - out: uint32_t* bitmask (non-NULL recommended)
 *  - count must be set to 0 by builder; entries are added via add_flaglist_flag
 */
ArgParseErrCode add_flaglist(ArgParse *ap, const char *label, uint32_t *out,
                             bool required, ArgParseError *err);

/* add a flag entry into an existing FlagList in an array of ArgDef:
 *  - defs/ndefs: array + count to search for the FlagList by its long_name
 *  - flag_label: long_name for the new flag entry
 *  - list_label: label of the FlagList to which this entry will be added
 *  - has_abrv, overwrite_abrv: as before
 *
 * If the FlagList named list_label is not found, returns APERR_NOT_FOUND.
 * If the FlagList already has 32 flags, returns APERR_OVERFLOW.
 */
ArgParseErrCode add_flaglist_flag(ArgParse *ap, const char *flag_label,
                                  const char *list_label, bool has_abrv,
                                  char overwrite_abrv, ArgParseError *err);

/* Parser: no longer exits. Returns an SDL_AppResult to indicate what the
 * application should do:
 *   - SDL_APP_CONTINUE (or SDL_APPRETURN_CONTINUE depending on SDL version)
 *       : parsing succeeded and the app should continue running
 *   - SDL_APP_EXIT (application should exit with code 0)
 *   - SDL_APP_EXIT_FAILURE (application should exit with code 1)
 *
 * The function will populate `err` on failure (nullable). It will not call
 * exit() or print to stderr/stdout; the caller should handle messaging and
 * program termination according to the returned SDL_AppResult and err.
 *
 * The exact SDL_AppResult enum is provided by SDL; include <SDL.h> where
 * you call argparse_parse. If your SDL version provides different symbols,
 * adapt the mapping in the implementation.
 */
SDL_AppResult argparse_parse(int argc, char **argv, const ArgParse *ap,
                             ArgParseError *err);

#endif /* ARGPARSE_H */
