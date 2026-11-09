/*
 * query.c — cjq's query language: parser (text -> AST), formatter (AST ->
 * text) and evaluator (AST + document -> results).  See query.h for the
 * accepted syntax.
 */

#include "query.h"

#include <limits.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Comparison operators stored in hnode.a for K_CMP. */
enum
{
    CMP_EQ,
    CMP_NE,
    CMP_LT,
    CMP_LE,
    CMP_GT,
    CMP_GE
};

/* ----------------------------- results ---------------------------------- */

void results_init(results_t *r)
{
    r->n = 0;
    r->cap = 0;
    r->v = NULL;
}

void results_push(results_t *r, json_value *v)
{
    if (r->n == r->cap)
    {
        r->cap = r->cap ? 2 * r->cap : 8;
        r->v = xrealloc(r->v, r->cap * sizeof *r->v);
    }
    r->v[r->n++] = v;
}

static void results_absorb(results_t *dst, results_t *src)
{
    for (size_t i = 0; i < src->n; ++i)
        results_push(dst, src->v[i]);
    src->n = 0;
}

/* ----------------------- growable string buffer ------------------------- */

typedef struct
{
    char *data;
    size_t len, cap;
} sbuf_t;

static void sbuf_push(sbuf_t *b, char c)
{
    if (b->len == b->cap)
    {
        b->cap = b->cap ? 2 * b->cap : 16;
        b->data = xrealloc(b->data, b->cap);
    }
    b->data[b->len++] = c;
}

static void sbuf_text(sbuf_t *b, const char *s)
{
    while (*s)
        sbuf_push(b, *s++);
}

/* ------------------------------ parser ---------------------------------- */

typedef struct
{
    const char *cur;
    const char *start;
    const char *error;
    size_t error_pos;
} qparser_t;

static void qerr_at(qparser_t *p, const char *at, const char *message)
{
    if (!p->error)
    {
        p->error = message;
        p->error_pos = (size_t)(at - p->start);
    }
}

static void qerr(qparser_t *p, const char *message)
{
    qerr_at(p, p->cur, message);
}

static void skip_blanks(qparser_t *p)
{
    while (*p->cur == ' ' || *p->cur == '\t' || *p->cur == '\n' || *p->cur == '\r')
        p->cur++;
}

/* Accept c, skipping blanks in front of it. */
static int q_accept(qparser_t *p, char c)
{
    skip_blanks(p);
    if (*p->cur == c)
    {
        p->cur++;
        return 1;
    }
    return 0;
}

static int is_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_name_char(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-';
}

static int is_digit_ch(char c)
{
    return c >= '0' && c <= '9';
}

/* A nonzero 'fail' signals that the caller must return NULL. */
typedef struct
{
    int fail;
    char *data;
} str_result_t;

static str_result_t parse_quoted_fail(qparser_t *p, const char *message)
{
    str_result_t r = {1, NULL};
    qerr(p, message);
    return r;
}

/* p->cur at the opening '"'. */
static str_result_t parse_quoted(qparser_t *p)
{
    sbuf_t b = {NULL, 0, 0};
    p->cur++;
    for (;;)
    {
        char c = *p->cur;
        if (c == '"')
        {
            p->cur++;
            break;
        }
        if (c == '\0')
            return parse_quoted_fail(p, "unterminated string");
        if (c == '\\')
        {
            p->cur++;
            switch (*p->cur)
            {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default:
                sbuf_push(&b, '\0');
                return parse_quoted_fail(p, "invalid escape in string");
            }
            p->cur++;
        }
        sbuf_push(&b, c);
        p->cur++;
    }
    sbuf_push(&b, '\0');
    {
        str_result_t ok = {0, b.data};
        return ok;
    }
}

/* p->cur at the first name character. */
static char *parse_name(qparser_t *p)
{
    const char *begin = p->cur;
    while (is_name_char(*p->cur))
        p->cur++;
    size_t len = (size_t)(p->cur - begin);
    char *s = xmalloc(len + 1);
    memcpy(s, begin, len);
    s[len] = '\0';
    return s;
}

/* p->cur at ']'... Rather: at '-' or a digit. */
static long parse_long(qparser_t *p)
{
    const char *begin = p->cur;
    if (*p->cur == '-')
        p->cur++;
    if (!is_digit_ch(*p->cur))
        return (qerr(p, "expected a number"), 0);
    while (is_digit_ch(*p->cur))
        p->cur++;
    size_t len = (size_t)(p->cur - begin);
    if (len > 18)
        return (qerr(p, "number too long"), 0);
    char buf[24];
    memcpy(buf, begin, len);
    buf[len] = '\0';
    return strtol(buf, NULL, 10);
}

static hnode *qnode(qkind k)
{
    hnode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = k;
    n->a = LONG_MIN;  /* "omitted" for K_SLICE, irrelevant elsewhere */
    n->b = LONG_MAX;  /* "omitted" for K_SLICE, irrelevant elsewhere */
    return n;
}

/* Forward declarations for the mutually recursive little parser. */
static hnode *parse_pipe(qparser_t *p);

/* 0 = unknown, 1 = takes no argument, 2 = requires one, 3 = optional. */
static int builtin_kind(const char *name)
{
    static const char *const no_args[] = {
        "length", "values", "type", "first", "last", "add",
        "floor", "ceil", "abs", "sqrt", "ftos", "not",
        "unique", "flatten", "to_entries", "from_entries", "reverse", NULL};
    static const char *const required[] = {
        "has", "select", "map", "contains", "startswith", "endswith", "join",
        "split", "group_by", "test", NULL};
    static const char *const optional[] = {"keys", "sort", "max", "min", "sum",
                                           NULL};

    for (const char *const *p = no_args; *p; ++p)
        if (!strcmp(name, *p))
            return 1;
    for (const char *const *p = required; *p; ++p)
        if (!strcmp(name, *p))
            return 2;
    for (const char *const *p = optional; *p; ++p)
        if (!strcmp(name, *p))
            return 3;
    return 0;
}

/* p->cur at the '(' — takes ownership of name. */
static hnode *parse_call(qparser_t *p, char *name)
{
    hnode *n = qnode(K_CALL);
    n->name = name;
    p->cur++; /* '(' */
    skip_blanks(p);
    if (*p->cur != ')')
    {
        hnode *first = parse_pipe(p);
        if (!first)
            return NULL;
        n->kid = first;
        hnode *last = first;
        while (q_accept(p, ','))
        {
            hnode *another = parse_pipe(p);
            if (!another)
                return NULL;
            last->alt = another;
            last = another;
        }
        if (!q_accept(p, ')'))
            return (qerr(p, "expected ')'"), NULL);
    }
    else
        p->cur++;

    switch (builtin_kind(name))
    {
    case 0:
        return (qerr(p, "unknown builtin function"), NULL);
    case 1:
        if (n->kid)
            return (qerr(p, "builtin takes no arguments"), NULL);
        break;
    case 2:
        if (!n->kid)
            return (qerr(p, "builtin requires an argument"), NULL);
        break;
    case 3:
        break;
    default:
        break;
    }
    return n;
}

/* p->cur at '['. */
static hnode *parse_bracket(qparser_t *p)
{
    p->cur++; /* '[' */
    skip_blanks(p);
    if (*p->cur == ']')
    {
        p->cur++;
        return qnode(K_WILD);
    }
    if (*p->cur == '*')
    {
        p->cur++;
        if (!q_accept(p, ']'))
            return (qerr(p, "expected ']'"), NULL);
        return qnode(K_WILD);
    }
    if (*p->cur == '"')
    {
        str_result_t r = parse_quoted(p);
        if (r.fail)
            return NULL;
        if (!q_accept(p, ']'))
            return (qerr(p, "expected ']'"), NULL);
        hnode *n = qnode(K_FIELD);
        n->name = r.data;
        return n;
    }
    if (*p->cur == ':') /* slice without lower bound */
    {
        p->cur++;
        hnode *n = qnode(K_SLICE);
        skip_blanks(p);
        if (is_digit_ch(*p->cur) || (*p->cur == '-' && is_digit_ch(p->cur[1])))
            n->b = parse_long(p);
        if (!q_accept(p, ']'))
            return (qerr(p, "expected ']'"), NULL);
        return n;
    }
    if (is_digit_ch(*p->cur) || (*p->cur == '-' && is_digit_ch(p->cur[1])))
    {
        long index = parse_long(p);
        if (!q_accept(p, ':'))
        {
            if (!q_accept(p, ']'))
                return (qerr(p, "expected ']'"), NULL);
            hnode *n = qnode(K_IDX);
            n->a = index;
            return n;
        }
        hnode *n = qnode(K_SLICE);
        n->a = index;
        skip_blanks(p);
        if (is_digit_ch(*p->cur) || (*p->cur == '-' && is_digit_ch(p->cur[1])))
            n->b = parse_long(p);
        if (!q_accept(p, ']'))
            return (qerr(p, "expected ']'"), NULL);
        return n;
    }
    /* dynamic index: a full nested query */
    hnode *kid = parse_pipe(p);
    if (!kid)
        return NULL;
    if (!q_accept(p, ']'))
        return (qerr(p, "expected ']'"), NULL);
    hnode *n = qnode(K_IDX);
    n->kid = kid;
    return n;
}

static hnode *parse_step(qparser_t *p)
{
    skip_blanks(p);
    char c = *p->cur;
    if (c == '$')
    {
        p->cur++;
        return qnode(K_ROOT);
    }
    if (c == '.')
    {
        p->cur++;
        char d = *p->cur; /* blanks are NOT allowed between '.' and the name */
        if (d == '.')
        {
            p->cur++;
            if (is_name_start(*p->cur))
            {
                hnode *n = qnode(K_RECF);
                n->name = parse_name(p);
                return n;
            }
            return qnode(K_REC);
        }
        if (d == '"')
        {
            str_result_t r = parse_quoted(p);
            if (r.fail)
                return NULL;
            hnode *n = qnode(K_FIELD);
            n->name = r.data;
            return n;
        }
        if (is_name_start(d))
        {
            char *name = parse_name(p);
            if (*p->cur == '(')
                return parse_call(p, name);
            hnode *n = qnode(K_FIELD);
            n->name = name;
            return n;
        }
        return qnode(K_DOT);
    }
    if (c == '[')
        return parse_bracket(p);
    if (c == '"')
    {
        str_result_t r = parse_quoted(p);
        if (r.fail)
            return NULL;
        hnode *n = qnode(K_CONST);
        n->name = r.data;
        return n;
    }
    if (is_digit_ch(c) || (c == '-' && is_digit_ch(p->cur[1])))
    {
        char *end = NULL;
        double d = strtod(p->cur, &end);
        if (end == p->cur)
            return (qerr(p, "malformed number literal"), NULL);
        p->cur = end;
        hnode *n = qnode(K_NUM);
        n->num = d;
        return n;
    }
    if (is_name_start(c))
    {
        static const struct
        {
            const char *word;
            long literal; /* 0 null, 1 false, 2 true */
        } literals[] = {{"null", 0}, {"false", 1}, {"true", 2}};
        for (size_t i = 0; i < sizeof literals / sizeof literals[0]; ++i)
        {
            size_t len = strlen(literals[i].word);
            if (!strncmp(p->cur, literals[i].word, len) &&
                !is_name_char(p->cur[len]))
            {
                p->cur += len;
                hnode *n = qnode(K_LIT);
                n->a = literals[i].literal;
                return n;
            }
        }
        char *name = parse_name(p);
        if (*p->cur != '(')
            return (qerr(p, "expected '(' after name"), NULL);
        return parse_call(p, name);
    }
    if (c == '\0')
        return (qerr(p, "unexpected end of query"), NULL);
    return (qerr(p, "unexpected character in query"), NULL);
}

/* Whole-word match for the boolean keywords 'and' and 'or'. */
static int keyword_at(const qparser_t *p, const char *word)
{
    size_t len = strlen(word);
    return !strncmp(p->cur, word, len) && !is_name_char(p->cur[len]);
}

/* A chain of steps ('' means stop: ',' '|' ')' ']' '}' or end of input). */
static hnode *parse_alt(qparser_t *p)
{
    hnode *head = NULL;
    hnode *tail = NULL;
    for (;;)
    {
        skip_blanks(p);
        char c = *p->cur;
        if (c == '\0' || c == ',' || c == '|' || c == ')' || c == ']' ||
            c == '}' || c == '<' || c == '>' || c == '=' || c == '!')
            break;
        if (is_name_start(c) && (keyword_at(p, "and") || keyword_at(p, "or")))
            break;
        hnode *step = parse_step(p);
        if (!step)
            return NULL;
        skip_blanks(p);
        if (*p->cur == '?')
        {
            p->cur++;
            step->opt = 1;
            skip_blanks(p);
        }
        if (tail)
            tail->next = step;
        else
            head = step;
        tail = step;
    }
    if (!head)
        return (qerr(p, "empty query"), NULL);
    return head;
}

static hnode *parse_stage(qparser_t *p)
{
    hnode *first = parse_alt(p);
    if (!first)
        return NULL;
    if (*p->cur != ',')
        return first;
    hnode *alt = qnode(K_ALT);
    alt->kid = first;
    hnode *last = first;
    while (q_accept(p, ','))
    {
        hnode *another = parse_alt(p);
        if (!another)
            return NULL;
        last->alt = another;
        last = another;
    }
    return alt;
}

/* comparison := stage (OP stage)?
 * OP is one of == != < <= > >=.  Comparisons bind tighter than pipes. */
static hnode *parse_cmp(qparser_t *p)
{
    hnode *left = parse_stage(p);
    if (!left)
        return NULL;

    skip_blanks(p);
    int op = -1;
    const char *c = p->cur;
    if (c[0] == '=' && c[1] == '=')
    {
        op = CMP_EQ;
        p->cur += 2;
    }
    else if (c[0] == '!' && c[1] == '=')
    {
        op = CMP_NE;
        p->cur += 2;
    }
    else if (c[0] == '<' && c[1] == '=')
    {
        op = CMP_LE;
        p->cur += 2;
    }
    else if (c[0] == '>' && c[1] == '=')
    {
        op = CMP_GE;
        p->cur += 2;
    }
    else if (c[0] == '<')
    {
        op = CMP_LT;
        p->cur++;
    }
    else if (c[0] == '>')
    {
        op = CMP_GT;
        p->cur++;
    }
    if (op < 0)
        return left;

    hnode *right = parse_stage(p);
    if (!right)
        return NULL;
    hnode *n = qnode(K_CMP);
    n->kid = left;
    n->right = right;
    n->a = op;
    return n;
}

/* Boolean operators bind looser than comparisons but tighter than pipes. */
static hnode *parse_and(qparser_t *p)
{
    hnode *left = parse_cmp(p);
    if (!left)
        return NULL;
    skip_blanks(p);
    while (keyword_at(p, "and"))
    {
        p->cur += 3;
        hnode *right = parse_cmp(p);
        if (!right)
            return NULL;
        hnode *n = qnode(K_AND);
        n->kid = left;
        n->right = right;
        left = n;
        skip_blanks(p);
    }
    return left;
}

static hnode *parse_or(qparser_t *p)
{
    hnode *left = parse_and(p);
    if (!left)
        return NULL;
    skip_blanks(p);
    while (keyword_at(p, "or"))
    {
        p->cur += 2;
        hnode *right = parse_and(p);
        if (!right)
            return NULL;
        hnode *n = qnode(K_OR);
        n->kid = left;
        n->right = right;
        left = n;
        skip_blanks(p);
    }
    return left;
}

static hnode *parse_pipe(qparser_t *p)
{
    hnode *first = parse_or(p);
    if (!first)
        return NULL;
    if (*p->cur != '|')
        return first;
    hnode *pipe = qnode(K_PIPE);
    pipe->kid = first;
    hnode *last = first;
    while (q_accept(p, '|'))
    {
        hnode *another = parse_or(p);
        if (!another)
            return NULL;
        last->alt = another;
        last = another;
    }
    return pipe;
}

hnode *query_parse(const char *text, const char **error, size_t *error_pos)
{
    qparser_t p = {text, text, NULL, 0};
    skip_blanks(&p);
    if (*p.cur == '\0')
    {
        if (error)
            *error = "empty query";
        if (error_pos)
            *error_pos = 0;
        return NULL;
    }
    hnode *n = parse_pipe(&p);
    if (p.error)
    {
        if (error)
            *error = p.error;
        if (error_pos)
            *error_pos = p.error_pos;
        return NULL;
    }
    skip_blanks(&p);
    if (*p.cur != '\0')
    {
        if (error)
            *error = "unexpected trailing characters in query";
        if (error_pos)
            *error_pos = (size_t)(p.cur - p.start);
        return NULL;
    }
    if (error)
        *error = NULL;
    if (error_pos)
        *error_pos = 0;
    return n;
}

void q_free(hnode *n)
{
    if (!n)
        return;
    q_free(n->kid);
    q_free(n->next);
    q_free(n->alt);
    q_free(n->right);
    free(n->name);
    free(n);
}

/* ----------------------------- formatter -------------------------------- */

static void fmt_chain(sbuf_t *b, const hnode *n);

static void fmt_long(sbuf_t *b, long v)
{
    char n[24];
    snprintf(n, sizeof n, "%ld", v);
    sbuf_text(b, n);
}

static int name_needs_quotes(const char *s)
{
    if (!s[0])
        return 1;
    for (const char *c = s; *c; ++c)
        if (!is_name_char(*c))
            return 1;
    return 0;
}

static void fmt_name(sbuf_t *b, const char *s)
{
    if (!name_needs_quotes(s))
    {
        sbuf_text(b, s);
        return;
    }
    sbuf_push(b, '"');
    for (const char *c = s; *c; ++c)
    {
        if (*c == '"' || *c == '\\')
            sbuf_push(b, '\\');
        sbuf_push(b, *c);
    }
    sbuf_push(b, '"');
}

static void fmt_chain(sbuf_t *b, const hnode *n)
{
    for (const hnode *c = n; c; c = c->next)
    {
        switch (c->kind)
        {
        case K_ROOT:
            sbuf_push(b, '$');
            break;
        case K_DOT:
            sbuf_push(b, '.');
            break;
        case K_FIELD:
            sbuf_push(b, '.');
            fmt_name(b, c->name);
            break;
        case K_IDX:
            sbuf_push(b, '[');
            if (c->kid)
                fmt_chain(b, c->kid);
            else
                fmt_long(b, c->a);
            sbuf_push(b, ']');
            break;
        case K_SLICE:
            sbuf_push(b, '[');
            if (c->a != LONG_MIN)
                fmt_long(b, c->a);
            sbuf_push(b, ':');
            if (c->b != LONG_MAX)
                fmt_long(b, c->b);
            sbuf_push(b, ']');
            break;
        case K_WILD:
            sbuf_text(b, "[*]");
            break;
        case K_REC:
            sbuf_text(b, "..");
            break;
        case K_RECF:
            sbuf_text(b, "..");
            fmt_name(b, c->name);
            break;
        case K_CONST:
            sbuf_push(b, '"');
            for (const char *u = c->name; u && *u; ++u)
            {
                if (*u == '"' || *u == '\\')
                    sbuf_push(b, '\\');
                sbuf_push(b, *u);
            }
            sbuf_push(b, '"');
            break;
        case K_NUM:
        {
            char num[40];
            json_format_number(c->num, num);
            sbuf_text(b, num);
            break;
        }
        case K_LIT:
            sbuf_text(b, c->a == 0 ? "null" : c->a == 1 ? "false" : "true");
            break;
        case K_CMP:
        {
            static const char *const ops[] = {"==", "!=", "<", "<=", ">", ">="};
            fmt_chain(b, c->kid);
            sbuf_push(b, ' ');
            sbuf_text(b, ops[c->a]);
            sbuf_push(b, ' ');
            fmt_chain(b, c->right);
            break;
        }
        case K_AND:
        case K_OR:
            fmt_chain(b, c->kid);
            sbuf_text(b, c->kind == K_AND ? " and " : " or ");
            fmt_chain(b, c->right);
            break;
        case K_CALL:
        {
            sbuf_text(b, c->name);
            sbuf_push(b, '(');
            const hnode *a = c->kid;
            int first = 1;
            for (; a; a = a->alt)
            {
                if (!first)
                {
                    sbuf_push(b, ',');
                    sbuf_push(b, ' ');
                }
                fmt_chain(b, a);
                first = 0;
            }
            sbuf_push(b, ')');
            break;
        }
        case K_ALT:
        {
            sbuf_push(b, '(');
            const hnode *br = c->kid;
            int first = 1;
            for (; br; br = br->alt)
            {
                if (!first)
                {
                    sbuf_push(b, ',');
                    sbuf_push(b, ' ');
                }
                fmt_chain(b, br);
                first = 0;
            }
            sbuf_push(b, ')');
            break;
        }
        case K_PIPE:
        {
            const hnode *st = c->kid;
            int first = 1;
            for (; st; st = st->alt)
            {
                if (!first)
                {
                    sbuf_push(b, ' ');
                    sbuf_push(b, '|');
                    sbuf_push(b, ' ');
                }
                fmt_chain(b, st);
                first = 0;
            }
            break;
        }
        case K_OPT:
            break;
        }
        if (c->opt && c->kind != K_OPT)
            sbuf_push(b, '?');
    }
}

char *query_format(const hnode *n)
{
    sbuf_t b = {NULL, 0, 0};
    fmt_chain(&b, n);
    sbuf_push(&b, '\0');
    return b.data;
}

/* ----------------------------- evaluator -------------------------------- */

static void collect(const hnode *n, json_value *val, results_t *r);

static int is_truthy(const json_value *v)
{
    return json_truthy(v);
}

static json_value *index_array(const json_value *a, long i)
{
    if (!a || a->kind != JSON_ARRAY)
        return NULL;
    if (i < 0)
        i += (long)a->count;
    if (i < 0 || (size_t)i >= a->count)
        return NULL;
    return a->items[i];
}

/* Every value under v, v itself first (used by $..). */
static void collect_descendants(const json_value *v, results_t *out)
{
    if (!v)
        return;
    results_push(out, (json_value *)v);
    if (v->kind == JSON_OBJECT)
        for (size_t i = 0; i < v->count; ++i)
            collect_descendants(v->items[i], out);
    else if (v->kind == JSON_ARRAY)
        for (size_t i = 0; i < v->count; ++i)
            collect_descendants(v->items[i], out);
}

/* Every member value whose key is `key`, at any depth (used by $..key). */
static void collect_keyed(const json_value *v, const char *key, results_t *out)
{
    if (!v)
        return;
    if (v->kind == JSON_OBJECT)
        for (size_t i = 0; i < v->count; ++i)
        {
            if (strcmp(v->keys[i], key) == 0)
                results_push(out, v->items[i]);
            collect_keyed(v->items[i], key, out);
        }
    else if (v->kind == JSON_ARRAY)
        for (size_t i = 0; i < v->count; ++i)
            collect_keyed(v->items[i], key, out);
}

/* Evaluate the arg chain on item and return the first result (or NULL). */
static json_value *eval_first(const hnode *arg, json_value *item)
{
    results_t tmp = {0, 0, NULL};
    collect(arg, item, &tmp);
    json_value *v = tmp.n ? tmp.v[0] : NULL;
    free(tmp.v);
    return v;
}

static int eval_any_truthy(const hnode *arg, json_value *item)
{
    results_t tmp = {0, 0, NULL};
    collect(arg, item, &tmp);
    int truthy = 0;
    for (size_t i = 0; i < tmp.n; ++i)
        if (is_truthy(tmp.v[i]))
            truthy = 1;
    free(tmp.v);
    return truthy;
}

/* ------------------------- builtin functions ---------------------------- */

typedef struct
{
    const char *key;
    json_value *v;
} member_t;

static int cmp_member(const void *a, const void *b)
{
    return strcmp(((const member_t *)a)->key, ((const member_t *)b)->key);
}

static int type_rank(const json_value *v)
{
    if (!v)
        return 0;
    switch (v->kind)
    {
    case JSON_NULL: return 0;
    case JSON_FALSE: return 1;
    case JSON_TRUE: return 2;
    case JSON_NUMBER: return 3;
    case JSON_STRING: return 4;
    case JSON_ARRAY: return 5;
    case JSON_OBJECT: return 6;
    }
    return 0;
}

static int val_cmp(const json_value *a, const json_value *b)
{
    int ra = type_rank(a);
    int rb = type_rank(b);
    if (ra != rb)
        return ra < rb ? -1 : 1;
    if (ra == 3)
        return a->number < b->number ? -1 : a->number > b->number ? 1 : 0;
    if (ra == 4)
        return strcmp(a->string, b->string);
    if (ra == 5)
    {
        size_t n = a->count < b->count ? a->count : b->count;
        for (size_t i = 0; i < n; ++i)
        {
            int cmp = val_cmp(a->items[i], b->items[i]);
            if (cmp)
                return cmp;
        }
        return a->count == b->count ? 0 : a->count < b->count ? -1 : 1;
    }
    if (ra == 6)
    {
        size_t n = a->count < b->count ? a->count : b->count;
        for (size_t i = 0; i < n; ++i)
        {
            int cmp = strcmp(a->keys[i], b->keys[i]);
            if (cmp)
                return cmp;
            cmp = val_cmp(a->items[i], b->items[i]);
            if (cmp)
                return cmp;
        }
        return a->count == b->count ? 0 : a->count < b->count ? -1 : 1;
    }
    return 0;
}

static json_value *json_number_of(double d)
{
    return json_number_new(d);
}

/* Deep equality; object member order does not matter. */
static int json_equal(const json_value *a, const json_value *b)
{
    if (a == b)
        return 1;
    if (!a || !b || a->kind != b->kind)
        return 0;
    switch (a->kind)
    {
    case JSON_NULL:
    case JSON_TRUE:
    case JSON_FALSE:
        return 1;
    case JSON_NUMBER:
        return a->number == b->number;
    case JSON_STRING:
        return strcmp(a->string, b->string) == 0;
    case JSON_ARRAY:
        if (a->count != b->count)
            return 0;
        for (size_t i = 0; i < a->count; ++i)
            if (!json_equal(a->items[i], b->items[i]))
                return 0;
        return 1;
    case JSON_OBJECT:
        if (a->count != b->count)
            return 0;
        for (size_t i = 0; i < a->count; ++i)
        {
            json_value *bv = json_try_get(b, a->keys[i]);
            if (!bv || !json_equal(a->items[i], bv))
                return 0;
        }
        return 1;
    }
    return 0;
}

/* jq-ish contains: substring for strings, subset for arrays and objects. */
static int value_contains(const json_value *hay, const json_value *needle)
{
    if (!hay || !needle)
        return 0;
    if (needle->kind == JSON_NULL)
        return 1;

    /* Scalar membership: [1,2,3] contains 2, but ["hello"] does not
     * "contain" "ell" unless the whole string matches. */
    if (hay->kind == JSON_ARRAY && needle->kind != JSON_ARRAY &&
        needle->kind != JSON_OBJECT)
    {
        for (size_t i = 0; i < hay->count; ++i)
            if (json_equal(hay->items[i], needle))
                return 1;
        return 0;
    }

    if (needle->kind == JSON_STRING)
        return hay->kind == JSON_STRING &&
               strstr(hay->string, needle->string) != NULL;
    if (needle->kind == JSON_ARRAY)
    {
        if (hay->kind != JSON_ARRAY)
            return 0;
        for (size_t i = 0; i < needle->count; ++i)
        {
            int found = 0;
            for (size_t j = 0; j < hay->count && !found; ++j)
                found = value_contains(hay->items[j], needle->items[i]);
            if (!found)
                return 0;
        }
        return 1;
    }
    if (needle->kind == JSON_OBJECT)
    {
        if (hay->kind != JSON_OBJECT)
            return 0;
        for (size_t i = 0; i < needle->count; ++i)
        {
            json_value *hv = json_try_get(hay, needle->keys[i]);
            if (!hv || !value_contains(hv, needle->items[i]))
                return 0;
        }
        return 1;
    }
    return json_equal(hay, needle);
}

/* add()/join(): combine two values when the types allow it. */
static json_value *value_concat(const json_value *a, const json_value *b)
{
    if (!a || a->kind == JSON_NULL)
        return (json_value *)b;
    if (!b || b->kind == JSON_NULL)
        return (json_value *)a;
    if (a->kind == JSON_NUMBER && b->kind == JSON_NUMBER)
        return json_number_new(a->number + b->number);
    if (a->kind == JSON_STRING && b->kind == JSON_STRING)
    {
        size_t la = strlen(a->string);
        size_t lb = strlen(b->string);
        char *s = xmalloc(la + lb + 1);
        memcpy(s, a->string, la);
        memcpy(s + la, b->string, lb);
        s[la + lb] = '\0';
        json_value *v = json_string_new(s);
        free(s);
        return v;
    }
    if (a->kind == JSON_ARRAY && b->kind == JSON_ARRAY)
    {
        json_value *v = json_array_new();
        for (size_t i = 0; i < a->count; ++i)
            json_array_push(v, a->items[i]);
        for (size_t i = 0; i < b->count; ++i)
            json_array_push(v, b->items[i]);
        return v;
    }
    if (a->kind == JSON_OBJECT && b->kind == JSON_OBJECT)
    {
        json_value *v = json_object_new();
        for (size_t i = 0; i < a->count; ++i)
            json_object_set(v, a->keys[i], a->items[i]);
        for (size_t i = 0; i < b->count; ++i)
            json_object_set(v, b->keys[i], b->items[i]);
        return v;
    }
    return NULL;
}

static json_value *substr_new(const char *s, size_t len)
{
    char *tmp = xmalloc(len + 1);
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    json_value *v = json_string_new(tmp);
    free(tmp);
    return v;
}

static int cmp_value_ptrs(const void *pa, const void *pb)
{
    const json_value *a = *(const json_value *const *)pa;
    const json_value *b = *(const json_value *const *)pb;
    return val_cmp(a, b);
}

typedef struct
{
    json_value *key;
    json_value *item;
} keyed_item_t;

static int cmp_keyed_items(const void *pa, const void *pb)
{
    const keyed_item_t *a = pa;
    const keyed_item_t *b = pb;
    return val_cmp(a->key, b->key);
}

static void flatten_into(json_value *out, const json_value *v)
{
    for (size_t i = 0; i < v->count; ++i)
    {
        if (v->items[i]->kind == JSON_ARRAY)
            flatten_into(out, v->items[i]);
        else
            json_array_push(out, v->items[i]);
    }
}

/* Reverse UTF-8 code points without validating them: each leading byte
 * determines the chunk length, so valid text round-trips exactly. */
static json_value *reverse_string(const char *s)
{
    size_t len = strlen(s);
    char *out = xmalloc(len + 1);
    out[len] = '\0';
    size_t write = len;
    for (size_t i = 0; i < len;)
    {
        unsigned char c = (unsigned char)s[i];
        size_t chunk = 1;
        if ((c & 0xE0) == 0xC0)
            chunk = 2;
        else if ((c & 0xF0) == 0xE0)
            chunk = 3;
        else if ((c & 0xF8) == 0xF0)
            chunk = 4;
        if (i + chunk > len)
            chunk = 1;
        write -= chunk;
        memcpy(out + write, s + i, chunk);
        i += chunk;
    }
    json_value *v = json_string_new(out);
    free(out);
    return v;
}

static void do_call(const hnode *call, json_value *val, results_t *outs)
{
    const char *name = call->name;
    const hnode *arg = call->kid;

    if (strcmp(name, "length") == 0)
    {
        if (!val)
            return;
        switch (val->kind)
        {
        case JSON_STRING:
            results_push(outs, json_number_of((double)strlen(val->string)));
            return;
        case JSON_ARRAY:
        case JSON_OBJECT:
            results_push(outs, json_number_of((double)val->count));
            return;
        case JSON_NULL:
            results_push(outs, json_number_of(0.0));
            return;
        case JSON_NUMBER:
            results_push(outs, json_number_of(fabs(val->number)));
            return;
        default:
            return;
        }
    }
    if (strcmp(name, "keys") == 0)
    {
        if (!val || val->kind != JSON_OBJECT)
            return;
        json_value *out = json_array_new();
        member_t *ms =
            xmalloc((val->count ? val->count : 1) * sizeof *ms);
        for (size_t i = 0; i < val->count; ++i)
        {
            ms[i].key = val->keys[i];
            ms[i].v = val->items[i];
        }
        qsort(ms, val->count, sizeof *ms, cmp_member);
        for (size_t i = 0; i < val->count; ++i)
        {
            if (arg)
                json_array_push(out, ms[i].v);
            else
                json_array_push(out, json_string_new(ms[i].key));
        }
        free(ms);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "values") == 0)
    {
        if (!val || (val->kind != JSON_OBJECT && val->kind != JSON_ARRAY))
            return;
        json_value *out = json_array_new();
        for (size_t i = 0; i < val->count; ++i)
            json_array_push(out, val->items[i]);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "map") == 0)
    {
        if (!arg || !val ||
            (val->kind != JSON_ARRAY && val->kind != JSON_OBJECT))
            return;
        json_value *out = json_array_new();
        for (size_t i = 0; i < val->count; ++i)
        {
            results_t tmp = {0, 0, NULL};
            collect(arg, val->items[i], &tmp);
            for (size_t j = 0; j < tmp.n; ++j)
                json_array_push(out, tmp.v[j]);
            free(tmp.v);
        }
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "add") == 0)
    {
        if (!val || (val->kind != JSON_ARRAY && val->kind != JSON_OBJECT))
            return;
        if (!val->count)
        {
            results_push(outs, json_null_new());
            return;
        }
        json_value *acc = val->items[0];
        for (size_t i = 1; i < val->count; ++i)
        {
            acc = value_concat(acc, val->items[i]);
            if (!acc)
                return;
        }
        results_push(outs, acc);
        return;
    }
    if (strcmp(name, "unique") == 0)
    {
        if (!val || val->kind != JSON_ARRAY)
            return;
        json_value *out = json_array_new();
        if (!val->count)
        {
            results_push(outs, out);
            return;
        }
        json_value **items = xmalloc(val->count * sizeof *items);
        memcpy(items, val->items, val->count * sizeof *items);
        qsort(items, val->count, sizeof *items, cmp_value_ptrs);
        for (size_t i = 0; i < val->count; ++i)
            if (i == 0 || !json_equal(items[i], items[i - 1]))
                json_array_push(out, items[i]);
        free(items);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "group_by") == 0)
    {
        if (!arg || !val || val->kind != JSON_ARRAY)
            return;
        json_value *out = json_array_new();
        if (!val->count)
        {
            results_push(outs, out);
            return;
        }
        keyed_item_t *pairs = xmalloc(val->count * sizeof *pairs);
        json_value *missing = json_null_new();
        for (size_t i = 0; i < val->count; ++i)
        {
            json_value *key = eval_first(arg, val->items[i]);
            pairs[i].key = key ? key : missing;
            pairs[i].item = val->items[i];
        }
        qsort(pairs, val->count, sizeof *pairs, cmp_keyed_items);
        size_t i = 0;
        while (i < val->count)
        {
            size_t j = i + 1;
            while (j < val->count && json_equal(pairs[i].key, pairs[j].key))
                ++j;
            json_value *group = json_array_new();
            for (size_t k = i; k < j; ++k)
                json_array_push(group, pairs[k].item);
            json_array_push(out, group);
            i = j;
        }
        free(pairs);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "flatten") == 0)
    {
        if (!val || val->kind != JSON_ARRAY)
            return;
        json_value *out = json_array_new();
        flatten_into(out, val);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "to_entries") == 0)
    {
        if (!val || (val->kind != JSON_ARRAY && val->kind != JSON_OBJECT))
            return;
        json_value *out = json_array_new();
        for (size_t i = 0; i < val->count; ++i)
        {
            json_value *entry = json_object_new();
            if (val->kind == JSON_OBJECT)
                json_object_set(entry, "key", json_string_new(val->keys[i]));
            else
                json_object_set(entry, "key", json_number_new((double)i));
            json_object_set(entry, "value", val->items[i]);
            json_array_push(out, entry);
        }
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "from_entries") == 0)
    {
        if (!val || val->kind != JSON_ARRAY)
            return;
        json_value *out = json_object_new();
        for (size_t i = 0; i < val->count; ++i)
        {
            json_value *entry = val->items[i];
            if (entry->kind != JSON_OBJECT)
                return;
            json_value *key = json_try_get(entry, "key");
            json_value *value = json_try_get(entry, "value");
            if (!key || !value)
                return;
            if (key->kind == JSON_STRING)
                json_object_set(out, key->string, value);
            else if (key->kind == JSON_NUMBER)
            {
                char name[40];
                json_format_number(key->number, name);
                json_object_set(out, name, value);
            }
            else
                return;
        }
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "reverse") == 0)
    {
        if (!val)
            return;
        if (val->kind == JSON_ARRAY)
        {
            json_value *out = json_array_new();
            for (size_t i = val->count; i > 0; --i)
                json_array_push(out, val->items[i - 1]);
            results_push(outs, out);
        }
        else if (val->kind == JSON_STRING)
            results_push(outs, reverse_string(val->string));
        return;
    }
    if (strcmp(name, "test") == 0)
    {
        if (!arg || !val || val->kind != JSON_STRING)
            return;
        json_value *pattern = eval_first(arg, val);
        if (!pattern || pattern->kind != JSON_STRING)
            return;
        regex_t re;
        if (regcomp(&re, pattern->string, REG_EXTENDED | REG_NOSUB) != 0)
            return;
        int matched = regexec(&re, val->string, 0, NULL, 0) == 0;
        regfree(&re);
        results_push(outs, json_bool_new(matched));
        return;
    }
    if (strcmp(name, "contains") == 0)
    {
        if (!arg || !val)
            return;
        json_value *needle = eval_first(arg, val);
        if (!needle)
            return;
        results_push(outs, json_bool_new(value_contains(val, needle)));
        return;
    }
    if (strcmp(name, "startswith") == 0 || strcmp(name, "endswith") == 0)
    {
        if (!arg || !val || val->kind != JSON_STRING)
            return;
        json_value *needle = eval_first(arg, val);
        if (!needle || needle->kind != JSON_STRING)
            return;
        size_t n = strlen(needle->string);
        size_t m = strlen(val->string);
        int ok;
        if (name[0] == 's')
            ok = n <= m && memcmp(val->string, needle->string, n) == 0;
        else
            ok = n <= m &&
                 memcmp(val->string + (m - n), needle->string, n) == 0;
        results_push(outs, json_bool_new(ok));
        return;
    }
    if (strcmp(name, "join") == 0)
    {
        if (!arg || !val || val->kind != JSON_ARRAY)
            return;
        json_value *sepv = eval_first(arg, val);
        if (!sepv || sepv->kind != JSON_STRING)
            return;
        sbuf_t buf = {NULL, 0, 0};
        for (size_t i = 0; i < val->count; ++i)
        {
            json_value *item = val->items[i];
            if (item->kind == JSON_STRING)
                sbuf_text(&buf, item->string);
            else if (item->kind == JSON_NUMBER)
            {
                char num[40];
                json_format_number(item->number, num);
                sbuf_text(&buf, num);
            }
            else if (item->kind != JSON_NULL)
            {
                free(buf.data);
                return;
            }
            if (i + 1 < val->count)
                sbuf_text(&buf, sepv->string);
        }
        sbuf_push(&buf, '\0');
        results_push(outs, json_string_new(buf.data));
        free(buf.data);
        return;
    }
    if (strcmp(name, "split") == 0)
    {
        if (!arg || !val || val->kind != JSON_STRING)
            return;
        json_value *sepv = eval_first(arg, val);
        if (!sepv || sepv->kind != JSON_STRING)
            return;
        size_t sep_len = strlen(sepv->string);
        json_value *out = json_array_new();
        if (sep_len == 0)
        {
            for (const char *c = val->string; *c; ++c)
                json_array_push(out, substr_new(c, 1));
        }
        else
        {
            const char *start = val->string;
            for (;;)
            {
                const char *hit = strstr(start, sepv->string);
                if (!hit)
                {
                    json_array_push(out, substr_new(start, strlen(start)));
                    break;
                }
                json_array_push(out, substr_new(start, (size_t)(hit - start)));
                start = hit + sep_len;
            }
        }
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "type") == 0)
    {
        if (!val)
            return;
        const char *s = "null";
        if (val->kind == JSON_TRUE || val->kind == JSON_FALSE)
            s = "boolean";
        else if (val->kind == JSON_NUMBER)
            s = "number";
        else if (val->kind == JSON_STRING)
            s = "string";
        else if (val->kind == JSON_ARRAY)
            s = "array";
        else if (val->kind == JSON_OBJECT)
            s = "object";
        results_push(outs, json_string_new(s));
        return;
    }
    if (strcmp(name, "has") == 0)
    {
        if (!val || !arg)
            return;
        json_value *k = eval_first(arg, val);
        if (!k)
            return;
        if (k->kind == JSON_STRING)
            results_push(outs, json_bool_new(!!json_try_get(val, k->string)));
        else if (k->kind == JSON_NUMBER)
        {
            long i = (long)k->number;
            results_push(outs,
                         json_bool_new(i >= 0 && val->kind == JSON_ARRAY &&
                                       (size_t)i < val->count));
        }
        return;
    }
    if (strcmp(name, "select") == 0)
    {
        if (!arg)
            return;
        if (val && val->kind == JSON_ARRAY)
        {
            json_value *out = json_array_new();
            for (size_t i = 0; i < val->count; ++i)
                if (eval_any_truthy(arg, val->items[i]))
                    json_array_push(out, val->items[i]);
            results_push(outs, out);
        }
        else if (val && eval_any_truthy(arg, val))
            results_push(outs, val);
        return;
    }
    if (strcmp(name, "sort") == 0)
    {
        if (!val || val->kind != JSON_ARRAY)
            return;
        size_t n = val->count;
        json_value **tab = xmalloc((n ? n : 1) * sizeof *tab);
        for (size_t i = 0; i < n; ++i)
            tab[i] = val->items[i];
        for (size_t i = 1; i < n; ++i)
        {
            json_value *v = tab[i];
            json_value *kv = arg ? eval_first(arg, v) : v;
            size_t j = i;
            while (j > 0)
            {
                json_value *pv =
                    arg ? eval_first(arg, tab[j - 1]) : tab[j - 1];
                if (val_cmp(pv, kv) <= 0)
                    break;
                tab[j] = tab[j - 1];
                j--;
            }
            tab[j] = v;
        }
        json_value *out = json_array_new();
        for (size_t i = 0; i < n; ++i)
            json_array_push(out, tab[i]);
        free(tab);
        results_push(outs, out);
        return;
    }
    if (strcmp(name, "max") == 0 || strcmp(name, "min") == 0)
    {
        if (!val || val->kind != JSON_ARRAY || !val->count)
            return;
        int want_max = name[0] == 'm' && name[1] == 'a';
        size_t best = 0;
        json_value *bestv = arg ? eval_first(arg, val->items[0]) : val->items[0];
        for (size_t i = 1; i < val->count; ++i)
        {
            json_value *kv = arg ? eval_first(arg, val->items[i]) : val->items[i];
            int cmp = val_cmp(kv, bestv);
            if ((want_max && cmp > 0) || (!want_max && cmp < 0))
            {
                best = i;
                bestv = kv;
            }
        }
        results_push(outs, val->items[best]);
        return;
    }
    if (strcmp(name, "sum") == 0)
    {
        if (!val || (val->kind != JSON_ARRAY && val->kind != JSON_OBJECT))
            return;
        double d = 0.0;
        for (size_t i = 0; i < val->count; ++i)
        {
            json_value *v = val->items[i];
            json_value *k = arg ? eval_first(arg, v) : v;
            if (k && k->kind == JSON_NUMBER)
                d += k->number;
        }
        results_push(outs, json_number_of(d));
        return;
    }
    if (strcmp(name, "first") == 0)
    {
        json_value *v = index_array(val, 0);
        if (v)
            results_push(outs, v);
        return;
    }
    if (strcmp(name, "last") == 0)
    {
        if (val && val->kind == JSON_ARRAY && val->count)
        {
            json_value *v = val->items[val->count - 1];
            results_push(outs, v);
        }
        return;
    }
    if (strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0 ||
        strcmp(name, "abs") == 0)
    {
        if (!val || val->kind != JSON_NUMBER)
            return;
        double d = val->number;
        if (name[0] == 'f')
            d = floor(d);
        else if (name[0] == 'c')
            d = ceil(d);
        else
            d = fabs(d);
        results_push(outs, json_number_of(d));
        return;
    }
    if (strcmp(name, "sqrt") == 0)
    {
        if (!val || val->kind != JSON_NUMBER || val->number < 0)
            return;
        results_push(outs, json_number_of(sqrt(val->number)));
        return;
    }
    if (strcmp(name, "ftos") == 0)
    {
        if (!val || val->kind != JSON_NUMBER)
            return;
        char buf[40];
        json_format_number(val->number, buf);
        results_push(outs, json_string_new(buf));
        return;
    }
    if (strcmp(name, "not") == 0)
    {
        results_push(outs, json_bool_new(!is_truthy(val)));
        return;
    }
    /* unknown function: no result */
}

static void collect(const hnode *n, json_value *val, results_t *r)
{
    results_t outs = {0, 0, NULL};

    switch (n->kind)
    {
    case K_ROOT:
    case K_DOT:
        results_push(&outs, val);
        break;
    case K_CONST:
        results_push(&outs, json_string_new(n->name ? n->name : ""));
        break;
    case K_NUM:
        results_push(&outs, json_number_new(n->num));
        break;
    case K_LIT:
        results_push(&outs, n->a == 0 ? json_null_new()
                                     : json_bool_new(n->a == 2));
        break;
    case K_FIELD:
    {
        if (val && val->kind == JSON_OBJECT)
        {
            json_value *m = json_try_get(val, n->name);
            if (m)
                results_push(&outs, m);
        }
        break;
    }
    case K_IDX:
    {
        if (n->kid)
        {
            results_t tmp = {0, 0, NULL};
            collect(n->kid, val, &tmp);
            for (size_t i = 0; i < tmp.n; ++i)
            {
                json_value *k = tmp.v[i];
                json_value *found = NULL;
                if (k && k->kind == JSON_NUMBER)
                    found = index_array(val, (long)k->number);
                else if (k && k->kind == JSON_STRING && val &&
                         val->kind == JSON_OBJECT)
                    found = json_try_get(val, k->string);
                if (found)
                    results_push(&outs, found);
            }
            free(tmp.v);
        }
        else
        {
            json_value *v = index_array(val, n->a);
            if (v)
                results_push(&outs, v);
        }
        break;
    }
    case K_SLICE:
    {
        if (!val || val->kind != JSON_ARRAY)
            break;
        long count = (long)val->count;
        long a = n->a != LONG_MIN ? n->a : 0;
        long b = n->b != LONG_MAX ? n->b : count;
        if (b < 0 || a > count)
            break; /* negative end or start past the end: no result */
        if (a < 0)
            a += count;
        if (a < 0)
            a = 0;
        if (b > count)
            b = count;
        if (b > a)
        {
            json_value *out = json_array_new();
            for (long i = a; i < b; ++i)
                json_array_push(out, val->items[i]);
            results_push(&outs, out);
        }
        break;
    }
    case K_WILD:
        if (val && (val->kind == JSON_ARRAY || val->kind == JSON_OBJECT))
            for (size_t i = 0; i < val->count; ++i)
                results_push(&outs, val->items[i]);
        break;
    case K_REC:
        collect_descendants(val, &outs);
        break;
    case K_RECF:
        collect_keyed(val, n->name, &outs);
        break;
    case K_ALT:
        for (const hnode *br = n->kid; br; br = br->alt)
        {
            results_t tmp = {0, 0, NULL};
            collect(br, val, &tmp);
            results_absorb(&outs, &tmp);
            free(tmp.v);
        }
        break;
    case K_PIPE:
    {
        results_t inputs = {0, 0, NULL};
        results_push(&inputs, val);
        for (const hnode *st = n->kid; st; st = st->alt)
        {
            results_t out = {0, 0, NULL};
            for (size_t i = 0; i < inputs.n; ++i)
                collect(st, inputs.v[i], &out);
            free(inputs.v);
            inputs = out;
        }
        results_absorb(&outs, &inputs);
        free(inputs.v);
        break;
    }
    case K_CALL:
        do_call(n, val, &outs);
        break;
    case K_CMP:
    {
        results_t lhs = {0, 0, NULL};
        results_t rhs = {0, 0, NULL};
        collect(n->kid, val, &lhs);
        collect(n->right, val, &rhs);
        if (lhs.n && rhs.n)
        {
            json_value *a = lhs.v[0];
            json_value *b = rhs.v[0];
            int cmp = val_cmp(a, b);
            int truth = 0;
            switch (n->a)
            {
            case CMP_EQ: truth = json_equal(a, b); break;
            case CMP_NE: truth = !json_equal(a, b); break;
            case CMP_LT: truth = cmp < 0; break;
            case CMP_LE: truth = cmp <= 0; break;
            case CMP_GT: truth = cmp > 0; break;
            case CMP_GE: truth = cmp >= 0; break;
            }
            results_push(&outs, json_bool_new(truth));
        }
        free(lhs.v);
        free(rhs.v);
        break;
    }
    case K_AND:
        results_push(&outs,
                     json_bool_new(eval_any_truthy(n->kid, val) &&
                                   eval_any_truthy(n->right, val)));
        break;
    case K_OR:
        results_push(&outs,
                     json_bool_new(eval_any_truthy(n->kid, val) ||
                                   eval_any_truthy(n->right, val)));
        break;
    case K_OPT:
        break;
    }

    /* '?' — nothing matched: emit null instead */
    if (n->opt && n->kind != K_ALT && n->kind != K_PIPE && outs.n == 0)
        results_push(&outs, json_null_new());

    if (n->next)
    {
        for (size_t i = 0; i < outs.n; ++i)
            collect(n->next, outs.v[i], r);
    }
    else
    {
        results_absorb(r, &outs);
    }
    free(outs.v);
}

void query_eval(const hnode *n, json_value *val, results_t *r)
{
    collect(n, val, r);
}

const char *query_kind_name(qkind k)
{
    switch (k)
    {
    case K_ROOT: return "q-root";
    case K_DOT: return "q-dot";
    case K_FIELD: return "q-field";
    case K_IDX: return "q-idx";
    case K_SLICE: return "q-slice";
    case K_WILD: return "q-wild";
    case K_REC: return "q-rec";
    case K_RECF: return "q-recf";
    case K_OPT: return "q-opt";
    case K_ALT: return "q-alt";
    case K_PIPE: return "q-pipe";
    case K_CALL: return "q-call";
    case K_NUM: return "q-num";
    case K_LIT: return "q-lit";
    case K_CMP: return "q-cmp";
    case K_AND: return "q-and";
    case K_OR: return "q-or";
    case K_CONST: return "q-const";
    }
    return "q-unknown";
}
