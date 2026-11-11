/*
 * json.c — JSON value model, parser and printer.  See json.h.
 */

#define _POSIX_C_SOURCE 200809L

#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -- allocation ---------------------------------------------------------- */

void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p)
    {
        fprintf(stderr, "cjq: out of memory\n");
        exit(70);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q)
    {
        fprintf(stderr, "cjq: out of memory\n");
        exit(70);
    }
    return q;
}

char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    return strcpy(p, s);
}

/* -- construction -------------------------------------------------------- */

json_value *json_null_new(void)
{
    json_value *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->kind = JSON_NULL;
    return v;
}

json_value *json_bool_new(int boolean)
{
    json_value *v = json_null_new();
    v->kind = boolean ? JSON_TRUE : JSON_FALSE;
    return v;
}

json_value *json_number_new(double number)
{
    json_value *v = json_null_new();
    v->kind = JSON_NUMBER;
    v->number = number;
    return v;
}

json_value *json_string_new(const char *text)
{
    json_value *v = json_null_new();
    if (text)
    {
        v->kind = JSON_STRING;
        v->string = xstrdup(text);
    }
    return v;
}

/* Actually build arrays/objects by overriding the kind of a generic value. */
static json_value *json_container_new(json_kind kind)
{
    json_value *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->kind = kind;
    return v;
}

json_value *json_array_new(void)
{
    return json_container_new(JSON_ARRAY);
}

void json_array_push(json_value *a, json_value *v)
{
    if (!a || a->kind != JSON_ARRAY || !v)
        return;
    if (a->count == a->capacity)
    {
        a->capacity = a->capacity ? 2 * a->capacity : 8;
        a->items = xrealloc(a->items, a->capacity * sizeof *a->items);
    }
    a->items[a->count++] = v;
}

json_value *json_object_new(void)
{
    return json_container_new(JSON_OBJECT);
}

void json_object_set(json_value *o, const char *key, json_value *v)
{
    if (!o || o->kind != JSON_OBJECT || !key || !v)
        return;
    for (size_t i = 0; i < o->count; ++i)
        if (strcmp(o->keys[i], key) == 0)
        {
            o->items[i] = v;
            return;
        }
    if (o->count == o->capacity)
    {
        o->capacity = o->capacity ? 2 * o->capacity : 8;
        o->keys = xrealloc(o->keys, o->capacity * sizeof *o->keys);
        o->items = xrealloc(o->items, o->capacity * sizeof *o->items);
    }
    o->keys[o->count] = xstrdup(key);
    o->items[o->count] = v;
    o->count++;
}

json_value *json_try_get(const json_value *o, const char *key)
{
    if (!o || o->kind != JSON_OBJECT || !key)
        return NULL;
    for (size_t i = 0; i < o->count; ++i)
        if (strcmp(o->keys[i], key) == 0)
            return o->items[i];
    return NULL;
}

json_value *json_try_copy(const json_value *v)
{
    if (!v)
        return NULL;
    switch (v->kind)
    {
    case JSON_ARRAY:
        {
            json_value *c = json_array_new();
            for (size_t i = 0; i < v->count; ++i)
                json_array_push(c, json_try_copy(v->items[i]));
            return c;
        }
    case JSON_OBJECT:
        {
            json_value *c = json_object_new();
            for (size_t i = 0; i < v->count; ++i)
                json_object_set(c, v->keys[i], json_try_copy(v->items[i]));
            return c;
        }
    default:
        if (v->kind == JSON_STRING)
            return json_string_new(v->string);
        if (v->kind == JSON_NUMBER)
            return json_number_new(v->number);
        if (v->kind == JSON_TRUE || v->kind == JSON_FALSE)
            return json_bool_new(v->kind == JSON_TRUE);
        return json_null_new();
    }
}

/* -- misc value helpers -------------------------------------------------- */

long json_number(const json_value *v)
{
    return v && v->kind == JSON_NUMBER ? (long)v->number : 0;
}

long json_strlen(const json_value *v)
{
    if (!v)
        return 0;
    if (v->kind == JSON_STRING)
        return (long)strlen(v->string);
    return (long)v->count;
}

json_value *json_try_get_num(json_value *a, size_t i)
{
    return a && (a->kind == JSON_ARRAY || a->kind == JSON_OBJECT) && i < a->count
               ? a->items[i]
               : NULL;
}

int json_truthy(const json_value *v)
{
    if (!v || v->kind == JSON_NULL || v->kind == JSON_FALSE)
        return 0;
    switch (v->kind)
    {
    case JSON_NUMBER:
        return v->number != 0;
    case JSON_STRING:
        return v->string && v->string[0] != '\0';
    case JSON_ARRAY:
    case JSON_OBJECT:
        return v->count != 0;
    default:
        return 1;
    }
}

/* -- parsing ------------------------------------------------------------- */

/* Deeply nested input would otherwise recurse until the stack dies.  JSON
 * hand-written by humans is nowhere near this; generated data can be. */
#define CJQ_MAX_DEPTH 512

typedef struct
{
    const char *cur;   /* read position */
    const char *start; /* buffer start  */
    const char *error; /* NULL when healthy */
    size_t error_pos;
    int depth;         /* current container nesting */
} parser_t;

static void perr_at(parser_t *p, const char *pos, const char *message)
{
    if (!p->error)
    {
        p->error = message;
        p->error_pos = (size_t)(pos - p->start);
    }
}

static void perr(parser_t *p, const char *message)
{
    perr_at(p, p->cur, message);
}

static void skip_spaces(parser_t *p)
{
    while (*p->cur == ' ' || *p->cur == '\t' || *p->cur == '\n' || *p->cur == '\r')
        p->cur++;
}

static int match_lit(parser_t *p, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(p->cur, lit, n) != 0)
        return 0;
    p->cur += n;
    return 1;
}

/* Growable byte buffer used to build strings. */
typedef struct
{
    char *data;
    size_t len, cap;
} strbuf_t;

static void strbuf_push(strbuf_t *b, char c)
{
    if (b->len == b->cap)
    {
        b->cap = b->cap ? 2 * b->cap : 16;
        b->data = xrealloc(b->data, b->cap);
    }
    b->data[b->len++] = c;
}

static uint32_t read_hex4(parser_t *p, uint32_t *out)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
    {
        char c = p->cur[i];
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            return 0;
        value = value * 16 + d;
    }
    p->cur += 4;
    *out = value;
    return 1;
}

static void utf8_encode(strbuf_t *b, uint32_t cp)
{
    if (cp < 0x80)
        strbuf_push(b, (char)cp);
    else if (cp < 0x800)
    {
        strbuf_push(b, (char)(0xC0 | (cp >> 6)));
        strbuf_push(b, (char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        strbuf_push(b, (char)(0xE0 | (cp >> 12)));
        strbuf_push(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        strbuf_push(b, (char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        strbuf_push(b, (char)(0xF0 | (cp >> 18)));
        strbuf_push(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        strbuf_push(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        strbuf_push(b, (char)(0x80 | (cp & 0x3F)));
    }
}

/* p->cur points at the opening '"'.  Returns a malloc'd string or NULL on
 * error. */
static char *parse_string(parser_t *p)
{
    strbuf_t b = {NULL, 0, 0};
    p->cur++; /* opening quote */
    for (;;)
    {
        if (*p->cur == '"')
            break;
        if (*p->cur == '\0')
        {
            perr(p, "unterminated string");
            return NULL;
        }
        if (*p->cur == '\\')
        {
            p->cur++;
            switch (*p->cur)
            {
            case '"': strbuf_push(&b, '"'); break;
            case '\\': strbuf_push(&b, '\\'); break;
            case '/': strbuf_push(&b, '/'); break;
            case 'b': strbuf_push(&b, '\b'); break;
            case 'f': strbuf_push(&b, '\f'); break;
            case 'n': strbuf_push(&b, '\n'); break;
            case 'r': strbuf_push(&b, '\r'); break;
            case 't': strbuf_push(&b, '\t'); break;
            case 'u':
            {
                uint32_t high;
                p->cur++; /* skip the 'u'; read_hex4 expects the digits */
                if (!read_hex4(p, &high))
                {
                    perr(p, "expected 4 hex digits after \\u");
                    return NULL;
                }
                if (high >= 0xD800 && high <= 0xDBFF)
                {
                    if (!(p->cur[0] == '\\' && p->cur[1] == 'u'))
                    {
                        perr(p, "unpaired high surrogate");
                        return NULL;
                    }
                    p->cur += 2;
                    uint32_t low;
                    if (!read_hex4(p, &low))
                    {
                        perr(p, "expected 4 hex digits after \\u");
                        return NULL;
                    }
                    if (low < 0xDC00 || low > 0xDFFF)
                    {
                        perr(p, "unpaired high surrogate");
                        return NULL;
                    }
                    high = 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (high >= 0xDC00 && high <= 0xDFFF)
                {
                    perr(p, "unpaired low surrogate");
                    return NULL;
                }
                utf8_encode(&b, high);
                continue; /* cursor already past the escape */
            }
            default:
                perr(p, "invalid escape sequence");
                return NULL;
            }
            p->cur++;
        }
        else
        {
            strbuf_push(&b, *p->cur++);
        }
    }
    p->cur++; /* closing quote */
    strbuf_push(&b, '\0');
    return b.data;
}

/* Validates the JSON number grammar and returns the double.  p->cur points
 * at the first character. */
static int parse_number(parser_t *p, json_value *v)
{
    const char *c = p->cur;
    const char *s = c;
    if (*s == '-')
        s++;
    if (*s < '0' || *s > '9')
    {
        perr(p, "malformed number");
        return 0;
    }
    if (s[0] == '0' && s[1] >= '0' && s[1] <= '9')
        return (perr(p, "leading zeros in numbers"), 0);
    while (*s >= '0' && *s <= '9')
        s++;
    if (*s == '.')
    {
        s++;
        if (*s < '0' || *s > '9')
            return (perr(p, "malformed number"), 0);
        while (*s >= '0' && *s <= '9')
            s++;
    }
    if (*s == 'e' || *s == 'E')
    {
        s++;
        if (*s == '+' || *s == '-')
            s++;
        if (*s < '0' || *s > '9')
            return (perr(p, "malformed number"), 0);
        while (*s >= '0' && *s <= '9')
            s++;
    }
    char buf[512];
    size_t len = (size_t)(s - c);
    if (len >= sizeof buf)
        return (perr(p, "number too long"), 0);
    memcpy(buf, c, len);
    buf[len] = '\0';
    errno = 0;
    double d = strtod(buf, NULL);
    /* Underflow to zero (or to a subnormal) is still valid JSON; only
     * overflow to infinity is a hard error. */
    if (errno == ERANGE && isinf(d))
        return (perr(p, "number out of range"), 0);
    p->cur = s;
    v->kind = JSON_NUMBER;
    v->number = d;
    return 1;
}

static json_value *parse_value(parser_t *p);

static json_value *parse_array(parser_t *p)
{
    json_value *a = json_array_new();
    p->cur++; /* '[' */
    skip_spaces(p);
    if (*p->cur == ']')
    {
        p->cur++;
        return a;
    }
    for (;;)
    {
        skip_spaces(p);
        json_value *item = parse_value(p);
        if (!item)
            return NULL;
        json_array_push(a, item);
        skip_spaces(p);
        if (*p->cur == ',')
        {
            p->cur++;
            continue;
        }
        if (*p->cur == ']')
        {
            p->cur++;
            return a;
        }
        perr(p, "expected ',' or ']'");
        return NULL;
    }
}

static json_value *parse_object(parser_t *p)
{
    json_value *o = json_object_new();
    p->cur++; /* '{' */
    skip_spaces(p);
    if (*p->cur == '}')
    {
        p->cur++;
        return o;
    }
    for (;;)
    {
        skip_spaces(p);
        if (*p->cur != '"')
            return (perr(p, "expected object key"), NULL);
        char *key = parse_string(p);
        if (!key)
            return NULL;
        skip_spaces(p);
        if (*p->cur != ':')
            return (perr(p, "expected ':'"), NULL);
        p->cur++;
        skip_spaces(p);
        json_value *item = parse_value(p);
        if (!item)
            return NULL;
        json_object_set(o, key, item);
        skip_spaces(p);
        if (*p->cur == ',')
        {
            p->cur++;
            continue;
        }
        if (*p->cur == '}')
        {
            p->cur++;
            return o;
        }
        perr(p, "expected ',' or '}'");
        return NULL;
    }
}

static json_value *parse_value(parser_t *p)
{
    skip_spaces(p);
    json_value *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    switch (*p->cur)
    {
    case 'n':
        if (!match_lit(p, "null"))
            return (perr(p, "invalid literal"), NULL);
        v->kind = JSON_NULL;
        break;
    case 't':
        if (!match_lit(p, "true"))
            return (perr(p, "invalid literal"), NULL);
        v->kind = JSON_TRUE;
        break;
    case 'f':
        if (!match_lit(p, "false"))
            return (perr(p, "invalid literal"), NULL);
        v->kind = JSON_FALSE;
        break;
    case '"':
    {
        char *s = parse_string(p);
        if (!s)
            return NULL;
        v->kind = JSON_STRING;
        v->string = s;
        break;
    }
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (!parse_number(p, v))
            return NULL;
        break;
    case '[':
    case '{':
    {
        if (p->depth >= CJQ_MAX_DEPTH)
            return (perr(p, "nesting too deep"), NULL);
        p->depth++;
        json_value *nested =
            *p->cur == '[' ? parse_array(p) : parse_object(p);
        p->depth--;
        return nested;
    }
    default:
        if (*p->cur == '\0')
            perr(p, "unexpected end of input");
        else
            perr(p, "unexpected character");
        return NULL;
    }
    return v;
}

json_value *json_parse(const char *text, const char **end, const char **error,
                       size_t *error_pos)
{
    parser_t p = {text, text, NULL, 0, 0};
    json_value *v = parse_value(&p);
    if (p.error)
    {
        if (error)
            *error = p.error;
        if (error_pos)
            *error_pos = p.error_pos;
        return NULL;
    }
    if (end)
        *end = p.cur;
    if (error)
        *error = NULL;
    if (error_pos)
        *error_pos = 0;
    return v;
}

void json_pos(const char *text, size_t pos, long *line, long *column)
{
    *line = 1;
    *column = 1;
    for (size_t i = 0; i < pos && text[i] != '\0'; ++i)
    {
        if (text[i] == '\n')
        {
            (*line)++;
            *column = 1;
        }
        else
            (*column)++;
    }
}

/* -- printing ------------------------------------------------------------ */

#define CJQ_COLOR_KEY "\033[36m"
#define CJQ_COLOR_STRING "\033[32m"
#define CJQ_COLOR_NUMBER "\033[35m"
#define CJQ_COLOR_BOOL "\033[33m"
#define CJQ_COLOR_NULL "\033[34m"
#define CJQ_COLOR_RESET "\033[0m"

static int g_colors = 0;
static int g_ascii = 0;
static int g_sort_keys = 0;
static char g_indent[16] = "    ";

void json_set_colors(int on)
{
    g_colors = on ? 1 : 0;
}

int json_colors_on(void)
{
    return g_colors;
}

void json_set_ascii(int on)
{
    g_ascii = on ? 1 : 0;
}

void json_set_indent(const char *unit)
{
    if (!unit)
        unit = "";
    size_t n = strlen(unit);
    if (n >= sizeof g_indent)
        n = sizeof g_indent - 1;
    memcpy(g_indent, unit, n);
    g_indent[n] = '\0';
}

void json_set_sort_keys(int on)
{
    g_sort_keys = on ? 1 : 0;
}

void json_format_number(double number, char buf[40])
{
    for (int precision = 1; precision < 17; ++precision)
    {
        snprintf(buf, 40, "%.*g", precision, number);
        if (strtod(buf, NULL) != number)
            continue;

        /* The shortest round-tripping form may use an exponent even for
         * values that read better in plain decimal (1e2 -> 100).  Rewrite
         * those when the exponent is in the range where fixed notation
         * stays short; keep scientific form outside it, like jq does. */
        const char *e = strchr(buf, 'e');
        if (e)
        {
            int exp = atoi(e + 1);
            if (exp >= -4 && exp <= 20)
            {
                int decimals = precision - 1 - exp;
                if (decimals < 0)
                    decimals = 0;
                if (decimals <= 20)
                    snprintf(buf, 40, "%.*f", decimals, number);
            }
        }
        return;
    }
    snprintf(buf, 40, "%.17g", number);
}

static void put_colored(FILE *out, const char *color, const char *text)
{
    if (g_colors)
        fputs(color, out);
    fputs(text, out);
    if (g_colors)
        fputs(CJQ_COLOR_RESET, out);
}

static void put_indent(FILE *out, int depth)
{
    for (int i = 0; i < depth; ++i)
        fputs(g_indent, out);
}

/* Decode one UTF-8 sequence at s.  Returns its length, or 0 when the bytes
 * are not valid UTF-8. */
static int utf8_next(const unsigned char *s, uint32_t *cp)
{
    unsigned char c = s[0];
    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }

    int len;
    uint32_t value;
    if ((c & 0xE0) == 0xC0)
    {
        len = 2;
        value = c & 0x1Fu;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        len = 3;
        value = c & 0x0Fu;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        len = 4;
        value = c & 0x07u;
    }
    else
        return 0;

    for (int i = 1; i < len; ++i)
    {
        if ((s[i] & 0xC0) != 0x80)
            return 0;
        value = (value << 6) | (s[i] & 0x3Fu);
    }

    /* Reject overlong encodings, surrogates and out-of-range values. */
    if ((len == 2 && value < 0x80) || (len == 3 && value < 0x800) ||
        (len == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
        return 0;

    *cp = value;
    return len;
}

/* Prints a JSON string with quotes; color wraps the whole literal when
 * color != NULL. */
static void print_quoted(const char *s, FILE *out, const char *color)
{
    if (color && g_colors)
        fputs(color, out);
    fputc('"', out);
    const unsigned char *c = (const unsigned char *)s;
    while (*c)
    {
        switch (*c)
        {
        case '"': fputs("\\\"", out); c++; break;
        case '\\': fputs("\\\\", out); c++; break;
        case '\b': fputs("\\b", out); c++; break;
        case '\f': fputs("\\f", out); c++; break;
        case '\n': fputs("\\n", out); c++; break;
        case '\r': fputs("\\r", out); c++; break;
        case '\t': fputs("\\t", out); c++; break;
        default:
            if (*c < 0x20)
            {
                fprintf(out, "\\u%04X", (unsigned)*c);
                c++;
            }
            else if (g_ascii && *c >= 0x80)
            {
                uint32_t cp;
                int len = utf8_next(c, &cp);
                if (len == 0)
                {
                    cp = 0xFFFD;
                    len = 1;
                }
                if (cp >= 0x10000)
                {
                    uint32_t v = cp - 0x10000;
                    fprintf(out, "\\u%04x\\u%04x",
                            (unsigned)(0xD800 + (v >> 10)),
                            (unsigned)(0xDC00 + (v & 0x3FF)));
                }
                else
                    fprintf(out, "\\u%04x", (unsigned)cp);
                c += len;
            }
            else
            {
                fputc(*c, out);
                c++;
            }
        }
    }
    fputc('"', out);
    if (color && g_colors)
        fputs(CJQ_COLOR_RESET, out);
}

static void print_value(const json_value *v, FILE *out, int pretty, int depth);

static void print_array(const json_value *a, FILE *out, int pretty, int depth)
{
    if (!a->count)
    {
        fputs("[]", out);
        return;
    }
    fputc('[', out);
    for (size_t i = 0; i < a->count; ++i)
    {
        if (i)
            fputc(',', out);
        if (pretty)
        {
            fputc('\n', out);
            put_indent(out, depth + 1);
        }
        print_value(a->items[i], out, pretty, depth + 1);
    }
    if (pretty)
    {
        fputc('\n', out);
        put_indent(out, depth);
    }
    fputc(']', out);
}

static int cmp_key_ptrs(const void *pa, const void *pb)
{
    const char *const *a = pa;
    const char *const *b = pb;
    return strcmp(*a, *b);
}

static void print_member(const char *key, const json_value *val, FILE *out,
                         int pretty, int depth, int first)
{
    if (!first)
        fputc(',', out);
    if (pretty)
    {
        fputc('\n', out);
        put_indent(out, depth + 1);
    }
    print_quoted(key, out, CJQ_COLOR_KEY);
    fputc(':', out);
    if (pretty)
        fputc(' ', out);
    print_value(val, out, pretty, depth + 1);
}

static void print_object(const json_value *o, FILE *out, int pretty, int depth)
{
    if (!o->count)
    {
        fputs("{}", out);
        return;
    }
    fputc('{', out);
    if (g_sort_keys)
    {
        const char **keys = xmalloc(o->count * sizeof *keys);
        for (size_t i = 0; i < o->count; ++i)
            keys[i] = o->keys[i];
        qsort(keys, o->count, sizeof *keys, cmp_key_ptrs);
        for (size_t i = 0; i < o->count; ++i)
            print_member(keys[i], json_try_get(o, keys[i]), out, pretty, depth,
                         i == 0);
        free(keys);
    }
    else
    {
        for (size_t i = 0; i < o->count; ++i)
            print_member(o->keys[i], o->items[i], out, pretty, depth, i == 0);
    }
    if (pretty)
    {
        fputc('\n', out);
        put_indent(out, depth);
    }
    fputc('}', out);
}

static void print_value(const json_value *v, FILE *out, int pretty, int depth)
{
    switch (v->kind)
    {
    case JSON_NULL:
        put_colored(out, CJQ_COLOR_NULL, "null");
        break;
    case JSON_TRUE:
        put_colored(out, CJQ_COLOR_BOOL, "true");
        break;
    case JSON_FALSE:
        put_colored(out, CJQ_COLOR_BOOL, "false");
        break;
    case JSON_NUMBER:
        {
            char buf[40];
            json_format_number(v->number, buf);
            put_colored(out, CJQ_COLOR_NUMBER, buf);
        }
        break;
    case JSON_STRING:
        print_quoted(v->string, out, CJQ_COLOR_STRING);
        break;
    case JSON_ARRAY:
        print_array(v, out, pretty, depth);
        break;
    case JSON_OBJECT:
        print_object(v, out, pretty, depth);
        break;
    }
}

void json_print_ex(const json_value *v, FILE *out, int pretty, int newline)
{
    if (!v)
        return;
    print_value(v, out, pretty, 0);
    if (newline)
        fputc('\n', out);
}

void json_print(const json_value *v, FILE *out, int pretty)
{
    json_print_ex(v, out, pretty, 1);
}

json_value *json_value_to_string(const json_value *v)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f)
        return NULL;

    int colors = g_colors;
    int ascii = g_ascii;
    int sort_keys = g_sort_keys;
    g_colors = 0;
    g_ascii = 0;
    g_sort_keys = 0;
    json_print_ex(v, f, 0, 0);
    g_colors = colors;
    g_ascii = ascii;
    g_sort_keys = sort_keys;

    if (fclose(f) != 0)
    {
        free(buf);
        return NULL;
    }
    json_value *s = json_string_new(buf ? buf : "");
    free(buf);
    return s;
}
