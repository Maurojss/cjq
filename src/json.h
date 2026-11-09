#ifndef CJQ_JSON_H
#define CJQ_JSON_H

/*
 * json.h — self-contained JSON value model, recursive-descent parser and
 * printer for cjq.  Owns the data it allocates; deliberately never frees
 * (cjq is a one-shot CLI, the OS reclaims memory on exit).
 */

#include <stddef.h>
#include <stdio.h>

typedef enum
{
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_kind;

typedef struct json_value json_value;

struct json_value
{
    json_kind kind;
    double number;       /* JSON_NUMBER           */
    char *string;        /* JSON_STRING           */
    char **keys;         /* JSON_OBJECT names     */
    size_t count;        /* array/object length   */
    size_t capacity;     /* malloc room           */
    json_value **items;  /* array items; object values parallel to keys */
};

/* --- allocation --------------------------------------------------------- */

/* printf-neutrality: they abort the process on OOM; cjq treats memory as
 * cheap because values are never freed. */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* --- construction ------------------------------------------------------- */

json_value *json_null_new(void);
json_value *json_bool_new(int boolean);
json_value *json_number_new(double number);
json_value *json_string_new(const char *text); /* copies */
json_value *json_array_new(void);
/* Append v to a; nothing on v == NULL. */
void json_array_push(json_value *a, json_value *v);
json_value *json_object_new(void);
/* Sets key to v, copying key.  Later identical keys replace earlier ones but
 * keep their original position.  Nothing on v == NULL. */
void json_object_set(json_value *o, const char *key, json_value *v);
json_value *json_try_get(const json_value *o, const char *key); /* NULL if absent */

/* Deep-copy construction of the source for value sources used by map. */
json_value *json_try_copy(const json_value *v);

/* --- parsing ------------------------------------------------------------ */

/* Parse a single JSON value from text.  On success *end (if not NULL) points
 * just after the value.  On failure returns NULL, *error (if not NULL) points
 * to a static message and *error_pos (if not NULL) to the offending byte
 * offset within text. */
json_value *json_parse(const char *text, const char **end, const char **error,
                       size_t *error_pos);

/* Turn an offset inside text into a 1-based line/column pair. */
void json_pos(const char *text, size_t pos, long *line, long *column);

/* --- printing ----------------------------------------------------------- */

/* Color management is global because the printer is recursive. */
void json_set_colors(int on);
int json_colors_on(void);

/* Escape non-ASCII bytes as \uXXXX (and surrogate pairs) when on. */
void json_set_ascii(int on);

/* Indentation unit used by pretty printing; defaults to four spaces.
 * The string is copied, so it need not outlive the call. */
void json_set_indent(const char *unit);

/* Sort object members by key while printing. */
void json_set_sort_keys(int on);

/* pretty != 0 pretty-prints with the configured indent, otherwise the
 * output is compact single-line.  Colors follow json_set_colors(). */
void json_print(const json_value *v, FILE *out, int pretty);

/* Like json_print(), but newline == 0 omits the trailing newline. */
void json_print_ex(const json_value *v, FILE *out, int pretty, int newline);

/* Shortest round-trip representation of a double ("1" for 1.0). */
void json_format_number(double number, char buf[40]);

long json_number(const json_value *v); /* truncates toward zero */
long json_strlen(const json_value *v); /* byte length or array/object count */
json_value *json_try_get_num(json_value *a, size_t i);

/* cjq truthiness: null, false, 0, "", [] and {} are falsy. */
int json_truthy(const json_value *v);

#endif /* CJQ_JSON_H */
