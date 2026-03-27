#ifndef ARGPARSE_H
#define ARGPARSE_H

#include "hashmap.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
// #include <errno.h>

typedef enum {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_CHAR,
    VALUE_STR,
} ValueDataType;

typedef struct {
    bool positional;
    bool allow_short;
    char short_override; // '\0' = auto-derive from name[0]
    ValueDataType type;
    union {
        int i;
        float f;
        char c;
        char *s;
    };
} ValueArg;

typedef struct {
    bool allow_short;
    char short_override;
    bool b;
} FlagArg;

typedef enum {
    ARG_VALUE,
    ARG_FLAG,
    // ARG_COMMAND,
} ArgType;

typedef struct {
    ArgType type;
    const char *name;
    const char *description;
    union {
        FlagArg f;
        ValueArg v;
    };
} ArgDef;

typedef struct {
    const char *app_name;
    const char *usage;
    const char *description;
    ArgDef *args;
    HashMap *fast_look;
    size_t len;
    size_t cap;
} ArgParse;

typedef struct {
    const char *name;
    ArgType type; // ARG_FLAG or ARG_VALUE
    union {
        bool b; // when type == ARG_FLAG
        struct {
            ValueDataType dtype;
            union {
                int i;
                float f;
                char c;
                char *s;
            };
        }; // when type == ARG_VALUE
    };
} ArgParseResult;

int argparse_init(ArgParse *ap, const char *app_name, const char *usage, const char *description,
                  size_t capacity);

void argparse_free(ArgParse *ap);

int argparse_add_flag(ArgParse *ap, const char *name, const char *description, bool allow_short,
                      const char short_override, bool default_v);

#define argparse_add_value(ap, name, description, possitional, allow_short, short_override, x)     \
    _Generic((x),                                                                                  \
        int: argparse_add_value_i,                                                                 \
        float: argparse_add_value_f,                                                               \
        char: argparse_add_value_c,                                                                \
        char *: argparse_add_value_s,                                                              \
        ValueArg: argparse_add_value_v)(ap, name, description, possitional, allow_short,           \
                                        short_override, x)

int argparse_add_value_v(ArgParse *ap, const char *name, const char *description,
                         ValueArg default_v);
int argparse_add_value_i(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, int i);
int argparse_add_value_f(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, float f);
int argparse_add_value_c(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, char c);
int argparse_add_value_s(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, char *s);

SDL_AppResult argparse_parse(int argc, char **argv, const ArgParse *ap, HashMap *result);

#endif /* ARGPARSE_H */
