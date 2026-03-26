#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* If you use SDL_AppResult, include SDL headers in the translation unit that
 * calls argparse_parse (this header only references the type). */
#include <SDL3/SDL.h> /* optional here; caller can include instead */

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
        float *f;
        int *i;
        char *c;
        char **s;
        bool *b;
    } val;
} Value;

typedef struct {
    char *long_name;
    bool has_abrv;
    char overwrite_abrv;
    ValueType type;
    Value out;
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
    Value out;
    bool required;
} PositionalArg;

typedef struct {
    char *long_name;
    bool has_abrv;
    char overwrite_abrv;
} FlagEntry;

typedef struct {
    char *list_name;
    FlagEntry flags[64];
    uint8_t count;
    uint64_t *out;
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
    const char *description; /* human-readable help text; may be NULL */
    union {
        KeywordArg keyword;
        Flag flag;
        PositionalArg positional;
        FlagList flag_list;
    };
} ArgDef;

typedef struct {
    const char *app_name;
    const char *usage;
    const char *description;
    ArgDef *args;
    size_t len;
    size_t cap;
} ArgParse;

/* Error reporting model
 *
 * All builder/parser functions now return a POSIX-style error code:
 *   0        : success
 *   ENOMEM   : out of memory
 *   EINVAL   : invalid argument passed to builder
 *   EOVERFLOW: array/full / too many flags in list
 *   ENOENT   : not found (e.g., flaglist label not found)
 *   EFAULT   : type mismatch or bad pointer (best-effort)
 *   other errno values as appropriate
 *
 * Functions do not fill an out-parameter error struct; callers can inspect
 * the returned errno-style code and use strerror() for text.
 */

/* Builder/pipeline API
 *
 * General usage model:
 *   - Allocate an array ArgDef defs[N]; zero-init it.
 *   - Call add_* with &defs[idx], or for add_flaglist_flag supply defs and n to
 *     search for the FlagList by its long_name.
 *   - Builders write into the provided ArgDef slot (overwriting it).
 *
 * NOTE: many parameters are pointer types that must remain valid (e.g. value
 *       pointers and long_name strings) for the lifetime of the parser run.
 *
 * The `description` parameter in each builder is a human-readable help string
 * displayed by argparse_print_help. Pass NULL to omit.
 */

int argparse_init(ArgParse *ap, const char *app_name, const char *usage,
                  const char *description, size_t capacity);

void argparse_free(ArgParse *ap);

/* initialize/overwrite a slot as a keyword argument:
 *  - label: long option name (null-terminated)
 *  - has_abrv, overwrite_abrv: as before
 *  - out: Value* (non-NULL recommended) — parser writes only on explicit match
 *  - required: whether presence is mandatory
 *  - description: help text shown by --help (may be NULL)
 */
int add_kw_argument(ArgParse *ap, const char *label, bool has_abrv,
                    char overwrite_abrv, Value out, bool required,
                    const char *description);

/* initialize/overwrite a slot as a flag argument:
 *  - out: bool* (non-NULL recommended)
 *  - description: help text shown by --help (may be NULL)
 */
int add_flag(ArgParse *ap, const char *label, bool has_abrv,
             char overwrite_abrv, bool *out, bool required,
             const char *description);

/* initialize/overwrite a slot as a positional argument:
 *  - out: Value* (non-NULL recommended)
 *  - description: help text shown by --help (may be NULL)
 */
int add_positional_argument(ArgParse *ap, Value out, bool required,
                            const char *description);

/* initialize/overwrite a slot as a flaglist:
 *  - out: uint32_t* bitmask (non-NULL recommended)
 *  - count must be set to 0 by builder; entries are added via add_flaglist_flag
 *  - description: help text shown by --help (may be NULL)
 */
int add_flaglist(ArgParse *ap, const char *label, uint64_t *out, bool required,
                 const char *description);

/* add a flag entry into an existing FlagList in an array of ArgDef:
 *  - ap: ArgParse whose args[] will be searched for the FlagList by its
 * long_name
 *  - flag_label: long_name for the new flag entry
 *  - list_label: label of the FlagList to which this entry will be added
 *  - has_abrv, overwrite_abrv: as before
 *
 * Returns ENOENT if the FlagList named list_label is not found.
 * Returns EOVERFLOW if the FlagList already has 32 flags.
 *
 * Note: FlagEntry has no description field; per-entry help is not supported.
 */
int add_flaglist_flag(ArgParse *ap, const char *flag_label,
                      const char *list_label, bool has_abrv,
                      char overwrite_abrv);

/* Parser: no longer exits. Returns an SDL_AppResult to indicate what the
 * application should do:
 *   - SDL_APP_CONTINUE (or SDL_APPRETURN_CONTINUE depending on SDL version)
 *       : parsing succeeded and the app should continue running
 *   - SDL_APP_EXIT (application should exit with code 0)
 *   - SDL_APP_EXIT_FAILURE (application should exit with code 1)
 *
 * The function will return non-zero (a POSIX errno) on failure; callers should
 * use the returned errno to decide messaging/termination. The function will
 * not call exit() or print to stderr/stdout.
 *
 * Include <SDL.h> in the translation unit that calls argparse_parse so that
 * SDL_AppResult is defined appropriately for your SDL version.
 */
SDL_AppResult argparse_parse(int argc, char **argv, const ArgParse *ap);

#endif /* ARGPARSE_H */
