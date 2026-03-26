// argparse.c
#include "argparse.h"
#include "hashmap.h"
#include "log.h"
#include "version.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h> /* only for snprintf for error messages */
#include <stdlib.h>
#include <string.h>

/* effective_abrv — same logic as before */
static char effective_abrv(bool has_abrv, char overwrite_abrv, const char *long_name) {
    if (!has_abrv)
        return '\0';
    if (overwrite_abrv)
        return overwrite_abrv;
    return (char)tolower((unsigned char)long_name[0]);
}

/* parse_value with errno-style return codes; on error returns a non-zero
 * POSIX errno (EINVAL for invalid token/format, ERANGE for out-of-range).
 */
static int parse_value_checked(const char *token, ValueType type, Value *out_val) {
    if (!token)
        return EINVAL;

    switch (type) {
    case VAL_FLOAT: {
        char *end = NULL;
        errno = 0;
        float f = strtof(token, &end);
        if (end == token || *end != '\0')
            return EINVAL;
        if (errno == ERANGE)
            return ERANGE;
        if (out_val) {
            out_val->type = VAL_FLOAT;
            out_val->val.f = f;
        }
        return 0;
    }
    case VAL_INT: {
        char *end = NULL;
        errno = 0;
        long l = strtol(token, &end, 0);
        if (end == token || *end != '\0')
            return EINVAL;
        if (errno == ERANGE)
            return ERANGE;
        if (l < INT_MIN || l > INT_MAX)
            return ERANGE;
        if (out_val) {
            out_val->type = VAL_INT;
            out_val->val.i = (int)l;
        }
        return 0;
    }
    case VAL_CHAR:
        if (strlen(token) != 1)
            return EINVAL;
        if (out_val) {
            out_val->type = VAL_CHAR;
            out_val->val.c = token[0];
        }
        return 0;
    case VAL_STRING:
        if (out_val) {
            out_val->type = VAL_STRING;
            out_val->val.s = (char *)token;
        }
        return 0;
    case VAL_BOOL:
        if (strcmp(token, "true") == 0 || strcmp(token, "1") == 0) {
            if (out_val) {
                out_val->type = VAL_BOOL;
                out_val->val.b = true;
            }
            return 0;
        } else if (strcmp(token, "false") == 0 || strcmp(token, "0") == 0) {
            if (out_val) {
                out_val->type = VAL_BOOL;
                out_val->val.b = false;
            }
            return 0;
        } else {
            return EINVAL;
        }
    default:
        return EINVAL;
    }
}

/* Helper to count positional index (for messages) */
// static size_t positional_index_of(const ArgDef *defs, size_t n, size_t idx) {
//     size_t pnum = 0;
//     for (size_t j = 0; j < n; j++) {
//         if (defs[j].type == ARG_POSITIONAL)
//             pnum++;
//         if (j == idx)
//             break;
//     }
//     return pnum;
// }

/* return 0 on success, or a POSIX errno on failure */
static int add_element(ArgParse *ap, ArgDef **out_slot) {
    if (!ap || !out_slot)
        return EINVAL;

    /* ensure capacity */
    if (ap->len >= ap->cap) {
        size_t new_cap = ap->cap ? ap->cap * 2 : 4;
        ArgDef *new_args = realloc(ap->args, new_cap * sizeof *new_args);
        if (!new_args)
            return ENOMEM;

        /* zero the newly allocated region */
        if (new_cap > ap->cap) {
            size_t old_bytes = ap->cap * sizeof *new_args;
            size_t new_bytes = new_cap * sizeof *new_args;
            memset((char *)new_args + old_bytes, 0, new_bytes - old_bytes);
        }

        ap->args = new_args;
        ap->cap = new_cap;
    }

    /* return pointer to next free slot and increment len */
    *out_slot = &ap->args[ap->len++];
    memset(*out_slot, 0, sizeof **out_slot);

    return 0;
}

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

/* Builder functions now return 0 on success or a POSIX errno. */

int add_kw_argument(ArgParse *ap, const char *label, const char *description, ValueType type,
                    bool has_abrv, char overwrite_abrv, bool required, Value *default_val) {
    ArgDef *slot;
    int rc = add_element(ap, &slot);
    if (rc)
        return rc;

    if (!slot || !label)
        return EINVAL;

    slot->type = ARG_KEYWORD;
    slot->description = description;
    slot->keyword.long_name = (char *)label;
    slot->keyword.has_abrv = has_abrv;
    slot->keyword.overwrite_abrv = overwrite_abrv;
    slot->keyword.default_val = default_val;
    slot->keyword.required = required;
    slot->keyword.type = type;
    return 0;
}

int add_flag(ArgParse *ap, const char *label, const char *description, bool has_abrv,
             char overwrite_abrv, bool required, bool *default_val) {
    ArgDef *slot;
    int rc = add_element(ap, &slot);
    if (rc)
        return rc;

    if (!slot || !label)
        return EINVAL;

    slot->type = ARG_FLAG;
    slot->description = description;
    slot->flag.long_name = (char *)label;
    slot->flag.has_abrv = has_abrv;
    slot->flag.overwrite_abrv = overwrite_abrv;
    slot->flag.default_val = default_val;
    slot->flag.required = required;
    return 0;
}

int add_positional_argument(ArgParse *ap, const char *description, ValueType type, bool required,
                            Value *default_val) {
    ArgDef *slot;
    int rc = add_element(ap, &slot);
    if (rc)
        return rc;

    if (!slot)
        return EINVAL;

    slot->type = ARG_POSITIONAL;
    slot->description = description;
    slot->positional.default_val = default_val;
    slot->positional.required = required;
    slot->positional.type = type;
    return 0;
}

int add_flaglist(ArgParse *ap, const char *label, const char *description, uint64_t default_val) {
    ArgDef *slot;
    int rc = add_element(ap, &slot);
    if (rc)
        return rc;

    if (!slot || !label)
        return EINVAL;

    slot->type = ARG_FLAG_LIST;
    slot->description = description;
    slot->flag_list.list_name = (char *)label;
    slot->flag_list.count = 0;
    slot->flag_list.default_val = default_val;
    return 0;
}

int add_flaglist_flag(ArgParse *ap, const char *list_label, const char *flag_label, bool has_abrv,
                      char overwrite_abrv, bool *default_val) {
    if (!ap || !ap->args || !flag_label || !list_label)
        return EINVAL;

    ArgDef *defs = ap->args;
    size_t ndefs = ap->len;

    for (size_t i = 0; i < ndefs; i++) {
        if (defs[i].type != ARG_FLAG_LIST)
            continue;
        const char *lbl = defs[i].flag_list.list_name;
        if (!lbl)
            continue;
        if (strcmp(lbl, list_label) == 0) {
            uint32_t cnt = defs[i].flag_list.count;
            if (cnt >= FLAGLIST_MAX_LEN)
                return EOVERFLOW;
            defs[i].flag_list.flags[cnt].long_name = (char *)flag_label;
            defs[i].flag_list.flags[cnt].has_abrv = has_abrv;
            defs[i].flag_list.flags[cnt].overwrite_abrv = overwrite_abrv;
            defs[i].flag_list.default_val =
                (defs[i].flag_list.default_val & ~(1u << cnt)) | ((unsigned)(!!default_val) << cnt);
            defs[i].flag_list.count = cnt + 1;
            return 0;
        }
    }

    return ENOENT;
}

/* Helper: valtype_name unchanged and internal */
static const char *valuetype_name(ValueType t) {
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

/* Print a single option line with aligned columns.
 *
 * Format:
 *   <left_col>    <right_col>
 *
 * left_col  : flag/name/type info, left-padded with 2 spaces
 * right_col : description (if non-NULL), separated by enough spaces to reach
 *             HELP_DESC_COL
 */
#define HELP_DESC_COL 32

static void print_option_line(FILE *out, const char *left, const char *tag, bool required,
                              const char *description) {
    /* build left column: "  --name <type>" or "  --name" */
    char col[128];
    int n = snprintf(col, sizeof col, "  %s", left);
    if (tag && tag[0])
        n += snprintf(col + n, sizeof col - (size_t)n, " <%s>", tag);

    fprintf(out, "%s", col);

    /* pad to description column */
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

/* Print help/usage for an ArgParse definition.
 * - out: destination (e.g. stdout or stderr)
 * - progname: program name to show in usage (if NULL use ap->app_name)
 * - ap: pointer to ArgParse (must be initialized)
 */
void argparse_print_help(FILE *out, const char *progname, const ArgParse *ap) {
    if (!out || !ap)
        return;
    const char *pname = progname ? progname : (ap->app_name ? ap->app_name : PROJECT_NAME);

    /* Header / usage */
    fprintf(out, "Usage: %s", pname);
    if (ap->usage && ap->usage[0]) {
        fprintf(out, " %s\n\n", ap->usage);
    } else {
        /* show simple usage synopsis: flags and positionals */
        for (size_t i = 0; i < ap->len; ++i) {
            ArgDef *d = &ap->args[i];
            switch (d->type) {
            case ARG_FLAG:
                if (d->flag.required)
                    fprintf(out, " --%s", d->flag.long_name ? d->flag.long_name : "flag");
                else
                    fprintf(out, " [--%s]", d->flag.long_name ? d->flag.long_name : "flag");
                break;
            case ARG_KEYWORD:
                if (d->keyword.required)
                    fprintf(out, " --%s <%s>", d->keyword.long_name ? d->keyword.long_name : "opt",
                            valuetype_name(d->keyword.type));
                else
                    fprintf(out, " [--%s <%s>]",
                            d->keyword.long_name ? d->keyword.long_name : "opt",
                            valuetype_name(d->keyword.type));
                break;
            case ARG_POSITIONAL:
                if (d->positional.required)
                    fprintf(out, " <%s>", valuetype_name(d->positional.type));
                else
                    fprintf(out, " [<%s>]", valuetype_name(d->positional.type));
                break;
            case ARG_FLAG_LIST:
                char flags[d->flag_list.count];
                for (int i = 0; i < d->flag_list.count; i++) {
                    flags[i] = effective_abrv(d->flag_list.flags[i].has_abrv,
                                              d->flag_list.flags[i].overwrite_abrv,
                                              d->flag_list.flags[i].long_name);
                }
                fprintf(out, " [--%s %s]",
                        d->flag_list.list_name ? d->flag_list.list_name : "ERROR", flags);
                break;
            }
        }
        fprintf(out, "\n\n");
    }

    /* Description (if provided) */
    if (ap->description && ap->description[0]) {
        fprintf(out, "%s\n\n", ap->description);
    }

    fprintf(out, "Options:\n");
    for (size_t i = 0; i < ap->len; ++i) {
        ArgDef *d = &ap->args[i];
        switch (d->type) {
        case ARG_FLAG: {
            const char *lname = d->flag.long_name ? d->flag.long_name : "";
            char abrv = effective_abrv(d->flag.has_abrv, d->flag.overwrite_abrv, lname);
            char left[64];
            if (abrv)
                snprintf(left, sizeof left, "--%s, -%c", lname, abrv);
            else
                snprintf(left, sizeof left, "--%s", lname);
            print_option_line(out, left, NULL, d->flag.required, d->description);
            break;
        }
        case ARG_KEYWORD: {
            const char *lname = d->keyword.long_name ? d->keyword.long_name : "";
            char abrv = effective_abrv(d->keyword.has_abrv, d->keyword.overwrite_abrv, lname);
            char left[64];
            if (abrv)
                snprintf(left, sizeof left, "--%s, -%c", lname, abrv);
            else
                snprintf(left, sizeof left, "--%s", lname);
            print_option_line(out, left, valuetype_name(d->keyword.type), d->keyword.required,
                              d->description);
            break;
        }
        case ARG_POSITIONAL: {
            char left[32];
            snprintf(left, sizeof left, "%s", valuetype_name(d->positional.type));
            print_option_line(out, left, NULL, d->positional.required, d->description);
            break;
        }
        case ARG_FLAG_LIST: {
            const char *lname = d->flag_list.list_name ? d->flag_list.list_name : "";

            char flags[d->flag_list.count];
            for (int i = 0; i < d->flag_list.count; i++) {
                flags[i] = effective_abrv(d->flag_list.flags[i].has_abrv,
                                          d->flag_list.flags[i].overwrite_abrv,
                                          d->flag_list.flags[i].long_name);
            }
            char left[64];
            snprintf(left, sizeof left, "--%s %s", lname, flags);
            print_option_line(out, left, NULL, false, d->description);
            break;
        }
        }
    }

    fprintf(out, "\nVersion: %s (%s)%s, built: %s\n", PROJECT_GIT_DESCRIBE, PROJECT_GIT_COMMIT,
            PROJECT_GIT_DIRTY ? "-dirty" : "", PROJECT_BUILD_TIMESTAMP);
    fflush(out);
}

/* argparse_parse: logs errors internally using LOGE/LOGW; still returns
 * SDL_AppResult and sets errno for caller to inspect.
 */
SDL_AppResult argparse_parse(int argc, char **argv, const ArgParse *ap) {
    if (!argv || !ap || !ap->args) {
        errno = EINVAL;
        LOGE("argparse_parse: null argv or defs (%s)", strerror(errno));
        return SDL_APP_FAILURE;
    }

    HashMap *map = hashmap_new(0);

    bool *matched = calloc(ap->len, sizeof(bool));
    if (!matched) {
        errno = ENOMEM;
        LOGE("argparse_parse: calloc failed (%s)", strerror(errno));
        return SDL_APP_FAILURE;
    }

    size_t *pos_defs = malloc(ap->len * sizeof(size_t));
    if (!pos_defs) {
        free(matched);
        errno = ENOMEM;
        LOGE("argparse_parse: malloc for pos_defs failed (%s)", strerror(errno));
        return SDL_APP_FAILURE;
    }

    size_t pos_def_count = 0;
    for (size_t i = 0; i < ap->len; i++) {
        if (ap->args[i].type == ARG_POSITIONAL)
            pos_defs[pos_def_count++] = i;
    }
    size_t pos_filled = 0;

    for (int ai = 1; ai < argc; ai++) {
        const char *tok = argv[ai];
        if (!tok)
            continue;

        if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
            free(matched);
            free(pos_defs);
            errno = 0;
            argparse_print_help(stdout, NULL, ap);
            return SDL_APP_SUCCESS;
        }

        if (tok[0] == '-' && tok[1] == '-') {
            const char *name_start = tok + 2;
            if (*name_start == '\0') {
                free(matched);
                free(pos_defs);
                errno = EINVAL;
                LOGE("argparse_parse: bare '--' is not supported (%s)", strerror(errno));
                return SDL_APP_FAILURE;
            }
            const char *eq = strchr(name_start, '=');
            size_t name_len = eq ? (size_t)(eq - name_start) : strlen(name_start);
            const char *inline_val = eq ? eq + 1 : NULL;

            bool found = false;
            ArgResult res;
            for (size_t i = 0; i < ap->len && !found; i++) {
                const ArgDef *d = &ap->args[i];
                switch (d->type) {
                case ARG_KEYWORD: {
                    const KeywordArg *k = &d->keyword;
                    if (k->long_name && strncmp(k->long_name, name_start, name_len) == 0 &&
                        k->long_name[name_len] == '\0') {
                        const char *valstr = inline_val;
                        if (!valstr) {
                            if (ai + 1 >= argc) {
                                free(matched);
                                free(pos_defs);
                                errno = EINVAL;
                                LOGE("argparse_parse: --%s requires a <%s> "
                                     "value (%s)",
                                     k->long_name, valuetype_name(k->type), strerror(errno));
                                return SDL_APP_FAILURE;
                            }
                            valstr = argv[++ai];
                        }
                        Value v;
                        int rc = parse_value_checked(valstr, k->type, &v);
                        if (rc != 0) {
                            free(matched);
                            free(pos_defs);
                            errno = rc;
                            LOGE("argparse_parse: invalid value for --%s: '%s' "
                                 "(%s)",
                                 k->long_name, valstr, strerror(errno));
                            return SDL_APP_FAILURE;
                        }

                        res.name = k->long_name;
                        res.type = ARG_KEYWORD;
                        res.v = v;

                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG: {
                    const Flag *f = &d->flag;
                    if (f->long_name && strncmp(f->long_name, name_start, name_len) == 0 &&
                        f->long_name[name_len] == '\0') {

                        res.name = f->long_name;
                        res.type = ARG_FLAG;
                        res.b = true;

                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG_LIST: {
                    const FlagList *fl = &d->flag_list;
                    if (fl->list_name && strncmp(fl->list_name, name_start, name_len) == 0 &&
                        fl->list_name[name_len] == '\0') {
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
                errno = ENOENT;
                LOGE("argparse_parse: unknown option '--%.*s' (%s)", (int)name_len, name_start,
                     strerror(errno));
                return SDL_APP_FAILURE;
            }

            //} else if (tok[0] == '-' && tok[1] != '\0') {
            //    const char *name_start = tok + 1;
            //    bool found = false;
            //    for (size_t i = 0; i < ap->len && !found; i++) {
            //    }

        } else if (tok[0] == '-' && tok[1] != '\0' && tok[2] == '\0') {
            char ab = tok[1];
            bool found = false;

            for (size_t i = 0; i < ap->len && !found; i++) {
                const ArgDef *d = &ap->args[i];
                switch (d->type) {
                case ARG_KEYWORD: {
                    const KeywordArg *k = &d->keyword;
                    char eff = effective_abrv(k->has_abrv, k->overwrite_abrv, k->long_name);
                    if (eff && eff == ab) {
                        if (ai + 1 >= argc) {
                            free(matched);
                            free(pos_defs);
                            errno = EINVAL;
                            LOGE("argparse_parse: -%c requires a <%s> value "
                                 "(%s)",
                                 ab, valuetype_name(k->type), strerror(errno));
                            return SDL_APP_FAILURE;
                        }
                        Value v;
                        int rc = parse_value_checked(argv[++ai], k->type, &v);
                        if (rc != 0) {
                            free(matched);
                            free(pos_defs);
                            errno = rc;
                            LOGE("argparse_parse: invalid value for -%c: "
                                 "'%s' "
                                 "(%s)",
                                 ab, argv[ai], strerror(errno));
                            return SDL_APP_FAILURE;
                        }
                        // TODO: store in hashmap
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG: {
                    const Flag *f = &d->flag;
                    char eff = effective_abrv(f->has_abrv, f->overwrite_abrv, f->long_name);
                    if (eff && eff == ab) {
                        // TODO: store in hashmap
                        matched[i] = true;
                        found = true;
                    }
                    break;
                }
                case ARG_FLAG_LIST: {
                    const FlagList *fl = &d->flag_list;
                    for (uint32_t j = 0; j < fl->count && !found; j++) {
                        const FlagEntry *fe = &fl->flags[j];
                        char eff = effective_abrv(fe->has_abrv, fe->overwrite_abrv, fe->long_name);
                        if (eff && eff == ab) {
                            // TODO: store in hashmap
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
                errno = ENOENT;
                LOGE("argparse_parse: unknown option '-%c' (%s)", ab, strerror(errno));
                return SDL_APP_FAILURE;
            }
        } else {
            if (pos_filled >= pos_def_count) {
                free(matched);
                free(pos_defs);
                errno = E2BIG;
                LOGE("argparse_parse: unexpected positional argument '%s' "
                     "(%s)",
                     tok, strerror(errno));
                return SDL_APP_FAILURE;
            }
            size_t di = pos_defs[pos_filled++];
            const PositionalArg *p = &ap->args[di].positional;
            Value v;
            int rc = parse_value_checked(tok, p->type, &v);
            if (rc != 0) {
                free(matched);
                free(pos_defs);
                errno = rc;
                LOGE("argparse_parse: invalid positional value '%s' (%s)", tok, strerror(errno));
                return SDL_APP_FAILURE;
            }
            // TODO: store in hashmap
            matched[di] = true;
        }
    }

    for (size_t i = 0; i < ap->len; i++) {
        if (matched[i])
            continue;
        const ArgDef *d = &ap->args[i];
        switch (d->type) {
        case ARG_KEYWORD:
            if (d->keyword.required) {
                free(matched);
                free(pos_defs);
                errno = EINVAL;
                LOGE("argparse_parse: required argument '--%s' was not "
                     "provided (%s)",
                     d->keyword.long_name, strerror(errno));
                return SDL_APP_FAILURE;
            }
            break;
        case ARG_FLAG:
            if (d->flag.required) {
                free(matched);
                free(pos_defs);
                errno = EINVAL;
                LOGE("argparse_parse: required flag '--%s' was not provided "
                     "(%s)",
                     d->flag.long_name, strerror(errno));
                return SDL_APP_FAILURE;
            }
            break;
        case ARG_POSITIONAL:
            if (d->positional.required) {
                free(matched);
                free(pos_defs);
                errno = EINVAL;
                LOGE("argparse_parse: required positional argument was not "
                     "provided (%s)",
                     strerror(errno));
                return SDL_APP_FAILURE;
            }
            break;
        case ARG_FLAG_LIST:
            break;
        }
    }

    free(matched);
    free(pos_defs);
    errno = 0;
    LOGI("argparse_parse: arguments parsed successfully");
    return SDL_APP_CONTINUE;
}
