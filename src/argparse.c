#include "argparse.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * effective_abrv — return the short-form character for an arg, or '\0'.
 *
 * has_abrv == false              -> '\0'  (no short form)
 * has_abrv == true,
 *   overwrite_abrv == '\0'       -> long_name[0]  (auto-derived)
 *   overwrite_abrv != '\0'       -> overwrite_abrv
 */
static char effective_abrv(bool has_abrv, char overwrite_abrv,
                           const char *long_name) {
    if (!has_abrv)
        return '\0';
    if (overwrite_abrv)
        return overwrite_abrv;
    return (char)tolower((unsigned char)long_name[0]);
}

/* ─── error exit ────────────────────────────────────────────────────────────
 */

static void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ─── string → Value, exit on bad input ─────────────────────────────────────
 */

static Value parse_value(const char *token, ValueType type) {
    Value v;
    v.type = type;

    switch (type) {
    case VAL_FLOAT: {
        char *end;
        v.val.f = strtof(token, &end);
        if (end == token || *end != '\0')
            die("expected <float>, got '%s'", token);
        break;
    }
    case VAL_INT: {
        char *end;
        v.val.i = (int)strtol(token, &end, 0);
        if (end == token || *end != '\0')
            die("expected <int>, got '%s'", token);
        break;
    }
    case VAL_CHAR:
        if (strlen(token) != 1)
            die("expected a single <char>, got '%s'", token);
        v.val.c = token[0];
        break;

    case VAL_STRING:
        v.val.s = (char *)token; /* points into argv[] */
        break;

    case VAL_BOOL:
        if (strcmp(token, "true") == 0 || strcmp(token, "1") == 0)
            v.val.b = true;
        else if (strcmp(token, "false") == 0 || strcmp(token, "0") == 0)
            v.val.b = false;
        else
            die("expected <bool> (true/false/1/0), got '%s'", token);
        break;
    }
    return v;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Help generation
 * ═══════════════════════════════════════════════════════════════════════════
 */

static const char *valtype_name(ValueType t) {
    switch (t) {
    case VAL_FLOAT:
        return "float";
    case VAL_INT:
        return "int";
    case VAL_CHAR:
        return "char";
    case VAL_STRING:
        return "string";
    case VAL_BOOL:
        return "bool";
    }
    return "?";
}

/* Print "  -x, " or "      " (6 chars). */
static void print_abrv_col(char ab) {
    if (ab)
        printf("  -%c, ", ab);
    else
        printf("      ");
}

static void print_help(const char *prog, const ArgDef *defs, size_t n) {
    printf("Usage: %s [options] [positional...]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("        Show this help message and exit.\n\n");

    size_t pos_num = 1;

    for (size_t i = 0; i < n; i++) {
        const ArgDef *d = &defs[i];

        switch (d->type) {

        case ARG_KEYWORD: {
            const KeywordArg *k = &d->keyword;
            char ab =
                effective_abrv(k->has_abrv, k->overwrite_abrv, k->long_name);
            print_abrv_col(ab);
            printf("--%s <%s>%s\n", k->long_name, valtype_name(k->type),
                   k->required ? "  (required)" : "");
            break;
        }

        case ARG_FLAG: {
            const Flag *f = &d->flag;
            char ab =
                effective_abrv(f->has_abrv, f->overwrite_abrv, f->long_name);
            print_abrv_col(ab);
            printf("--%s%s\n", f->long_name, f->required ? "  (required)" : "");
            break;
        }

        case ARG_POSITIONAL: {
            const PositionalArg *p = &d->positional;
            printf("  <positional-%zu>  <%s>%s\n", pos_num++,
                   valtype_name(p->type), p->required ? "  (required)" : "");
            break;
        }

        case ARG_FLAG_LIST: {
            const FlagList *fl = &d->flag_list;
            printf("  Flag group (uint32_t bitmask)%s:\n",
                   fl->required ? "  (at least one required)" : "");
            for (uint32_t j = 0; j < fl->count; j++) {
                const FlagEntry *fe = &fl->flags[j];
                char ab = effective_abrv(fe->has_abrv, fe->overwrite_abrv,
                                         fe->long_name);
                print_abrv_col(ab);
                printf("--%s  (bit %u)\n", fe->long_name, j);
            }
            break;
        }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * argparse_parse
 * ═══════════════════════════════════════════════════════════════════════════
 */

int argparse_parse(int argc, char **argv, const ArgDef *defs, size_t n) {
    /* ── track which defs have been matched (for required checks) ────────────
     */
    bool *matched = calloc(n, sizeof(bool));
    if (!matched) {
        fprintf(stderr, "argparse: out of memory\n");
        exit(1);
    }

    /* ── collect positional-def indices in definition order ──────────────────
     */
    size_t *pos_defs = malloc(n * sizeof(size_t));
    if (!pos_defs) {
        free(matched);
        fprintf(stderr, "argparse: out of memory\n");
        exit(1);
    }
    size_t pos_def_count = 0;
    for (size_t i = 0; i < n; i++) {
        if (defs[i].type == ARG_POSITIONAL)
            pos_defs[pos_def_count++] = i;
    }
    size_t pos_filled = 0;

    /* ── walk argv[1..] ───────────────────────────────────────────────────────
     */
    for (int ai = 1; ai < argc; ai++) {
        const char *tok = argv[ai];

        /* ── built-in --help / -h ───────────────────────────────────────────
         */
        if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
            print_help(argv[0], defs, n);
            free(matched);
            free(pos_defs);
            exit(0);
        }

        /* ── long option  --name  or  --name=value ──────────────────────── */
        if (tok[0] == '-' && tok[1] == '-') {
            const char *name_start = tok + 2;

            if (*name_start == '\0') {
                free(matched);
                free(pos_defs);
                die("bare '--' is not supported");
            }

            const char *eq = strchr(name_start, '=');
            size_t name_len =
                eq ? (size_t)(eq - name_start) : strlen(name_start);
            const char *inline_val = eq ? eq + 1 : NULL;

            bool found = false;

            for (size_t i = 0; i < n && !found; i++) {
                const ArgDef *d = &defs[i];

                switch (d->type) {
                case ARG_KEYWORD: {
                    const KeywordArg *k = &d->keyword;
                    if (strncmp(k->long_name, name_start, name_len) == 0 &&
                        k->long_name[name_len] == '\0') {
                        const char *valstr = inline_val;
                        if (!valstr) {
                            if (ai + 1 >= argc) {
                                free(matched);
                                free(pos_defs);
                                die("--%s requires a <%s> value", k->long_name,
                                    valtype_name(k->type));
                            }
                            valstr = argv[++ai];
                        }
                        Value v = parse_value(valstr, k->type);
                        if (k->out)
                            *k->out = v;
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG: {
                    const Flag *f = &d->flag;
                    if (strncmp(f->long_name, name_start, name_len) == 0 &&
                        f->long_name[name_len] == '\0') {
                        if (f->out)
                            *f->out = true;
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG_LIST: {
                    const FlagList *fl = &d->flag_list;
                    for (uint32_t j = 0; j < fl->count && !found; j++) {
                        const FlagEntry *fe = &fl->flags[j];
                        if (strncmp(fe->long_name, name_start, name_len) == 0 &&
                            fe->long_name[name_len] == '\0') {
                            if (fl->out)
                                *fl->out |= (1u << j);
                            matched[i] = true;
                            found = true;
                        }
                    }
                    break;
                }
                case ARG_POSITIONAL:
                    break;
                }
            }

            if (!found) {
                free(matched);
                free(pos_defs);
                die("unknown option '--%.*s'", (int)name_len, name_start);
            }

            /* ── short option  -x  ────────────────────────────────────────────
             */
        } else if (tok[0] == '-' && tok[1] != '\0' && tok[2] == '\0') {
            char ab = tok[1];
            bool found = false;

            for (size_t i = 0; i < n && !found; i++) {
                const ArgDef *d = &defs[i];

                switch (d->type) {
                case ARG_KEYWORD: {
                    const KeywordArg *k = &d->keyword;
                    char eff = effective_abrv(k->has_abrv, k->overwrite_abrv,
                                              k->long_name);
                    if (eff && eff == ab) {
                        if (ai + 1 >= argc) {
                            free(matched);
                            free(pos_defs);
                            die("-%c requires a <%s> value", ab,
                                valtype_name(k->type));
                        }
                        Value v = parse_value(argv[++ai], k->type);
                        if (k->out)
                            *k->out = v;
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG: {
                    const Flag *f = &d->flag;
                    char eff = effective_abrv(f->has_abrv, f->overwrite_abrv,
                                              f->long_name);
                    if (eff && eff == ab) {
                        if (f->out)
                            *f->out = true;
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG_LIST: {
                    const FlagList *fl = &d->flag_list;
                    for (uint32_t j = 0; j < fl->count && !found; j++) {
                        const FlagEntry *fe = &fl->flags[j];
                        char eff = effective_abrv(
                            fe->has_abrv, fe->overwrite_abrv, fe->long_name);
                        if (eff && eff == ab) {
                            if (fl->out)
                                *fl->out |= (1u << j);
                            matched[i] = true;
                            found = true;
                        }
                    }
                    break;
                }
                case ARG_POSITIONAL:
                    break;
                }
            }

            if (!found) {
                free(matched);
                free(pos_defs);
                die("unknown option '-%c'", ab);
            }

            /* ── positional token ─────────────────────────────────────────────
             */
        } else {
            if (pos_filled >= pos_def_count) {
                free(matched);
                free(pos_defs);
                die("unexpected positional argument '%s'", tok);
            }
            size_t di = pos_defs[pos_filled++];
            const PositionalArg *p = &defs[di].positional;
            Value v = parse_value(tok, p->type);
            if (p->out)
                *p->out = v;
            matched[di] = true;
        }
    }

    /* ── required checks ──────────────────────────────────────────────────────
     */
    for (size_t i = 0; i < n; i++) {
        if (matched[i])
            continue;

        const ArgDef *d = &defs[i];
        switch (d->type) {
        case ARG_KEYWORD:
            if (d->keyword.required) {
                free(matched);
                free(pos_defs);
                die("required argument '--%s' was not provided",
                    d->keyword.long_name);
            }
            break;
        case ARG_FLAG:
            if (d->flag.required) {
                free(matched);
                free(pos_defs);
                die("required flag '--%s' was not provided", d->flag.long_name);
            }
            break;
        case ARG_POSITIONAL:
            if (d->positional.required) {
                /* Count which positional number this is for the message. */
                size_t pnum = 0;
                for (size_t j = 0; j < n; j++) {
                    if (defs[j].type == ARG_POSITIONAL)
                        pnum++;
                    if (j == i)
                        break;
                }
                free(matched);
                free(pos_defs);
                die("required positional argument <%zu> was not provided",
                    pnum);
            }
            break;
        case ARG_FLAG_LIST:
            if (d->flag_list.required &&
                (d->flag_list.out == NULL || *d->flag_list.out == 0)) {
                free(matched);
                free(pos_defs);
                die("at least one flag from flag-group %zu must be provided",
                    i);
            }
            break;
        }
    }

    free(matched);
    free(pos_defs);
    return 0;
}
