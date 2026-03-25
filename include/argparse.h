#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Value  —  typed scalar written to the caller's out-pointer on a match
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    VAL_FLOAT,
    VAL_INT,
    VAL_CHAR,
    VAL_STRING,  /* points into argv[] — valid for the program's lifetime */
    VAL_BOOL,
} ValueType;

typedef struct {
    ValueType type;
    union {
        float  f;
        int    i;
        char   c;
        char  *s;
        bool   b;
    } val;
} Value;

/* ═══════════════════════════════════════════════════════════════════════════
 * Argument-kind structs
 *
 * out-pointer contract
 * ────────────────────
 *  NULL      : parsed value is discarded; required still applies.
 *  non-NULL  : *out is overwritten on a successful match.
 *              Pre-initialise *out before calling argparse_parse() to set
 *              a default — the parser only writes on an explicit match.
 *
 * required
 * ────────
 *  true  : argument must appear on the command line; exit(1) otherwise.
 *  false : argument is optional.
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * KeywordArg  —  --name <value>  /  -a <value>
 *
 * type  : how the value string is parsed; must match the type of *out.
 * out   : Value*  (NULL = discard)
 */
typedef struct {
    char     *long_name;
    bool      has_abrv;
    char      overwrite_abrv;
    ValueType type;
    Value    *out;
    bool      required;
} KeywordArg;

/*
 * Flag  —  --name  /  -a
 *
 * out : bool*  set to true when the flag is present  (NULL = discard)
 *
 * required = true means the flag MUST be passed; useful for explicit
 * acknowledgement switches (e.g. --force).
 */
typedef struct {
    char *long_name;
    bool  has_abrv;
    char  overwrite_abrv;
    bool *out;
    bool  required;
} Flag;

/*
 * PositionalArg  —  bare token matched in definition order
 *
 * type : how the token is parsed; must match the type of *out.
 * out  : Value*  (NULL = discard)
 */
typedef struct {
    ValueType  type;
    Value     *out;
    bool       required;
} PositionalArg;

/*
 * FlagList  —  a group of up to 32 independent flags
 *
 * Result is a uint32_t bitmask: bit j is set when flags[j] was present.
 * count must be <= 32.
 * out  : uint32_t*  (NULL = discard)
 *
 * required = true means at least one flag in the group must be present.
 */
typedef struct {
    char *long_name;
    bool  has_abrv;
    char  overwrite_abrv;
} FlagEntry;

typedef struct {
    FlagEntry  flags[32];
    uint32_t   count;
    uint32_t  *out;
    bool       required;
} FlagList;

/* ═══════════════════════════════════════════════════════════════════════════
 * ArgDef  —  top-level discriminated union, one per argument slot
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ARG_KEYWORD,
    ARG_FLAG,
    ARG_POSITIONAL,
    ARG_FLAG_LIST,
} ArgType;

typedef struct {
    ArgType type;
    union {
        KeywordArg    keyword;
        Flag          flag;
        PositionalArg positional;
        FlagList      flag_list;
    };
} ArgDef;

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * argparse_parse  —  parse argc/argv against an array of argument definitions.
 *
 * @param argc  as received by main()
 * @param argv  as received by main()
 * @param defs  array of n argument definitions
 * @param n     number of elements in defs[]
 *
 * @return  0 on success.
 *          Never returns non-zero: all errors call exit(1).
 *
 * On a successful match the parsed value is written to the def's out-pointer
 * (if non-NULL).  Absent optional args leave their out-pointers untouched,
 * preserving any default the caller placed there beforehand.
 *
 * Special tokens
 *   --help / -h          : prints usage to stdout,  then exit(0)
 *   unknown option       : message to stderr,        then exit(1)
 *   required arg absent  : message to stderr,        then exit(1)
 */
int argparse_parse(int argc, char **argv, const ArgDef *defs, size_t n);

#endif /* ARGPARSE_H */
