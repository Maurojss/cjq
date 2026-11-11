/*
 * unit.c — white-box unit tests for the JSON engine (json.c) and the query
 * language (query.c).  Run through `make test` or directly after building:
 *
 *   ./tests/unit
 */

#define _POSIX_C_SOURCE 200809L

#include "json.h"
#include "query.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

static void report(const char *what, const char *got, const char *want)
{
    g_failures++;
    fprintf(stderr, "FAIL: %s\n", what);
    if (got)
        fprintf(stderr, "  got:  %s\n", got);
    if (want)
        fprintf(stderr, "  want: %s\n", want);
}

static json_value *parse_or_die(const char *text)
{
    const char *err = NULL;
    size_t pos = 0;
    json_value *v = json_parse(text, NULL, &err, &pos);
    if (!v)
    {
        fprintf(stderr, "unit: cannot parse fixture `%s': %s at byte %zu\n",
                text, err ? err : "?", pos);
        exit(1);
    }
    return v;
}

static char *capture(const json_value *v, int pretty)
{
    char *out = NULL;
    size_t len = 0;
    FILE *m = open_memstream(&out, &len);
    if (!m)
    {
        perror("open_memstream");
        exit(1);
    }
    json_print(v, m, pretty);
    fclose(m);
    return out;
}

static void check_print(const char *json, const char *expected)
{
    g_checks++;
    json_value *v = parse_or_die(json);
    char *got = capture(v, 0);
    if (strcmp(got, expected) != 0)
        report("json_print compact", got, expected);
    free(got);
}

static void check_pretty(const char *json, const char *expected)
{
    g_checks++;
    json_value *v = parse_or_die(json);
    char *got = capture(v, 1);
    if (strcmp(got, expected) != 0)
        report("json_print pretty", got, expected);
    free(got);
}

static void check_ascii(const char *json, const char *expected)
{
    g_checks++;
    json_value *v = parse_or_die(json);
    json_set_ascii(1);
    char *got = capture(v, 0);
    json_set_ascii(0);
    if (strcmp(got, expected) != 0)
        report("json_print ascii", got, expected);
    free(got);
}

static void check_indent(const char *json, const char *unit,
                         const char *expected)
{
    g_checks++;
    json_value *v = parse_or_die(json);
    json_set_indent(unit);
    char *got = capture(v, 1);
    json_set_indent("    ");
    if (strcmp(got, expected) != 0)
        report("json_print indent", got, expected);
    free(got);
}

static void check_parse_fails(const char *json)
{
    g_checks++;
    const char *err = NULL;
    size_t pos = 0;
    json_value *v = json_parse(json, NULL, &err, &pos);
    if (v || !err)
        report("json_parse should have failed", json, NULL);
}

static void check_format(const char *query, const char *expected)
{
    g_checks++;
    const char *err = NULL;
    size_t pos = 0;
    hnode *ast = query_parse(query, &err, &pos);
    if (!ast)
    {
        report("query_parse failed", query, expected);
        return;
    }
    char *got = query_format(ast);
    if (strcmp(got, expected) != 0)
        report("query_format", got, expected);
    free(got);
    q_free(ast);
}

static void check_query_fails(const char *query)
{
    g_checks++;
    const char *err = NULL;
    size_t pos = 0;
    hnode *ast = query_parse(query, &err, &pos);
    if (ast || !err)
        report("query_parse should have failed", query, NULL);
}

static void check_eval(const char *doc_json, const char *query, int raw,
                       const char *expected)
{
    g_checks++;
    json_value *doc = parse_or_die(doc_json);
    const char *err = NULL;
    size_t pos = 0;
    hnode *ast = query_parse(query, &err, &pos);
    if (!ast)
    {
        report("query_parse failed (eval)", query, expected);
        return;
    }
    results_t rs;
    results_init(&rs);
    query_eval(ast, doc, &rs);
    char *out = NULL;
    size_t len = 0;
    FILE *m = open_memstream(&out, &len);
    for (size_t i = 0; i < rs.n; ++i)
    {
        if (raw && rs.v[i]->kind == JSON_STRING)
        {
            fputs(rs.v[i]->string, m);
            fputc('\n', m);
        }
        else
            json_print(rs.v[i], m, 0);
    }
    fclose(m);
    if (strcmp(out, expected) != 0)
        report("query_eval", out, expected);
    free(out);
    free(rs.v);
    q_free(ast);
}

static void check_deep_nesting(void)
{
    size_t n = 600;
    char *text = xmalloc(2 * n + 1);
    for (size_t i = 0; i < n; ++i)
        text[i] = '[';
    for (size_t i = 0; i < n; ++i)
        text[n + i] = ']';
    text[2 * n] = '\0';

    g_checks++;
    const char *err = NULL;
    size_t pos = 0;
    json_value *v = json_parse(text, NULL, &err, &pos);
    if (v || !err)
        report("deep nesting should have failed", NULL, NULL);

    /* Well below the limit it still parses. */
    n = 100;
    for (size_t i = 0; i < n; ++i)
        text[n + i] = ']';
    text[2 * n] = '\0';
    g_checks++;
    err = NULL;
    pos = 0;
    v = json_parse(text, NULL, &err, &pos);
    if (!v || err)
        report("shallow nesting should parse", err, NULL);
    free(text);
}

/* Nested builtin calls and dynamic indexes must be rejected before the
 * parser runs out of stack. */
static void check_deep_queries(void)
{
    size_t n = 1000;
    const char *err = NULL;
    size_t pos = 0;

    char *text = malloc(4 * n + n + 2);
    if (!text)
        exit(1);
    for (size_t i = 0; i < n; ++i)
        memcpy(text + 4 * i, "map(", 4);
    text[4 * n] = '.';
    for (size_t i = 0; i < n; ++i)
        text[4 * n + 1 + i] = ')';
    text[5 * n + 1] = '\0';

    g_checks++;
    hnode *ast = query_parse(text, &err, &pos);
    if (ast || !err)
        report("deep builtin query should have failed", NULL, NULL);
    free(text);

    const char *err2 = NULL;
    size_t pos2 = 0;
    char *index = malloc(2 * n + n + 3);
    if (!index)
        exit(1);
    for (size_t i = 0; i < n; ++i)
        memcpy(index + 2 * i, ".[", 2);
    index[2 * n] = '.';
    for (size_t i = 0; i < n; ++i)
        index[2 * n + 1 + i] = ']';
    index[3 * n + 1] = '\0';

    g_checks++;
    ast = query_parse(index, &err2, &pos2);
    if (ast || !err2)
        report("deep index query should have failed", NULL, NULL);
    free(index);
}

static uint32_t next_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Throw random bytes and single-byte mutations of valid documents at the
 * parser; it must either accept them or fail cleanly, never crash. */
static void check_random_inputs(void)
{
    static const char *const seeds[] = {
        "{\"a\":[1,2.5,true,false,null],\"s\":\"caf\\u00e9\"}",
        "[{\"x\":1},{\"x\":[2,3]},{\"x\":{\"y\":\"z\"}}]",
        "{\"nested\":{\"deep\":[[[{\"k\":\"v\"}]]]},\"n\":-1.5e-3}",
        "\"\\ud83d\\ude00 \\t \\\\ \\\"\"",
    };
    const size_t seed_count = sizeof seeds / sizeof seeds[0];
    uint32_t state = 0x9e3779b9u;
    char buf[160];

    for (int iter = 0; iter < 3000; ++iter)
    {
        size_t len;
        if (iter & 1)
        {
            const char *seed = seeds[next_rand(&state) % seed_count];
            len = strlen(seed);
            memcpy(buf, seed, len);
            for (int flips = 0; flips < 3; ++flips)
                buf[next_rand(&state) % len] =
                    (char)(1 + next_rand(&state) % 255);
        }
        else
        {
            len = next_rand(&state) % 64;
            for (size_t i = 0; i < len; ++i)
                buf[i] = (char)(1 + next_rand(&state) % 255);
        }
        buf[len] = '\0';

        const char *err = NULL;
        size_t pos = 0;
        json_value *v = json_parse(buf, NULL, &err, &pos);
        if (!v && (!err || pos > len))
        {
            report("random input produced bad error metadata", buf, NULL);
            return;
        }
        if (v)
        {
            char *out = NULL;
            size_t out_len = 0;
            FILE *m = open_memstream(&out, &out_len);
            if (!m)
            {
                perror("open_memstream");
                exit(1);
            }
            json_print(v, m, (int)(next_rand(&state) & 1));
            fclose(m);
            free(out);
        }
    }
    g_checks++;
}

/* Throw random bytes and mutations of valid queries at the query parser. */
static void check_random_queries(void)
{
    static const char *const seeds[] = {
        "$.a.b[0] | length()",
        "$.users[*] | select(.age > 21) | map(.name)",
        "$..id",
        "$name",
        "match(\"^[a-z]+$\")",
        "keys_unsorted()",
        "sort(.x)[-1]",
    };
    const size_t seed_count = sizeof seeds / sizeof seeds[0];
    uint32_t state = 0x51ed270bu;
    char buf[160];

    for (int iter = 0; iter < 3000; ++iter)
    {
        size_t len;
        if (iter & 1)
        {
            const char *seed = seeds[next_rand(&state) % seed_count];
            len = strlen(seed);
            memcpy(buf, seed, len);
            for (int flips = 0; flips < 3; ++flips)
                buf[next_rand(&state) % len] =
                    (char)(1 + next_rand(&state) % 255);
        }
        else
        {
            len = next_rand(&state) % 64;
            for (size_t i = 0; i < len; ++i)
                buf[i] = (char)(1 + next_rand(&state) % 255);
        }
        buf[len] = '\0';

        const char *err = NULL;
        size_t pos = 0;
        hnode *ast = query_parse(buf, &err, &pos);
        if (!ast)
        {
            if (!err)
            {
                report("random query failed without an error", buf, NULL);
                return;
            }
            continue;
        }
        char *fmt = query_format(ast);
        if (!fmt)
            report("query_format returned NULL", buf, NULL);
        free(fmt);
        q_free(ast);
    }
    g_checks++;
}

int main(void)
{
    /* ---- JSON parsing and printing ---- */
    check_print("{\"a\":1}", "{\"a\":1}\n");
    check_print("[1, 2.5, true, false, null]", "[1,2.5,true,false,null]\n");
    check_print("{\"b\":1,\"b\":2}", "{\"b\":2}\n");
    check_print("\"tab\\there\"", "\"tab\\there\"\n");
    check_print("\"caf\\u00e9\"", "\"caf\xc3\xa9\"\n");
    check_print("\"\\ud83d\\ude00\"", "\"\xf0\x9f\x98\x80\"\n");
    check_print("[1.0, 0.5, 1e21, -0.0]", "[1,0.5,1e+21,-0]\n");
    check_print("\"\\u0001\"", "\"\\u0001\"\n");
    check_print(" { \"x\" : [ ] } ", "{\"x\":[]}\n");
    check_print("[1e2, 1e20, 1e-5, 1.5e3]", "[100,100000000000000000000,1e-05,1500]\n");
    check_print("[5e-324]", "[5e-324]\n");
    check_pretty("{\"a\":[1]}", "{\n    \"a\": [\n        1\n    ]\n}\n");
    check_ascii("\"caf\\u00e9\"", "\"caf\\u00e9\"\n");
    check_ascii("\"\\ud83d\\ude00\"", "\"\\ud83d\\ude00\"\n");
    check_ascii("\"plain\"", "\"plain\"\n");
    check_indent("{\"a\":[1]}", "  ", "{\n  \"a\": [\n    1\n  ]\n}\n");
    check_indent("{\"a\":1}", "\t", "{\n\t\"a\": 1\n}\n");
    check_indent("{\"a\":1}", "", "{\n\"a\": 1\n}\n");

    /* ---- JSON parse errors ---- */
    check_parse_fails("");
    check_parse_fails("{");
    check_parse_fails("{\"a\":}");
    check_parse_fails("[1,]");
    check_parse_fails("tru");
    check_parse_fails("01");
    check_parse_fails("1.");
    check_parse_fails("\"abc");
    check_parse_fails("\"a\\u0000b\"");

    /* ---- nesting limit ---- */
    check_deep_nesting();
    check_deep_queries();

    /* ---- parser stress ---- */
    check_random_inputs();
    check_random_queries();

    /* ---- query formatter ---- */
    check_format("$", "$");
    check_format("$.", "$.");
    check_format("$.alice.id", "$.alice.id");
    check_format("$.a[2].b", "$.a[2].b");
    check_format("$.b[-1]", "$.b[-1]");
    check_format("$.a[*]", "$.a[*]");
    check_format("$.a[]", "$.a[*]");
    check_format("$.a?", "$.a?");
    check_format("$..uid", "$..uid");
    check_format("$.a, $.b", "($.a, $.b)");
    check_format("$.a | length()", "$.a | length()");
    check_format("$.a[1:3]", "$.a[1:3]");
    check_format("$.a[:3]", "$.a[:3]");
    check_format("$.a[1:]", "$.a[1:]");
    check_format("$.\"two words\"", "$.\"two words\"");
    check_format("$.a[.x]", "$.a[.x]");
    check_format("has(\"tag\")", "has(\"tag\")");
    check_format("$.age > 21", "$.age > 21");
    check_format("$.a == \"x\"", "$.a == \"x\"");
    check_format("select(.ok != false)", "select(.ok != false)");
    check_format("1.5 <= .x", "1.5 <= .x");
    check_format("map(.name)", "map(.name)");
    check_format("add()", "add()");
    check_format("$.a and $.b", "$.a and $.b");
    check_format("$.a or $.b and $.c", "$.a or $.b and $.c");
    check_format("unique()", "unique()");
    check_format("group_by(.k)", "group_by(.k)");
    check_format("flatten()", "flatten()");
    check_format("to_entries()", "to_entries()");
    check_format("from_entries()", "from_entries()");
    check_format("reverse()", "reverse()");
    check_format("test(\"^a\")", "test(\"^a\")");
    check_format("keys_unsorted()", "keys_unsorted()");
    check_format("min_by(.v)", "min_by(.v)");
    check_format("max_by(.v)", "max_by(.v)");
    check_format("fromjson()", "fromjson()");
    check_format("tojson()", "tojson()");
    check_format("$who", "$who");
    check_format("$my_var", "$my_var");
    check_format("tostring()", "tostring()");
    check_format("tonumber()", "tonumber()");
    check_format("ascii_downcase()", "ascii_downcase()");
    check_format("ascii_upcase()", "ascii_upcase()");
    check_format("ltrimstr(\"ab\")", "ltrimstr(\"ab\")");
    check_format("rtrimstr(\"ab\")", "rtrimstr(\"ab\")");
    check_format("match(\"^a\")", "match(\"^a\")");

    /* ---- query syntax errors ---- */
    check_query_fails("");
    check_query_fails("a");
    check_query_fails("$.a[");
    check_query_fails("$.a[2");
    check_query_fails("$.a]");
    check_query_fails("has(");
    check_query_fails("\"abc");
    check_query_fails("nope()");
    check_query_fails("length(1)");
    check_query_fails("has()");
    check_query_fails("$.a | select()");
    check_query_fails("$.a | map()");
    check_query_fails("$.a | add(1)");
    check_query_fails("$.a > ");
    check_query_fails("$.a = 1");
    check_query_fails("$.a and");
    check_query_fails("$.a | unique(1)");
    check_query_fails("$.a | group_by()");
    check_query_fails("$.a | flatten(1)");
    check_query_fails("$.a | to_entries(1)");
    check_query_fails("$.a | from_entries(1)");
    check_query_fails("$.a | reverse(1)");
    check_query_fails("$.a | test()");
    check_query_fails("$.a | min_by()");
    check_query_fails("$.a | max_by()");
    check_query_fails("$.a | keys_unsorted(1)");
    check_query_fails("$.a | fromjson(1)");
    check_query_fails("$.a | tojson(1)");
    check_query_fails("$.a | tostring(1)");
    check_query_fails("$.a | tonumber(1)");
    check_query_fails("$.a | ascii_downcase(1)");
    check_query_fails("$.a | ascii_upcase(1)");
    check_query_fails("$.a | ltrimstr()");
    check_query_fails("$.a | rtrimstr()");
    check_query_fails("$.a | match()");

    /* ---- evaluation ---- */
    const char *doc =
        "{\"a\":[1,2,3,4,5],\"alice\":{\"id\":7},\"b\":{\"tag\":\"data\"},"
        "\"list\":[{\"x\":1,\"y\":2},{\"x\":2,\"y\":3}],\"alone\":null,"
        "\"two words\":\"hi\",\"nested\":{\"uid\":9,\"deep\":{\"uid\":[1]}}}";

    check_eval(doc, "$.alice.id", 0, "7\n");
    check_eval(doc, "$.a[1]", 0, "2\n");
    check_eval(doc, "$.a[-1]", 0, "5\n");
    check_eval(doc, "$.a[1:3]", 0, "[2,3]\n");
    check_eval(doc, "$.a[1:]", 0, "[2,3,4,5]\n");
    check_eval(doc, "$.a[:2]", 0, "[1,2]\n");
    check_eval(doc, "$.a[-2:]", 0, "[4,5]\n");
    check_eval(doc, "$.a[0:-2]", 0, "");
    check_eval(doc, "$.a[100:200]", 0, "");
    check_eval(doc, "$.missing", 0, "");
    check_eval(doc, "$.missing?", 0, "null\n");
    check_eval(doc, "$.a[*]", 0, "1\n2\n3\n4\n5\n");
    check_eval(doc, "$.b.tag", 0, "\"data\"\n");
    check_eval(doc, "$.b[\"tag\"]", 0, "\"data\"\n");
    check_eval(doc, "$.alone.x", 0, "");
    check_eval(doc, "$.alone.x?", 0, "null\n");
    check_eval(doc, "$.a | length()", 0, "5\n");
    check_eval(doc, "$.\"two words\"", 0, "\"hi\"\n");
    check_eval(doc, "$.\"two words\"", 1, "hi\n");
    check_eval(doc, "$..uid", 0, "9\n[1]\n");
    check_eval(doc, "$.nested..uid", 0, "9\n[1]\n");
    check_eval(doc, "$.b | keys()", 0, "[\"tag\"]\n");
    check_eval(doc, "$.b | values()", 0, "[\"data\"]\n");
    check_eval(doc, "$.a | values()", 0, "[1,2,3,4,5]\n");
    check_eval(doc, "$.a | first()", 0, "1\n");
    check_eval(doc, "$.a | last()", 0, "5\n");
    check_eval(doc, "$.a | sum()", 0, "15\n");
    check_eval(doc, "$.list | sum(.x)", 0, "3\n");
    check_eval(doc, "$.list | max(.y).x", 0, "2\n");
    check_eval(doc, "$.list | min(.y).x", 0, "1\n");
    check_eval(doc, "$.list | sort(.x)[1].y", 0, "3\n");
    check_eval(doc, "$.list | sort(.y)[0].x", 0, "1\n");
    check_eval(doc, "$.list | select(.x)", 0,
               "[{\"x\":1,\"y\":2},{\"x\":2,\"y\":3}]\n");
    check_eval(doc, "$.b | has(\"tag\")", 0, "true\n");
    check_eval(doc, "$.b | has(\"nope\")", 0, "false\n");
    check_eval(doc, "$.b | type()", 0, "\"object\"\n");
    check_eval(doc, "$.a | type()", 0, "\"array\"\n");
    check_eval(doc, "$.alone | not()", 0, "true\n");
    check_eval(doc, "$.a[0] | ftos()", 0, "\"1\"\n");
    check_eval(doc, "$.a[0] | floor()", 0, "1\n");
    check_eval(doc, "$.a, $.b", 0, "[1,2,3,4,5]\n{\"tag\":\"data\"}\n");
    check_eval(doc, "$.a[1:3] | length()", 0, "2\n");
    check_eval(doc, "$.missing[*]", 0, "");
    check_eval(doc, "$.list[*].x", 0, "1\n2\n");
    check_eval(doc, "$.alone", 0, "null\n");
    check_eval(doc, "$.a[10]?", 0, "null\n");
    check_eval(doc, "$.a[10]", 0, "");
    check_eval("{\"o\":{\"k\":\"banana\",\"banana\":7}}", "$.o[.k]", 0, "7\n");
    check_eval(doc, "$.a | $[0]", 0, "1\n");
    check_eval("{\"s\":\"z\"}", "$.s | length()", 0, "1\n");
    check_eval("{\"s\":\"\\u00e9\"}", "$.s", 1, "\xc3\xa9\n");
    check_eval("[0,1,\"\",\"x\",[],{},null,false,true]",
               "$ | select(.)", 0, "[1,\"x\",true]\n");
    check_eval("0", "$ | not()", 0, "true\n");
    check_eval("\"\"", "$ | not()", 0, "true\n");
    check_eval("[]", "$ | not()", 0, "true\n");
    check_eval("{}", "$ | not()", 0, "true\n");
    check_eval("false", "$ | not()", 0, "true\n");

    /* ---- comparisons ---- */
    const char *person = "{\"age\":21,\"name\":\"Ada\",\"ok\":true}";
    check_eval(person, "$.age > 20", 0, "true\n");
    check_eval(person, "$.age > 21", 0, "false\n");
    check_eval(person, "$.age >= 21", 0, "true\n");
    check_eval(person, "$.age < 21", 0, "false\n");
    check_eval(person, "$.age <= 21", 0, "true\n");
    check_eval(person, "$.age == 21", 0, "true\n");
    check_eval(person, "$.age != 21", 0, "false\n");
    check_eval(person, "$.name == \"Ada\"", 0, "true\n");
    check_eval(person, "$.name < \"Bob\"", 0, "true\n");
    check_eval(person, "$.ok == true", 0, "true\n");
    check_eval(person, "$.ok == false", 0, "false\n");
    check_eval(person, "$.missing == null", 0, "");
    check_eval("[{\"age\":17},{\"age\":30}]", "$ | select(.age > 21)", 0,
               "[{\"age\":30}]\n");
    check_eval("[{\"age\":17},{\"age\":30}]", "$ | select(.age >= 17)",
               0, "[{\"age\":17},{\"age\":30}]\n");

    /* ---- boolean operators ---- */
    check_eval(person, "$.age > 20 and $.ok", 0, "true\n");
    check_eval(person, "$.age > 21 and $.ok", 0, "false\n");
    check_eval(person, "$.age > 21 or $.ok", 0, "true\n");
    check_eval(person, "$.missing == null or $.ok", 0, "true\n");
    check_eval(person, "$.missing == 1 and $.ok", 0, "false\n");
    check_eval("null", "true or false and false", 0, "true\n");
    check_eval("[{\"age\":17,\"ok\":true},{\"age\":30,\"ok\":false}]",
               "$ | select(.age > 21 and .ok)", 0, "[]\n");

    /* ---- map / add / contains / strings ---- */
    const char *team =
        "{\"users\":[{\"name\":\"Ada\",\"age\":36},"
        "{\"name\":\"Linus\",\"age\":17}]}";
    check_eval(team, "$.users | map(.name)", 0, "[\"Ada\",\"Linus\"]\n");
    check_eval(team, "$.users | select(.age > 21) | map(.name)", 0,
               "[\"Ada\"]\n");
    check_eval("{\"a\":1,\"b\":2}", "$ | map(.)", 0, "[1,2]\n");
    check_eval("[1,2,3]", "$ | add()", 0, "6\n");
    check_eval("[\"a\",\"b\"]", "$ | add()", 0, "\"ab\"\n");
    check_eval("[[1],[2],[]]", "$ | add()", 0, "[1,2]\n");
    check_eval("[{\"a\":1},{\"b\":2}]", "$ | add()", 0,
               "{\"a\":1,\"b\":2}\n");
    check_eval("[]", "$ | add()", 0, "null\n");
    check_eval("\"hello\"", "$ | contains(\"ell\")", 0, "true\n");
    check_eval("\"hello\"", "$ | contains(\"xyz\")", 0, "false\n");
    check_eval("[1,2,3]", "$ | contains(2)", 0, "true\n");
    check_eval("[1,2,3]", "$ | contains(4)", 0, "false\n");
    check_eval("[[1,2],[3]]", "$ | contains(2)", 0, "false\n");
    check_eval("{\"a\":{\"x\":1,\"y\":2,\"sub\":{\"x\":1}}}",
               "$.a | contains(.sub)", 0, "true\n");
    check_eval("{\"a\":{\"x\":2,\"sub\":{\"x\":1}}}",
               "$.a | contains(.sub)", 0, "false\n");
    check_eval("\"hello\"", "$ | startswith(\"he\")", 0, "true\n");
    check_eval("\"hello\"", "$ | startswith(\"lo\")", 0, "false\n");
    check_eval("\"hello\"", "$ | endswith(\"lo\")", 0, "true\n");
    check_eval("\"hello\"", "$ | endswith(\"\")", 0, "true\n");
    check_eval("[\"a\",1,null,\"b\"]", "$ | join(\"-\")", 0, "\"a-1--b\"\n");
    check_eval("\"a,b,,c\"", "$ | split(\",\")", 0,
               "[\"a\",\"b\",\"\",\"c\"]\n");
    check_eval("\"abc\"", "$ | split(\"\")", 0, "[\"a\",\"b\",\"c\"]\n");

    /* ---- unique / group_by / flatten / to_entries ---- */
    check_eval("[3,1,2,3,1]", "$ | unique()", 0, "[1,2,3]\n");
    check_eval("[\"b\",\"a\",\"b\"]", "$ | unique()", 0, "[\"a\",\"b\"]\n");
    check_eval("[{\"a\":1},{\"a\":1},{\"a\":2}]", "$ | unique()", 0,
               "[{\"a\":1},{\"a\":2}]\n");
    check_eval("[]", "$ | unique()", 0, "[]\n");
    check_eval("\"x\"", "$ | unique()", 0, "");
    check_eval("[{\"k\":\"b\"},{\"k\":\"a\"},{\"k\":\"b\"}]",
               "$ | group_by(.k)", 0,
               "[[{\"k\":\"a\"}],[{\"k\":\"b\"},{\"k\":\"b\"}]]\n");
    check_eval("[1,2,3]", "$ | group_by(. > 1)", 0,
               "[[1],[2,3]]\n");
    check_eval("[]", "$ | group_by(.)", 0, "[]\n");
    check_eval("[[1,[2,[3]]],4]", "$ | flatten()", 0, "[1,2,3,4]\n");
    check_eval("[[],[[]]]", "$ | flatten()", 0, "[]\n");
    check_eval("{\"a\":1,\"b\":2}", "$ | to_entries()", 0,
               "[{\"key\":\"a\",\"value\":1},{\"key\":\"b\",\"value\":2}]\n");
    check_eval("[10,20]", "$ | to_entries()", 0,
               "[{\"key\":0,\"value\":10},{\"key\":1,\"value\":20}]\n");
    check_eval("null", "$ | to_entries()", 0, "");

    /* ---- from_entries / reverse / test ---- */
    check_eval("[{\"key\":\"a\",\"value\":1},{\"key\":\"b\",\"value\":2}]",
               "$ | from_entries()", 0, "{\"a\":1,\"b\":2}\n");
    check_eval("[]", "$ | from_entries()", 0, "{}\n");
    check_eval("[{\"key\":0,\"value\":\"a\"},{\"key\":1,\"value\":\"b\"}]",
               "$ | from_entries()", 0, "{\"0\":\"a\",\"1\":\"b\"}\n");
    check_eval("[10,20]", "$ | to_entries() | from_entries()", 0,
               "{\"0\":10,\"1\":20}\n");
    check_eval("[{\"value\":1}]", "$ | from_entries()", 0, "");
    check_eval("[1,2,3]", "$ | reverse()", 0, "[3,2,1]\n");
    check_eval("[]", "$ | reverse()", 0, "[]\n");
    check_eval("\"abc\"", "$ | reverse()", 0, "\"cba\"\n");
    check_eval("\"\"", "$ | reverse()", 0, "\"\"\n");
    check_eval("\"caf\\u00e9\"", "$ | reverse()", 0,
               "\"\xc3\xa9" "fac\"\n");
    check_eval("\"hello\"", "$ | test(\"^h.*o$\")", 0, "true\n");
    check_eval("\"hello\"", "$ | test(\"^x\")", 0, "false\n");
    check_eval("\"hello\"", "$ | test(\"[aeiou]\")", 0, "true\n");
    check_eval("\"hello\"", "$ | test(\"(\")", 0, "");
    check_eval("\"abc123\"", "$ | match(\"[a-z]+\")", 0,
               "{\"offset\":0,\"length\":3,\"string\":\"abc\",\"captures\":[]}\n");
    check_eval("\"abc123\"", "$ | match(\"([a-z]+)([0-9]+)\")", 0,
               "{\"offset\":0,\"length\":6,\"string\":\"abc123\","
               "\"captures\":[{\"offset\":0,\"length\":3,\"string\":\"abc\","
               "\"name\":null},{\"offset\":3,\"length\":3,\"string\":\"123\","
               "\"name\":null}]}\n");
    check_eval("\"abc\"", "$ | match(\"z\")", 0, "");
    check_eval("\"abc\"", "$ | match(\"(\")", 0, "");
    check_eval("\"a\"", "$ | match(\"(a)(x)?\")", 0,
               "{\"offset\":0,\"length\":1,\"string\":\"a\","
               "\"captures\":[{\"offset\":0,\"length\":1,\"string\":\"a\","
               "\"name\":null},{\"offset\":-1,\"length\":0,"
               "\"string\":null,\"name\":null}]}\n");
    check_eval("\"caf\xc3\xa9\"", "$ | match(\"\xc3\xa9\")", 0,
               "{\"offset\":3,\"length\":1,\"string\":\"\xc3\xa9\","
               "\"captures\":[]}\n");

    /* ---- keys_unsorted / min_by / max_by / fromjson / tojson ---- */
    check_eval("{\"b\":2,\"a\":1}", "$ | keys_unsorted()", 0,
               "[\"b\",\"a\"]\n");
    check_eval("{\"b\":2,\"a\":1}", "$ | keys()", 0, "[\"a\",\"b\"]\n");
    check_eval("[{\"n\":\"a\",\"v\":3},{\"n\":\"b\",\"v\":1}]",
               "$ | min_by(.v)", 0, "{\"n\":\"b\",\"v\":1}\n");
    check_eval("[{\"n\":\"a\",\"v\":3},{\"n\":\"b\",\"v\":1}]",
               "$ | max_by(.v)", 0, "{\"n\":\"a\",\"v\":3}\n");
    check_eval("[3,1,2]", "$ | min_by(.)", 0, "1\n");
    check_eval("[3,1,2]", "$ | max_by(.)", 0, "3\n");
    check_eval("[]", "$ | min_by(.)", 0, "");
    check_eval("\"{\\\"a\\\":1}\"", "$ | fromjson()", 0, "{\"a\":1}\n");
    check_eval("\"  [1,2]  \"", "$ | fromjson()", 0, "[1,2]\n");
    check_eval("\"not json\"", "$ | fromjson()", 0, "");
    check_eval("\"{\\\"a\\\":1} trailing\"", "$ | fromjson()", 0, "");
    check_eval("{\"b\":2,\"a\":[1,2]}", "$ | tojson()", 0,
               "\"{\\\"b\\\":2,\\\"a\\\":[1,2]}\"\n");
    check_eval("\"{\\\"a\\\":1}\"", "$ | fromjson() | tojson()", 0,
               "\"{\\\"a\\\":1}\"\n");

    /* ---- CLI variables ($name) ---- */
    query_set_var("who", json_string_new("world"));
    query_set_var("n", parse_or_die("42"));
    query_set_var("obj", parse_or_die("{\"x\":[1,2]}"));
    check_eval("null", "$who", 0, "\"world\"\n");
    check_eval("null", "$n", 0, "42\n");
    check_eval("null", "$obj", 0, "{\"x\":[1,2]}\n");
    check_eval("{\"a\":1}", "$obj | .x | length()", 0, "2\n");
    check_eval("null", "$missing", 0, "");
    check_eval("null", "$who | length()", 0, "5\n");
    check_eval("null", "$who | tojson()", 0, "\"\\\"world\\\"\"\n");

    /* ---- tostring / tonumber / case / trimstr ---- */
    check_eval("[1,\"a\",true,null,[1,2],{\"a\":1}]",
               "$ | map(tostring())", 0,
               "[\"1\",\"a\",\"true\",\"null\",\"[1,2]\",\"{\\\"a\\\":1}\"]\n");
    check_eval("\"x\"", "$ | tostring()", 0, "\"x\"\n");
    check_eval("\"42\"", "$ | tonumber()", 0, "42\n");
    check_eval("\" 3.5 \"", "$ | tonumber()", 0, "3.5\n");
    check_eval("7", "$ | tonumber()", 0, "7\n");
    check_eval("\"x\"", "$ | tonumber()", 0, "");
    check_eval("true", "$ | tonumber()", 0, "");
    check_eval("\"AbC\"", "$ | ascii_downcase()", 0, "\"abc\"\n");
    check_eval("\"AbC\"", "$ | ascii_upcase()", 0, "\"ABC\"\n");
    check_eval("\"caf\\u00e9\"", "$ | ascii_upcase()", 0,
               "\"CAF\xc3\xa9\"\n");
    check_eval("\"foobar\"", "$ | ltrimstr(\"foo\")", 0, "\"bar\"\n");
    check_eval("\"foobar\"", "$ | rtrimstr(\"bar\")", 0, "\"foo\"\n");
    check_eval("\"foobar\"", "$ | ltrimstr(\"x\")", 0, "\"foobar\"\n");
    check_eval("\"foobar\"", "$ | rtrimstr(\"x\")", 0, "\"foobar\"\n");
    check_eval("\"aaa\"", "$ | ltrimstr(\"aa\")", 0, "\"a\"\n");
    check_eval("\"aaa\"", "$ | rtrimstr(\"aa\")", 0, "\"a\"\n");

    printf("unit: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
