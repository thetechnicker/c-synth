// argparse.c
#include "argparse.h"
#include "hashmap.h"
#include "log.h"
#include "version.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static char effective_abrv(bool has_abrv, char overwrite_abrv, const char *long_name) {
    if (!has_abrv)
        return '\0';
    if (overwrite_abrv)
        return overwrite_abrv;
    return (char)tolower((unsigned char)long_name[0]);
}

static int add_element(ArgParse *ap, ArgDef **out_slot) {
    if (!ap || !out_slot)
        return EINVAL;

    if (ap->len >= ap->cap) {
        size_t new_cap = ap->cap ? ap->cap * 2 : 4;
        ArgDef *new_args = realloc(ap->args, new_cap * sizeof *new_args);
        if (!new_args)
            return ENOMEM;

        memset((char *)new_args + ap->cap * sizeof *new_args, 0,
               (new_cap - ap->cap) * sizeof *new_args);

        ap->args = new_args;
        ap->cap = new_cap;
    }

    *out_slot = &ap->args[ap->len++];
    memset(*out_slot, 0, sizeof **out_slot);
    return 0;
}

/* Find ArgDef by exact long name. */
static ArgDef *find_by_long(const ArgParse *ap, const char *name) {
    for (size_t i = 0; i < ap->len; i++)
        if (ap->args[i].name && strcmp(ap->args[i].name, name) == 0)
            return &ap->args[i];
    return NULL;
}

/* Find ArgDef by its effective short character. */
static ArgDef *find_by_short(const ArgParse *ap, char c) {
    for (size_t i = 0; i < ap->len; i++) {
        ArgDef *d = &ap->args[i];
        const char *n = d->name ? d->name : "";
        char abrv = '\0';
        if (d->type == ARG_FLAG)
            abrv = effective_abrv(d->f.allow_short, d->f.short_override, n);
        else if (d->type == ARG_VALUE && !d->v.positional)
            abrv = effective_abrv(d->v.allow_short, d->v.short_override, n);
        if (abrv && abrv == c)
            return d;
    }
    return NULL;
}

static const char *valuetype_name(ValueDataType t) {
    switch (t) {
    case VALUE_FLOAT:
        return "float";
    case VALUE_INT:
        return "int";
    case VALUE_CHAR:
        return "char";
    case VALUE_STR:
        return "string";
    }
    return "?";
}

/*
 * Format a value default as "type=repr" into buf.
 * Examples: "int=42", "float=1.5", "char='a'", "string=\"foo\""
 */
static void format_default(char *buf, size_t sz, const ValueArg *v) {
    switch (v->type) {
    case VALUE_INT:
        snprintf(buf, sz, "%s=%d", valuetype_name(v->type), v->i);
        break;
    case VALUE_FLOAT:
        snprintf(buf, sz, "%s=%g", valuetype_name(v->type), (double)v->f);
        break;
    case VALUE_CHAR:
        snprintf(buf, sz, "%s='%c'", valuetype_name(v->type), v->c);
        break;
    case VALUE_STR:
        snprintf(buf, sz, "%s=\"%s\"", valuetype_name(v->type), v->s ? v->s : "");
        break;
    }
}

/*
 * Parse a raw string token into the value fields of an ArgParseResult.
 * Returns true on success, false on conversion error.
 * For VALUE_STR the pointer borrows the argv string (valid for program lifetime).
 */
static bool parse_value_token(const char *tok, ValueDataType type, ArgParseResult *out) {
    out->dtype = type;
    switch (type) {
    case VALUE_INT: {
        char *end;
        errno = 0;
        long v = strtol(tok, &end, 10);
        if (*end != '\0' || errno != 0)
            return false;
        out->i = (int)v;
        return true;
    }
    case VALUE_FLOAT: {
        char *end;
        errno = 0;
        out->f = strtof(tok, &end);
        if (*end != '\0' || errno != 0)
            return false;
        return true;
    }
    case VALUE_CHAR:
        if (strlen(tok) != 1)
            return false;
        out->c = tok[0];
        return true;
    case VALUE_STR:
        out->s = (char *)tok;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Public API — init / free / add_*                                    */
/* ------------------------------------------------------------------ */

int argparse_init(ArgParse *ap, const char *app_name, const char *usage, const char *description,
                  size_t capacity) {
    if (!ap)
        return EINVAL;
    if (capacity == 0)
        capacity = 4;

    ArgDef *slots = malloc(sizeof *slots * capacity);
    if (!slots)
        return ENOMEM;

    memset(slots, 0, sizeof *slots * capacity);
    ap->app_name = app_name;
    ap->usage = usage;
    ap->description = description;
    ap->args = slots;
    ap->len = 0;
    ap->cap = capacity;
    ap->fast_look = hashmap_new(0);

    return 0;
}

void argparse_free(ArgParse *ap) {
    if (!ap)
        return;
    free(ap->args);
    ap->args = NULL;
    ap->len = 0;
    ap->cap = 0;
}

int argparse_add_flag(ArgParse *ap, const char *name, const char *description, bool allow_short,
                      char short_override, bool default_v) {
    ArgDef *arg;
    int ret = add_element(ap, &arg);
    if (ret) {
        LOGE("Error: %s", strerror(ret));
        return 1;
    }

    hashmap_insert(ap->fast_look, name, arg);

    arg->name = name;
    arg->description = description;
    arg->type = ARG_FLAG;
    arg->f.allow_short = allow_short;
    arg->f.short_override = short_override;
    arg->f.b = default_v;
    return 0;
}

int argparse_add_value_v(ArgParse *ap, const char *name, const char *description,
                         ValueArg default_v) {
    ArgDef *arg;
    int ret = add_element(ap, &arg);
    if (ret) {
        LOGE("Error: %s", strerror(ret));
        return 1;
    }

    hashmap_insert(ap->fast_look, name, arg);

    arg->name = name;
    arg->description = description;
    arg->type = ARG_VALUE;
    arg->v = default_v;
    return 0;
}

int argparse_add_value_i(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, int i) {
    ValueArg v = {.positional = possitional,
                  .allow_short = allow_short,
                  .short_override = short_override,
                  .type = VALUE_INT,
                  .i = i};
    return argparse_add_value_v(ap, name, description, v);
}
int argparse_add_value_f(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, float f) {
    ValueArg v = {.positional = possitional,
                  .allow_short = allow_short,
                  .short_override = short_override,
                  .type = VALUE_FLOAT,
                  .f = f};
    return argparse_add_value_v(ap, name, description, v);
}
int argparse_add_value_c(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, char c) {
    ValueArg v = {.positional = possitional,
                  .allow_short = allow_short,
                  .short_override = short_override,
                  .type = VALUE_CHAR,
                  .c = c};
    return argparse_add_value_v(ap, name, description, v);
}
int argparse_add_value_s(ArgParse *ap, const char *name, const char *description, bool possitional,
                         bool allow_short, const char short_override, char *s) {
    ValueArg v = {.positional = possitional,
                  .allow_short = allow_short,
                  .short_override = short_override,
                  .type = VALUE_STR,
                  .s = s};
    return argparse_add_value_v(ap, name, description, v);
}

/* ------------------------------------------------------------------ */
/* argparse_print_help — improved                                      */
/* ------------------------------------------------------------------ */

/*
 * Column at which the description text starts.
 * Left column content wider than this gets only 2 spaces of padding.
 */
#define HELP_DESC_COL 38

/*
 * Print one option row:
 *   "  <left> [<tag>]    [required]  <description>"
 *
 * left : e.g. "--output, -o"   or  "input-file"
 * tag  : e.g. "string=\"out.txt\""  — shown as <tag>; pass NULL to omit
 */
static void print_option_line(FILE *out, const char *left, const char *tag, bool required,
                              const char *description) {
    char col[160];
    int n = snprintf(col, sizeof col, "  %s", left);
    if (tag && tag[0])
        n += snprintf(col + n, sizeof col - (size_t)n, " <%s>", tag);

    fprintf(out, "%s", col);

    int pad = HELP_DESC_COL - n;
    if (pad < 2)
        pad = 2;
    fprintf(out, "%*s", pad, "");

    if (required)
        fprintf(out, "[required]");
    if (description && description[0]) {
        if (required)
            fprintf(out, "  ");
        fprintf(out, "%s", description);
    }
    fprintf(out, "\n");
}

void argparse_print_help(FILE *out, const char *progname, const ArgParse *ap) {
    if (!out || !ap)
        return;

    const char *pname = progname ? progname : (ap->app_name ? ap->app_name : PROJECT_NAME);

    /* ----------------------------------------------------------------
     * Usage line
     * Positionals shown bare: <name>
     * Named flags:  [-x|--name]
     * Named values: [-x|--name <type=default>]
     * ---------------------------------------------------------------- */
    fprintf(out, "Usage: %s", pname);

    if (ap->usage && ap->usage[0]) {
        fprintf(out, " %s", ap->usage);
    } else {
        for (size_t i = 0; i < ap->len; i++) {
            ArgDef *d = &ap->args[i];
            const char *n = d->name ? d->name : "";
            char dtag[80];

            switch (d->type) {
            case ARG_FLAG: {
                char abrv = effective_abrv(d->f.allow_short, d->f.short_override, n);
                if (abrv)
                    fprintf(out, " [-%c|--%s]", abrv, n);
                else
                    fprintf(out, " [--%s]", n);
                break;
            }
            case ARG_VALUE:
                if (d->v.positional) {
                    fprintf(out, " <%s>", n[0] ? n : valuetype_name(d->v.type));
                } else {
                    format_default(dtag, sizeof dtag, &d->v);
                    char abrv = effective_abrv(d->v.allow_short, d->v.short_override, n);
                    if (abrv)
                        fprintf(out, " [-%c|--%s <%s>]", abrv, n, dtag);
                    else
                        fprintf(out, " [--%s <%s>]", n, dtag);
                }
                break;
            }
        }
    }
    fprintf(out, "\n");

    /* Description */
    if (ap->description && ap->description[0])
        fprintf(out, "\n%s\n", ap->description);

    /* ----------------------------------------------------------------
     * Positional arguments section
     * ---------------------------------------------------------------- */
    bool has_pos = false;
    for (size_t i = 0; i < ap->len; i++)
        if (ap->args[i].type == ARG_VALUE && ap->args[i].v.positional) {
            has_pos = true;
            break;
        }

    if (has_pos) {
        fprintf(out, "\nPositional arguments:\n");
        for (size_t i = 0; i < ap->len; i++) {
            ArgDef *d = &ap->args[i];
            if (d->type != ARG_VALUE || !d->v.positional)
                continue;
            const char *n = d->name ? d->name : "";
            char left[80];
            snprintf(left, sizeof left, "%s", n[0] ? n : valuetype_name(d->v.type));
            print_option_line(out, left, valuetype_name(d->v.type), /*required=*/true,
                              d->description);
        }
    }

    /* ----------------------------------------------------------------
     * Named options section
     * ---------------------------------------------------------------- */
    fprintf(out, "\nOptions:\n");

    /* Always show --help first */
    fprintf(out, "  --help, -h                        Show this help message\n");

    for (size_t i = 0; i < ap->len; i++) {
        ArgDef *d = &ap->args[i];
        /* Positionals already shown above */
        if (d->type == ARG_VALUE && d->v.positional)
            continue;

        const char *n = d->name ? d->name : "";
        char left[80];

        switch (d->type) {
        case ARG_FLAG: {
            char abrv = effective_abrv(d->f.allow_short, d->f.short_override, n);
            if (abrv)
                snprintf(left, sizeof left, "--%s, -%c", n, abrv);
            else
                snprintf(left, sizeof left, "--%s", n);
            /* Show default state as tag */
            char def_tag[24];
            snprintf(def_tag, sizeof def_tag, "bool=%s", d->f.b ? "true" : "false");
            print_option_line(out, left, def_tag, /*required=*/false, d->description);
            break;
        }
        case ARG_VALUE: {
            char abrv = effective_abrv(d->v.allow_short, d->v.short_override, n);
            if (abrv)
                snprintf(left, sizeof left, "--%s, -%c", n, abrv);
            else
                snprintf(left, sizeof left, "--%s", n);
            char def_tag[80];
            format_default(def_tag, sizeof def_tag, &d->v);
            print_option_line(out, left, def_tag, /*required=*/false, d->description);
            break;
        }
        }
    }

    fprintf(out, "\nVersion: %s (%s)%s, built: %s\n", PROJECT_GIT_DESCRIBE, PROJECT_GIT_COMMIT,
            PROJECT_GIT_DIRTY ? "-dirty" : "", PROJECT_BUILD_TIMESTAMP);
    fflush(out);
}

/* ------------------------------------------------------------------ */
/* argparse_parse — completed                                          */
/* ------------------------------------------------------------------ */

/*
 * Allocate a heap ArgParseResult pre-filled from an unmatched ArgDef default.
 * Returns NULL on allocation failure.
 */
static ArgParseResult *make_default_result(const ArgDef *def) {
    ArgParseResult *r = malloc(sizeof(ArgParseResult));
    if (!r)
        return NULL;

    r->name = def->name;

    if (def->type == ARG_FLAG) {
        r->type = ARG_FLAG;
        r->b = def->f.b;
    } else {
        r->type = ARG_VALUE;
        r->dtype = def->v.type;
        switch (def->v.type) {
        case VALUE_INT:
            r->i = def->v.i;
            break;
        case VALUE_FLOAT:
            r->f = def->v.f;
            break;
        case VALUE_CHAR:
            r->c = def->v.c;
            break;
        case VALUE_STR:
            r->s = def->v.s;
            break;
        }
    }
    return r;
}

/*
 * argparse_parse
 *
 * Tokens understood:
 *   --name          flag → true
 *   --name value    named value (two tokens)
 *   --name=value    named value (one token)
 *   -x              short flag → true
 *   -x value        short value (two tokens)
 *   -xVALUE         short value (inline, no space)
 *   bare token      positional, assigned in definition order
 *
 * On success every ArgDef (matched or defaulted) has one heap-allocated
 * ArgParseResult* stored in `result` under def->name.
 * The CALLER is responsible for freeing those values.
 *
 * Returns:
 *   SDL_APP_SUCCESS  — --help was requested
 *   SDL_APP_CONTINUE — all arguments parsed successfully
 *   SDL_APP_FAILURE  — parse error (logged; help printed to stderr)
 */
SDL_AppResult argparse_parse(int argc, char **argv, const ArgParse *ap, HashMap *result) {
    if (!argv || !ap || !ap->args || !result) {
        errno = EINVAL;
        LOGE("argparse_parse: null argv or defs (%s)", strerror(errno));
        return SDL_APP_FAILURE;
    }

    /* Track which defs have been matched */
    bool *matched = calloc(ap->len, sizeof(bool));
    if (!matched) {
        LOGE("argparse_parse: calloc failed (%s)", strerror(ENOMEM));
        return SDL_APP_FAILURE;
    }

    /* Collect positional def indices in definition order */
    size_t *pos_defs = malloc((ap->len + 1) * sizeof(size_t));
    if (!pos_defs) {
        free(matched);
        LOGE("argparse_parse: malloc failed (%s)", strerror(ENOMEM));
        return SDL_APP_FAILURE;
    }
    size_t pos_def_count = 0;
    for (size_t i = 0; i < ap->len; i++)
        if (ap->args[i].type == ARG_VALUE && ap->args[i].v.positional)
            pos_defs[pos_def_count++] = i;
    size_t pos_filled = 0;

    /* Convenience: program name for help output */
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : ap->app_name;

    SDL_AppResult status = SDL_APP_CONTINUE;

    for (int ai = 1; ai < argc && status == SDL_APP_CONTINUE; ai++) {
        const char *tok = argv[ai];
        if (!tok)
            continue;

        /* ---- built-in help ---- */
        if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
            argparse_print_help(stdout, prog, ap);
            free(matched);
            free(pos_defs);
            return SDL_APP_SUCCESS;
        }

        /* ---- long option: --name  or  --name=value ---- */
        if (strncmp(tok, "--", 2) == 0 && tok[2] != '\0') {
            const char *name_start = tok + 2;
            const char *eq = strchr(name_start, '=');
            char name[128];
            const char *inline_val = NULL;

            if (eq) {
                size_t nlen = (size_t)(eq - name_start);
                if (nlen >= sizeof name)
                    nlen = sizeof name - 1;
                memcpy(name, name_start, nlen);
                name[nlen] = '\0';
                inline_val = eq + 1;
            } else {
                snprintf(name, sizeof name, "%s", name_start);
            }

            ArgDef *def = find_by_long(ap, name);
            if (!def) {
                LOGE("argparse_parse: unknown option '--%s'", name);
                argparse_print_help(stderr, prog, ap);
                status = SDL_APP_FAILURE;
                break;
            }

            size_t def_idx = (size_t)(def - ap->args);
            ArgParseResult *r = malloc(sizeof(ArgParseResult));
            if (!r) {
                status = SDL_APP_FAILURE;
                break;
            }
            r->name = def->name;

            if (def->type == ARG_FLAG) {
                r->type = ARG_FLAG;
                r->b = true;
            } else {
                /* resolve value token */
                const char *val_tok = inline_val;
                if (!val_tok) {
                    if (ai + 1 < argc)
                        val_tok = argv[++ai];
                    else {
                        LOGE("argparse_parse: '--%s' requires an argument", name);
                        argparse_print_help(stderr, prog, ap);
                        free(r);
                        status = SDL_APP_FAILURE;
                        break;
                    }
                }
                r->type = ARG_VALUE;
                if (!parse_value_token(val_tok, def->v.type, r)) {
                    LOGE("argparse_parse: invalid value '%s' for '--%s' (expected %s)", val_tok,
                         name, valuetype_name(def->v.type));
                    argparse_print_help(stderr, prog, ap);
                    free(r);
                    status = SDL_APP_FAILURE;
                    break;
                }
            }

            hashmap_insert(result, def->name, r);
            matched[def_idx] = true;

            /* ---- short option: -x  or  -xVALUE ---- */
        } else if (tok[0] == '-' && tok[1] != '\0' && tok[1] != '-') {
            char c = tok[1];
            const char *inline_val = tok[2] ? tok + 2 : NULL;

            ArgDef *def = find_by_short(ap, c);
            if (!def) {
                LOGE("argparse_parse: unknown short option '-%c'", c);
                argparse_print_help(stderr, prog, ap);
                status = SDL_APP_FAILURE;
                break;
            }

            size_t def_idx = (size_t)(def - ap->args);
            ArgParseResult *r = malloc(sizeof(ArgParseResult));
            if (!r) {
                status = SDL_APP_FAILURE;
                break;
            }
            r->name = def->name;

            if (def->type == ARG_FLAG) {
                r->type = ARG_FLAG;
                r->b = true;
            } else {
                const char *val_tok = inline_val;
                if (!val_tok) {
                    if (ai + 1 < argc)
                        val_tok = argv[++ai];
                    else {
                        LOGE("argparse_parse: '-%c' requires an argument", c);
                        argparse_print_help(stderr, prog, ap);
                        free(r);
                        status = SDL_APP_FAILURE;
                        break;
                    }
                }
                r->type = ARG_VALUE;
                if (!parse_value_token(val_tok, def->v.type, r)) {
                    LOGE("argparse_parse: invalid value '%s' for '-%c' (expected %s)", val_tok, c,
                         valuetype_name(def->v.type));
                    argparse_print_help(stderr, prog, ap);
                    free(r);
                    status = SDL_APP_FAILURE;
                    break;
                }
            }

            hashmap_insert(result, def->name, r);
            matched[def_idx] = true;

            /* ---- positional ---- */
        } else {
            if (pos_filled >= pos_def_count) {
                LOGW("argparse_parse: unexpected positional argument '%s'", tok);
                continue;
            }

            size_t def_idx = pos_defs[pos_filled++];
            ArgDef *def = &ap->args[def_idx];

            ArgParseResult *r = malloc(sizeof(ArgParseResult));
            if (!r) {
                status = SDL_APP_FAILURE;
                break;
            }
            r->name = def->name;
            r->type = ARG_VALUE;
            if (!parse_value_token(tok, def->v.type, r)) {
                LOGE("argparse_parse: invalid positional value '%s' (expected %s)", tok,
                     valuetype_name(def->v.type));
                argparse_print_help(stderr, prog, ap);
                free(r);
                status = SDL_APP_FAILURE;
                break;
            }

            hashmap_insert(result, def->name, r);
            matched[def_idx] = true;
        }
    } /* for each argv token */

    /* ----------------------------------------------------------------
     * Post-parse pass:
     *   - unmatched positionals → error + help
     *   - unmatched named args  → insert their default
     * ---------------------------------------------------------------- */
    if (status == SDL_APP_CONTINUE) {
        for (size_t i = 0; i < ap->len; i++) {
            if (matched[i])
                continue;
            ArgDef *def = &ap->args[i];

            if (def->type == ARG_VALUE && def->v.positional) {
                LOGE("argparse_parse: missing required positional argument <%s>",
                     def->name ? def->name : valuetype_name(def->v.type));
                argparse_print_help(stderr, prog, ap);
                status = SDL_APP_FAILURE;
                break;
            }

            ArgParseResult *r = make_default_result(def);
            if (!r) {
                status = SDL_APP_FAILURE;
                break;
            }
            hashmap_insert(result, def->name, r);
        }
    }

    free(matched);
    free(pos_defs);

    if (status == SDL_APP_CONTINUE) {
        errno = 0;
        LOGI("argparse_parse: arguments parsed successfully");
    }
    return status;
}
