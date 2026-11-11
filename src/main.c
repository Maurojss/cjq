/*
 * main.c — cjq, a shell-friendly JSON query tool written in C99 with no
 * dependencies.  See query.h for the query language and json.h for the
 * value model.
 */

#define _POSIX_C_SOURCE 200809L

#include "json.h"
#include "query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CJQ_VERSION "1.2.0"

#define EXIT_OK 0
#define EXIT_FALSY 1
#define EXIT_USAGE 2
#define EXIT_INPUT 3
#define EXIT_QUERY 4

typedef struct
{
    const char *name;
    const char *value;
    int is_json;
} cli_var_t;

#define MAX_VARS 64

typedef struct
{
    const char *query_text;
    const char *query_file;
    const char *file;
    int compact, raw, slurp, exit_status, null_input;
    int want_color, no_color, help, version, option_end;
    int ascii, join, indent_set, sort_keys, raw_input;
    char indent[16];
    const char *positionals[2];
    int npos;
    cli_var_t vars[MAX_VARS];
    int nvars;
} flags_t;

static void print_help(FILE *out)
{
    fprintf(out,
        "cjq: query JSON from the shell — in C99, no dependencies\n"
        "\n"
        "usage: cjq [flags] QUERY [FILE]\n"
        "       pipeline | cjq QUERY\n"
        "\n"
        "flags:\n"
        "  -c --compact        print compact JSON, one line, no spaces\n"
        "  -r --raw            print string results without quotes\n"
        "  -j --join-output    no newline after each result\n"
        "  -a --ascii-output   escape non-ASCII characters as \\uXXXX\n"
        "  -s --slurp          gather every JSON value into one array\n"
        "  -R --raw-input      read raw text lines instead of JSON\n"
        "  -e --exit-status    exit 1 when the result is falsy or empty\n"
        "  -n --null-input     run the query against null, no input read\n"
        "  -C --color          force colors even when piped\n"
        "  -M --monochrome     never use colors\n"
        "     --no-color       never use colors (same as -M)\n"
        "     --indent N       indent pretty output with N spaces (0-8)\n"
        "     --tab            indent pretty output with one tab\n"
        "  -S --sort-keys      sort object keys in the output\n"
        "  -f --from-file F    read the query from file F\n"
        "     --arg N V        set $N to the string V\n"
        "     --argjson N V    set $N to the JSON value V\n"
        "  -h --help           show this help\n"
        "  -v --version        show the version\n"
        "\n"
        "query language:\n"
        "  $.a.b             descend into objects\n"
        "  $.b[2]            pick an array item\n"
        "  $.b[-1]           pick from the end\n"
        "  $.b[1:3]          slice; also [1:] and [:-1] without the end\n"
        "  $.\"two words\"     quoted object keys\n"
        "  $.b[*] or $.b[]   wildcard every item of an array or object\n"
        "  $.a?              optional member: emit null instead of a miss\n"
        "  $..uid            gather uid members at every depth\n"
        "  $.a, $.b          print several paths\n"
        "  $name             variable from --arg or --argjson\n"
        "  $.age > 21        comparisons: == != < <= > >=\n"
        "  $.ok and $.age > 18  boolean operators: and, or\n"
        "  $.a | length()    pipe into a builtin function\n"
        "\n"
        "builtins: length() keys() keys_unsorted() values() type() has(key)\n"
        "          select(cond) map(expr) add() contains(x) startswith(s)\n"
        "          endswith(s) join(sep) split(sep) unique() group_by(expr)\n"
        "          flatten() to_entries() from_entries() reverse() test(regex)\n"
        "          sort(field) max(field) min(field) sum(field)\n"
        "          min_by(expr) max_by(expr) fromjson() tojson()\n"
        "          first() last() floor() ceil() abs() sqrt() ftos() not()\n"
        "\n"
        "exit status: 0 success; 1 falsy result with -e; 2 usage error;\n"
        "             3 invalid JSON input; 4 invalid query\n");
}

static void usage_fail(const char *message, const char *detail)
{
    fprintf(stderr, "cjq: %s%s%s\n", message, detail ? ": " : "", detail ? detail : "");
    fprintf(stderr, "try `cjq --help`\n");
    exit(EXIT_USAGE);
}

static void to_positional(flags_t *f, char *arg);

static void parse_args(int argc, char **argv, flags_t *f)
{
    for (int i = 1; i < argc; ++i)
    {
        char *arg = argv[i];
        if (f->option_end)
        {
            to_positional(f, arg);
            continue;
        }
        if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0')
        {
            f->option_end = 1;
            continue;
        }
        if (arg[0] == '-' && arg[1] == '-')
        {
            const char *m = arg + 2;
            const char *value = NULL;
            if (!strncmp(m, "indent=", 7))
            {
                value = m + 7;
                m = "indent";
            }
            else if (!strncmp(m, "from-file=", 10))
            {
                value = m + 10;
                m = "from-file";
            }
            if (!strcmp(m, "indent"))
            {
                if (!value)
                {
                    if (i + 1 >= argc)
                        usage_fail("--indent requires a number", NULL);
                    value = argv[++i];
                }
                size_t width = 0;
                if (*value == '\0')
                    usage_fail("--indent expects a number from 0 to 8", value);
                for (const char *c = value; *c; ++c)
                {
                    if (*c < '0' || *c > '9' || width > 8)
                        usage_fail("--indent expects a number from 0 to 8",
                                   value);
                    width = width * 10 + (size_t)(*c - '0');
                }
                if (width > 8)
                    usage_fail("--indent expects a number from 0 to 8", value);
                memset(f->indent, ' ', width);
                f->indent[width] = '\0';
                f->indent_set = 1;
            }
            else if (!strcmp(m, "tab"))
            {
                f->indent[0] = '\t';
                f->indent[1] = '\0';
                f->indent_set = 1;
            }
            else if (!strcmp(m, "sort-keys"))
                f->sort_keys = 1;
            else if (!strcmp(m, "from-file"))
            {
                if (!value)
                {
                    if (i + 1 >= argc)
                        usage_fail("--from-file requires a path", NULL);
                    value = argv[++i];
                }
                if (*value == '\0')
                    usage_fail("--from-file requires a path", NULL);
                f->query_file = value;
            }
            else if (!strcmp(m, "compact"))
                f->compact = 1;
            else if (!strcmp(m, "raw"))
                f->raw = 1;
            else if (!strcmp(m, "join-output"))
                f->join = 1;
            else if (!strcmp(m, "ascii-output"))
                f->ascii = 1;
            else if (!strcmp(m, "slurp"))
                f->slurp = 1;
            else if (!strcmp(m, "raw-input"))
                f->raw_input = 1;
            else if (!strcmp(m, "arg") || !strcmp(m, "argjson"))
            {
                int is_json = !strcmp(m, "argjson");
                if (i + 2 >= argc)
                    usage_fail("--arg/--argjson require NAME and VALUE", NULL);
                if (f->nvars >= MAX_VARS)
                    usage_fail("too many --arg/--argjson variables", NULL);
                f->vars[f->nvars].name = argv[++i];
                f->vars[f->nvars].value = argv[++i];
                f->vars[f->nvars].is_json = is_json;
                f->nvars++;
            }
            else if (!strcmp(m, "exit-status"))
                f->exit_status = 1;
            else if (!strcmp(m, "null-input"))
                f->null_input = 1;
            else if (!strcmp(m, "color"))
                f->want_color = 1;
            else if (!strcmp(m, "monochrome") || !strcmp(m, "no-color"))
                f->no_color = 1;
            else if (!strcmp(m, "help"))
                f->help = 1;
            else if (!strcmp(m, "version"))
                f->version = 1;
            else
                usage_fail("unknown option", arg);
            continue;
        }
        if (arg[0] == '-' && arg[1] == 'f')
        {
            if (arg[2] != '\0')
                f->query_file = arg + 2;
            else
            {
                if (i + 1 >= argc)
                    usage_fail("-f requires a path", NULL);
                f->query_file = argv[++i];
            }
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0')
        {
            for (char *c = arg + 1; *c; ++c)
            {
                switch (*c)
                {
                case 'c': f->compact = 1; break;
                case 'r': f->raw = 1; break;
                case 'j': f->join = 1; break;
                case 'a': f->ascii = 1; break;
                case 's': f->slurp = 1; break;
                case 'R': f->raw_input = 1; break;
                case 'e': f->exit_status = 1; break;
                case 'n': f->null_input = 1; break;
                case 'C': f->want_color = 1; break;
                case 'M': f->no_color = 1; break;
                case 'S': f->sort_keys = 1; break;
                case 'h': f->help = 1; break;
                case 'v': f->version = 1; break;
                default:
                    {
                        char tmp[2] = {*c, '\0'};
                        usage_fail("unknown option", tmp);
                    }
                }
            }
            continue;
        }
        to_positional(f, arg);
    }
}

static void to_positional(flags_t *f, char *arg)
{
    if (f->npos >= 2)
        usage_fail("expected at most QUERY and FILE", arg);
    f->positionals[f->npos++] = arg;
}

static char *read_all(FILE *fh)
{
    size_t cap = 1 << 16;
    size_t len = 0;
    char *buf = xmalloc(cap);
    for (;;)
    {
        if (len + 1 == cap)
        {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, cap - 1 - len, fh);
        if (got == 0)
        {
            if (ferror(fh))
            {
                fprintf(stderr, "cjq: cannot read input\n");
                exit(EXIT_INPUT);
            }
            break;
        }
        len += got;
    }
    buf[len] = '\0';
    return buf;
}

static char *read_named_file(const char *path, const char *what)
{
    FILE *fh = fopen(path, "rb");
    if (!fh)
    {
        fprintf(stderr, "cjq: cannot open %s '%s'\n", what, path);
        exit(EXIT_USAGE);
    }
    char *buf = read_all(fh);
    fclose(fh);
    return buf;
}

static json_value *parse_cli_json(const char *text, const char *name)
{
    const char *end, *error;
    size_t pos;
    json_value *v = json_parse(text, &end, &error, &pos);
    if (!v)
    {
        fprintf(stderr, "cjq: --argjson %s: %s\n", name, error);
        exit(EXIT_USAGE);
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
        end++;
    if (*end != '\0')
    {
        fprintf(stderr, "cjq: --argjson %s: trailing characters\n", name);
        exit(EXIT_USAGE);
    }
    return v;
}

static void push_line(json_value *lines, const char *s, size_t len)
{
    if (len && s[len - 1] == '\r')
        len--;
    char *tmp = xmalloc(len + 1);
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    json_array_push(lines, json_string_new(tmp));
    free(tmp);
}

/* Split raw input into one string per line, like `jq -R`. */
static json_value *split_lines(const char *buf)
{
    json_value *lines = json_array_new();
    if (*buf == '\0')
        return lines;
    const char *start = buf;
    for (;;)
    {
        const char *nl = strchr(start, '\n');
        if (!nl)
        {
            push_line(lines, start, strlen(start));
            break;
        }
        push_line(lines, start, (size_t)(nl - start));
        start = nl + 1;
        if (*start == '\0')
            break;
    }
    return lines;
}

static void input_fail(const char *buf, size_t pos, const char *error)
{
    long line, col;
    json_pos(buf, pos, &line, &col);
    fprintf(stderr, "cjq: JSON: %s (line %ld, column %ld)\n", error, line, col);
    exit(EXIT_INPUT);
}

static int result_truthy(const json_value *v)
{
    return json_truthy(v);
}

int main(int argc, char **argv)
{
    flags_t f = {0};
    parse_args(argc, argv, &f);

    if (f.help)
    {
        print_help(stdout);
        return EXIT_OK;
    }
    if (f.version)
    {
        printf("cjq %s\n", CJQ_VERSION);
        return EXIT_OK;
    }
    if (f.query_file)
    {
        if (f.npos > 1)
            usage_fail("expected at most FILE with -f", NULL);
        f.file = f.npos ? f.positionals[0] : NULL;
        f.query_text = read_named_file(f.query_file, "query file");
    }
    else
    {
        if (f.npos > 0)
            f.query_text = f.positionals[0];
        if (f.npos > 1)
            f.file = f.positionals[1];
    }
    if (!f.query_text)
        usage_fail("a query is required", NULL);

    for (int i = 0; i < f.nvars; ++i)
    {
        json_value *v = f.vars[i].is_json
                            ? parse_cli_json(f.vars[i].value, f.vars[i].name)
                            : json_string_new(f.vars[i].value);
        query_set_var(f.vars[i].name, v);
    }

    json_set_colors(!f.no_color && !getenv("CJQ_NO_COLOR") &&
                    (f.want_color || isatty(1)));
    json_set_ascii(f.ascii);
    json_set_sort_keys(f.sort_keys);
    if (f.indent_set)
        json_set_indent(f.indent);

    json_value *root = NULL;
    json_value *lines = NULL;
    if (f.null_input)
    {
        root = json_null_new();
    }
    else
    {
        FILE *fh = stdin;
        if (f.file)
        {
            fh = fopen(f.file, "rb");
            if (!fh)
            {
                fprintf(stderr, "cjq: cannot open '%s'\n", f.file);
                return EXIT_INPUT;
            }
        }
        char *buf = read_all(fh);
        if (f.file)
            fclose(fh);

        if (f.raw_input)
        {
            if (f.slurp)
                root = json_string_new(buf);
            else
                lines = split_lines(buf);
        }
        else if (f.slurp)
        {
            root = json_array_new();
            const char *c = buf;
            for (;;)
            {
                while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')
                    c++;
                if (*c == '\0')
                    break;
                const char *jerr;
                size_t jpos;
                json_value *v = json_parse(c, &c, &jerr, &jpos);
                if (!v)
                    input_fail(buf, (size_t)(c - buf) + jpos, jerr);
                json_array_push(root, v);
            }
        }
        else
        {
            const char *end, *jerr;
            size_t jpos;
            root = json_parse(buf, &end, &jerr, &jpos);
            if (!root)
                input_fail(buf, jpos, jerr);
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
                end++;
            if (*end != '\0')
                input_fail(buf, (size_t)(end - buf),
                           "trailing characters after the JSON value");
        }
        free(buf);
    }

    const char *qerr;
    size_t qpos;
    hnode *ast = query_parse(f.query_text, &qerr, &qpos);
    if (!ast)
    {
        long line, col;
        json_pos(f.query_text, qpos, &line, &col);
        fprintf(stderr, "cjq: query: %s (line %ld, column %ld)\n", qerr,
                line, col);
        return EXIT_QUERY;
    }

    results_t rs;
    results_init(&rs);
    if (lines)
    {
        for (size_t i = 0; i < lines->count; ++i)
            query_eval(ast, lines->items[i], &rs);
    }
    else
        query_eval(ast, root, &rs);

    for (size_t i = 0; i < rs.n; ++i)
    {
        json_value *v = rs.v[i];
        if (f.raw && v && v->kind == JSON_STRING)
        {
            fputs(v->string, stdout);
            if (!f.join)
                fputc('\n', stdout);
        }
        else
        {
            json_print_ex(v, stdout, !f.compact, !f.join);
        }
    }

    if (f.exit_status)
    {
        int truthy = 0;
        for (size_t i = 0; i < rs.n; ++i)
            if (result_truthy(rs.v[i]))
                truthy = 1;
        return truthy ? EXIT_OK : EXIT_FALSY;
    }
    return EXIT_OK;
}
