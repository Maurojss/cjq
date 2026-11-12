#ifndef CJQ_QUERY_H
#define CJQ_QUERY_H

/*
 * query.h — cjq's query language: parser (text -> AST), evaluator (AST +
 * value -> results) and formatter (AST -> text) for pretty diagnostics.
 *
 * Query syntax:
 *
 *   $.a.b[2].c          path steps
 *   $.a[-1].b[1:3]      negative indexes and slices
 *   $."two words".a     quoted object keys
 *   $.a[*].b | length() wildcard + pipe
 *   $.a?                optional: emits null when .a is missing
 *   $..ids              recursive object-key search
 *   $.a, $.b            output both paths (comma alternatives)
 *   $.age > 21          comparisons (< <= == != >= >)
 *   select(.ok == true) comparisons inside builtin arguments
 *   $.a and $.b         boolean operators (and, or)
 *   $name               variable set by --arg / --argjson
 *   range(0; 5)         builtin arguments are ';'-separated
 *   path(.a[0])         location of a value
 *   del(.a[0])          copy without the selected path
 */

#include "json.h"

#include <stddef.h>

typedef enum
{
    K_ROOT,  /* '$'                                        */
    K_VAR,   /* $name — CLI variable                       */
    K_DOT,   /* '.' — identity                             */
    K_FIELD, /* .name                                      */
    K_IDX,   /* [n] or [query]                             */
    K_SLICE, /* [a:b]                                      */
    K_WILD,  /* [*] or []                                  */
    K_REC,   /* '..' — every descendant                    */
    K_RECF,  /* '..name' — descendants sitting under name  */
    K_OPT,   /* reserved; produced only by later versions  */
    K_ALT,   /* ',' — alternatives                         */
    K_PIPE,  /* '|' — pipeline                             */
    K_CALL,  /* name(args...)                              */
    K_NUM,   /* 42, -1.5, 1e3 — numeric literal            */
    K_LIT,   /* true / false / null                        */
    K_CMP,   /* left OP right; a holds the operator        */
    K_AND,   /* left 'and' right                           */
    K_OR,    /* left 'or' right                            */
    K_CONST  /* "string" literal (useful for has(), etc.)  */
} qkind;

typedef struct hnode hnode;
struct hnode
{
    qkind kind;
    char *name;   /* K_FIELD key, K_RECF key, K_CONST text, K_CALL builtin */
    hnode *kid;   /* K_OPT inner step, K_IDX query, K_CALL first arg,
                     K_ALT/K_PIPE first child, K_CMP left side */
    hnode *arg;   /* K_CALL next argument (arguments are ';'-separated) */
    hnode *next;  /* next step in the same chain; NULL for K_ALT/K_PIPE */
    hnode *alt;   /* sibling chains: K_ALT branches, K_PIPE stages */
    hnode *right; /* K_CMP right-hand side */
    long a, b;    /* K_IDX / K_SLICE bounds; LONG_MIN/MAX means "omitted";
                     K_CMP operator; K_LIT 0 null, 1 false, 2 true */
    double num;   /* K_NUM value */
    int opt;      /* '?' applies to this step */
};

/* Parse a query.  On failure returns NULL and sets *error (a static message)
 * plus *error_pos (an offset into text).  Successful parses yield an AST that
 * shares ownership with the small memory arena implicitly owned by the
 * process — cjq never frees query nodes except through q_free(). */
hnode *query_parse(const char *text, const char **error, size_t *error_pos);

/* Define a query variable such as $name.  The value is borrowed and must
 * outlive evaluation; later definitions of the same name win. */
void query_set_var(const char *name, json_value *value);

/* Render an AST back to text ("$.a.b[2]"); malloc'd, never NULL. */
char *query_format(const hnode *n);

/* Evaluate query n against val (the parsed document) and append every
 * produced value to r.  A results list is a growable vector of borrowed
 * json_value pointers. */
typedef struct
{
    size_t n, cap;
    json_value **v;
} results_t;

void query_eval(const hnode *n, json_value *val, results_t *r);
void results_push(results_t *r, json_value *v);

/* Helpers shared with tests: */
void results_init(results_t *r);
const char *query_kind_name(qkind k); /* "q-dot", "q-field", ... */
void q_free(hnode *n);                /* frees an AST (parser node kind only) */

#endif /* CJQ_QUERY_H */
