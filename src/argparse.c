// argparse.c
#include "argparse.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>   /* only for snprintf for error messages */
#include <stdlib.h>
#include <string.h>

/* Helper: populate ArgParseError if non-NULL */
static void set_err(ArgParseError *err, ArgParseErrCode code,
                    const char *msg_fmt, ...)
{
    if (!err) return;
    err->code = code;
    /* Build a small message buffer */
    static char buf[512];
    va_list ap;
    va_start(ap, msg_fmt);
    vsnprintf(buf, sizeof(buf), msg_fmt, ap);
    va_end(ap);
    err->msg  = buf;
    err->file = __FILE__;
    err->line = __LINE__;
}

/* effective_abrv — same logic as before */
static char effective_abrv(bool has_abrv, char overwrite_abrv,
                            const char *long_name)
{
    if (!has_abrv)       return '\0';
    if (overwrite_abrv)  return overwrite_abrv;
    return (char)tolower((unsigned char)long_name[0]);
}

/* parse_value with errno checks; on error returns non-zero and sets err if present */
static int parse_value_checked(const char *token, ValueType type,
                               Value *out_val, ArgParseError *err)
{
    if (!token) {
        set_err(err, APERR_INVALID_ARG, "internal: null token");
        return -1;
    }

    switch (type) {
        case VAL_FLOAT: {
            char *end = NULL;
            errno = 0;
            float f = strtof(token, &end);
            if (end == token || *end != '\0' || errno == ERANGE) {
                set_err(err, APERR_OTHER, "expected <float>, got '%s'", token);
                return -1;
            }
            if (out_val) { out_val->type = VAL_FLOAT; out_val->val.f = f; }
            return 0;
        }
        case VAL_INT: {
            char *end = NULL;
            errno = 0;
            long l = strtol(token, &end, 0);
            if (end == token || *end != '\0' || errno == ERANGE
                || l < INT_MIN || l > INT_MAX) {
                set_err(err, APERR_OTHER, "expected <int>, got '%s'", token);
                return -1;
            }
            if (out_val) { out_val->type = VAL_INT; out_val->val.i = (int)l; }
            return 0;
        }
        case VAL_CHAR:
            if (strlen(token) != 1) {
                set_err(err, APERR_OTHER, "expected a single <char>, got '%s'",
                        token);
                return -1;
            }
            if (out_val) { out_val->type = VAL_CHAR; out_val->val.c = token[0]; }
            return 0;
        case VAL_STRING:
            if (out_val) { out_val->type = VAL_STRING; out_val->val.s = (char *)token; }
            return 0;
        case VAL_BOOL:
            if      (strcmp(token, "true")  == 0 || strcmp(token, "1") == 0) {
                if (out_val) { out_val->type = VAL_BOOL; out_val->val.b = true; }
                return 0;
            } else if (strcmp(token, "false") == 0 || strcmp(token, "0") == 0) {
                if (out_val) { out_val->type = VAL_BOOL; out_val->val.b = false; }
                return 0;
            } else {
                set_err(err, APERR_OTHER, "expected <bool> (true/false/1/0), got '%s'",
                        token);
                return -1;
            }
    }
    set_err(err, APERR_OTHER, "unknown value type");
    return -1;
}

/* Helper to count positional index (for messages) */
static size_t positional_index_of(const ArgDef *defs, size_t n, size_t idx)
{
    size_t pnum = 0;
    for (size_t j = 0; j < n; j++) {
        if (defs[j].type == ARG_POSITIONAL) pnum++;
        if (j == idx) break;
    }
    return pnum;
}

/* Builder functions */

ArgParseErrCode add_kw_argument(ArgDef *slot,
                                const char *label,
                                bool has_abrv,
                                char overwrite_abrv,
                                ValueType type,
                                Value *out,
                                bool required,
                                ArgParseError *err)
{
    if (!slot || !label) {
        set_err(err, APERR_INVALID_ARG, "add_kw_argument: null slot or label");
        return APERR_INVALID_ARG;
    }
    slot->type = ARG_KEYWORD;
    slot->keyword.long_name = (char *)label;
    slot->keyword.has_abrv = has_abrv;
    slot->keyword.overwrite_abrv = overwrite_abrv;
    slot->keyword.type = type;
    slot->keyword.out = out;
    slot->keyword.required = required;
    if (err) set_err(err, APERR_OK, "ok");
    return APERR_OK;
}

ArgParseErrCode add_flag(ArgDef *slot,
                         const char *label,
                         bool has_abrv,
                         char overwrite_abrv,
                         bool *out,
                         bool required,
                         ArgParseError *err)
{
    if (!slot || !label) {
        set_err(err, APERR_INVALID_ARG, "add_flag: null slot or label");
        return APERR_INVALID_ARG;
    }
    slot->type = ARG_FLAG;
    slot->flag.long_name = (char *)label;
    slot->flag.has_abrv = has_abrv;
    slot->flag.overwrite_abrv = overwrite_abrv;
    slot->flag.out = out;
    slot->flag.required = required;
    if (err) set_err(err, APERR_OK, "ok");
    return APERR_OK;
}

ArgParseErrCode add_positional_argument(ArgDef *slot,
                                       ValueType type,
                                       Value *out,
                                       bool required,
                                       ArgParseError *err)
{
    if (!slot) {
        set_err(err, APERR_INVALID_ARG, "add_positional_argument: null slot");
        return APERR_INVALID_ARG;
    }
    slot->type = ARG_POSITIONAL;
    slot->positional.type = type;
    slot->positional.out = out;
    slot->positional.required = required;
    if (err) set_err(err, APERR_OK, "ok");
    return APERR_OK;
}

ArgParseErrCode add_flaglist(ArgDef *slot,
                             const char *label,
                             uint32_t *out,
                             bool required,
                             ArgParseError *err)
{
    if (!slot || !label) {
        set_err(err, APERR_INVALID_ARG, "add_flaglist: null slot or label");
        return APERR_INVALID_ARG;
    }
    slot->type = ARG_FLAG_LIST;
    slot->flag_list.count = 0;
    slot->flag_list.out = out;
    slot->flag_list.required = required;
    slot->flag_list.flags[0].long_name = NULL; /* ensure clear */
    /* store the label in the first element's long_name field to identify the list */
    slot->flag_list.flags[0].long_name = (char *)label;
    /* The actual flags start at index 0; we treat flag_list.flags[] normally */
    if (out) *out = 0;
    if (err) set_err(err, APERR_OK, "ok");
    return APERR_OK;
}

ArgParseErrCode add_flaglist_flag(ArgDef *defs,
                                  size_t ndefs,
                                  const char *flag_label,
                                  const char *list_label,
                                  bool has_abrv,
                                  char overwrite_abrv,
                                  ArgParseError *err)
{
    if (!defs || !flag_label || !list_label) {
        set_err(err, APERR_INVALID_ARG, "add_flaglist_flag: null parameter");
        return APERR_INVALID_ARG;
    }

    /* find the FlagList by matching its stored label. We used flags[0].long_name */
    for (size_t i = 0; i < ndefs; i++) {
        if (defs[i].type != ARG_FLAG_LIST) continue;
        const char *lbl = defs[i].flag_list.flags[0].long_name;
        if (!lbl) continue;
        if (strcmp(lbl, list_label) == 0) {
            uint32_t cnt = defs[i].flag_list.count;
            if (cnt >= 32) {
                set_err(err, APERR_OVERFLOW, "flaglist '%s' is full", list_label);
                return APERR_OVERFLOW;
            }
            /* Place new entry at index cnt (first real entry at 0) */
            defs[i].flag_list.flags[cnt].long_name = (char *)flag_label;
            defs[i].flag_list.flags[cnt].has_abrv = has_abrv;
            defs[i].flag_list.flags[cnt].overwrite_abrv = overwrite_abrv;
            defs[i].flag_list.count = cnt + 1;
            if (err) set_err(err, APERR_OK, "ok");
            return APERR_OK;
        }
    }

    set_err(err, APERR_NOT_FOUND, "flaglist '%s' not found", list_label);
    return APERR_NOT_FOUND;
}

/* Helper: valtype_name for help / messages (kept internal) */
static const char *valtype_name(ValueType t)
{
    switch (t) {
        case VAL_FLOAT:  return "float";
        case VAL_INT:    return "int";
        case VAL_CHAR:   return "char";
        case VAL_STRING: return "string";
        case VAL_BOOL:   return "bool";
    }
    return "?";
}

/* argparse_parse: no exits, no printing. Returns SDL_AppResult. */
SDL_AppResult argparse_parse(int argc, char **argv, const ArgDef *defs, size_t n,
                             ArgParseError *err)
{
    if (!argv || !defs) {
        set_err(err, APERR_INVALID_ARG, "argparse_parse: null argv or defs");
        return SDL_APP_FAILURE;
    }

    bool *matched = calloc(n, sizeof(bool));
    if (!matched) {
        set_err(err, APERR_OOM, "out of memory");
        return SDL_APP_FAILURE;
    }

    /* collect positional-def indices */
    size_t *pos_defs = malloc(n * sizeof(size_t));
    if (!pos_defs) {
        free(matched);
        set_err(err, APERR_OOM, "out of memory");
        return SDL_APP_FAILURE;
    }
    size_t pos_def_count = 0;
    for (size_t i = 0; i < n; i++) {
        if (defs[i].type == ARG_POSITIONAL)
            pos_defs[pos_def_count++] = i;
    }
    size_t pos_filled = 0;

    for (int ai = 1; ai < argc; ai++) {
        const char *tok = argv[ai];
        if (!tok) continue;

        /* built-in help: don't print here; return SDL_APP_EXIT so caller may exit(0) */
        if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
            free(matched);
            free(pos_defs);
            if (err) set_err(err, APERR_OK, "help requested");
            return SDL_APP_SUCCESS;
        }

        /* long option */
        if (tok[0] == '-' && tok[1] == '-') {
            const char *name_start = tok + 2;
            if (*name_start == '\0') {
                free(matched); free(pos_defs);
                set_err(err, APERR_INVALID_ARG, "bare '--' is not supported");
                return SDL_APP_FAILURE;
            }
            const char *eq = strchr(name_start, '=');
            size_t name_len = eq ? (size_t)(eq - name_start) : strlen(name_start);
            const char *inline_val = eq ? eq + 1 : NULL;

            bool found = false;
            for (size_t i = 0; i < n && !found; i++) {
                const ArgDef *d = &defs[i];
                switch (d->type) {
                    case ARG_KEYWORD: {
                        const KeywordArg *k = &d->keyword;
                        if (strncmp(k->long_name, name_start, name_len) == 0
                            && k->long_name[name_len] == '\0')
                        {
                            const char *valstr = inline_val;
                            if (!valstr) {
                                if (ai + 1 >= argc) {
                                    free(matched); free(pos_defs);
                                    set_err(err, APERR_OTHER, "--%s requires a <%s> value",
                                            k->long_name, valtype_name(k->type));
                                    return SDL_APP_FAILURE;
                                }
                                valstr = argv[++ai];
                            }
                            Value v;
                            if (parse_value_checked(valstr, k->type, &v, err) != 0) {
                                free(matched); free(pos_defs);
                                return SDL_APP_FAILURE;
                            }
                            if (k->out) *k->out = v;
                            matched[i] = true;
                            found = true;
                        }
                        break;
                    }
                    case ARG_FLAG: {
                        const Flag *f = &d->flag;
                        if (strncmp(f->long_name, name_start, name_len) == 0
                            && f->long_name[name_len] == '\0')
                        {
                            if (f->out) *f->out = true;
                            matched[i] = true;
                            found = true;
                        }
                        break;
                    }
                    case ARG_FLAG_LIST: {
                        const FlagList *fl = &d->flag_list;
                        for (uint32_t j = 0; j < fl->count && !found; j++) {
                            const FlagEntry *fe = &fl->flags[j];
                            if (strncmp(fe->long_name, name_start, name_len) == 0
                                && fe->long_name[name_len] == '\0')
                            {
                                if (fl->out) *fl->out |= (1u << j);
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
                free(matched); free(pos_defs);
                set_err(err, APERR_OTHER, "unknown option '--%.*s'", (int)name_len, name_start);
                return SDL_APP_FAILURE;
            }

        /* short single-character option: -x */
        } else if (tok[0] == '-' && tok[1] != '\0' && tok[2] == '\0') {
            char ab = tok[1];
            bool found = false;

            for (size_t i = 0; i < n && !found; i++) {
                const ArgDef *d = &defs[i];
                switch (d->type) {
                    case ARG_KEYWORD: {
                        const KeywordArg *k = &d->keyword;
                        char eff = effective_abrv(k->has_abrv, k->overwrite_abrv, k->long_name);
                        if (eff && eff == ab) {
                            if (ai + 1 >= argc) {
                                free(matched); free(pos_defs);
                                set_err(err, APERR_OTHER, "-%c requires a <%s> value",
                                        ab, valtype_name(k->type));
                                return SDL_APP_FAILURE;
                            }
                            Value v;
                            if (parse_value_checked(argv[++ai], k->type, &v, err) != 0) {
                                free(matched); free(pos_defs);
                                return SDL_APP_FAILURE;
                            }
                            if (k->out) *k->out = v;
                            matched[i] = true;
                            found = true;
                        }
                        break;
                    }
                    case ARG_FLAG: {
                        const Flag *f = &d->flag;
                        char eff = effective_abrv(f->has_abrv, f->overwrite_abrv, f->long_name);
                        if (eff && eff == ab) {
                            if (f->out) *f->out = true;
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
                                if (fl->out) *fl->out |= (1u << j);
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
                free(matched); free(pos_defs);
                set_err(err, APERR_OTHER, "unknown option '-%c'", ab);
                return SDL_APP_FAILURE;
            }

        /* positional */
        } else {
            if (pos_filled >= pos_def_count) {
                free(matched); free(pos_defs);
                set_err(err, APERR_OTHER, "unexpected positional argument '%s'", tok);
                return SDL_APP_FAILURE;
            }
            size_t di = pos_defs[pos_filled++];
            const PositionalArg *p = &defs[di].positional;
            Value v;
            if (parse_value_checked(tok, p->type, &v, err) != 0) {
                free(matched); free(pos_defs);
                return SDL_APP_FAILURE;
            }
            if (p->out) *p->out = v;
            matched[di] = true;
        }
    }

    /* required checks */
    for (size_t i = 0; i < n; i++) {
        if (matched[i]) continue;
        const ArgDef *d = &defs[i];
        switch (d->type) {
            case ARG_KEYWORD:
                if (d->keyword.required) {
                    free(matched); free(pos_defs);
                    set_err(err, APERR_OTHER, "required argument '--%s' was not provided",
                            d->keyword.long_name);
                    return SDL_APP_FAILURE;
                }
                break;
            case ARG_FLAG:
                if (d->flag.required) {
                    free(matched); free(pos_defs);
                    set_err(err, APERR_OTHER, "required flag '--%s' was not provided",
                            d->flag.long_name);
                    return SDL_APP_FAILURE;
                }
                break;
            case ARG_POSITIONAL:
                if (d->positional.required) {
                    size_t pnum = positional_index_of(defs, n, i);
                    free(matched); free(pos_defs);
                    set_err(err, APERR_OTHER, "required positional argument <%zu> was not provided",
                            pnum);
                    return SDL_APP_FAILURE;
                }
                break;
            case ARG_FLAG_LIST:
                if (d->flag_list.required && (d->flag_list.out == NULL || *d->flag_list.out == 0)) {
                    free(matched); free(pos_defs);
                    set_err(err, APERR_OTHER, "at least one flag from flag-group %zu must be provided",
                            i);
                    return SDL_APP_FAILURE;
                }
                break;
        }
    }

    free(matched);
    free(pos_defs);
    if (err) set_err(err, APERR_OK, "ok");
    return SDL_APP_CONTINUE;
}
