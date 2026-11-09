#!/bin/sh
# End-to-end tests for cjq.  Set CJQ_BIN to test a specific binary.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${CJQ_BIN:-$ROOT/cjq}
CHECKS=0
FAILURES=0

if [ ! -x "$BIN" ]; then
    echo "e2e: no executable binary at $BIN" >&2
    exit 1
fi

report() {
    FAILURES=$((FAILURES + 1))
    echo "FAIL: $1" >&2
}

# t NAME QUERY INPUT EXPECTED FLAG...
t() {
    name=$1
    query=$2
    input=$3
    expected=$4
    shift 4
    CHECKS=$((CHECKS + 1))
    got=$(printf '%s' "$input" | "$BIN" "$@" "$query" 2>/dev/null)
    want=$(printf '%b' "$expected")
    if [ "$got" != "$want" ]; then
        report "$name"
        printf '  got:  %s\n' "$got" >&2
        printf '  want: %s\n' "$want" >&2
    fi
}

# exit_code NAME EXPECTED-CODE INPUT COMMAND...
exit_code() {
    name=$1
    want=$2
    input=$3
    shift 3
    CHECKS=$((CHECKS + 1))
    printf '%s' "$input" | "$@" >/dev/null 2>&1
    got=$?
    if [ "$got" -ne "$want" ]; then
        report "$name (exit $got, want $want)"
    fi
}

# ---- basic output formats -------------------------------------------------
t pretty '$.a' '{"a":[1,2]}' '[\n    1,\n    2\n]'
t compact '$.a' '{"a":[1,2]}' '[1,2]' -c
t raw '$.s' '{"s":"hello"}' 'hello' -r
t strings '$.s' '{"s":"hello"}' '"hello"'
t quoted_key '$.foo."two words"' '{"foo":{"two words":true}}' 'true' -c
t wildcard '$.a[*]' '{"a":["x","y"]}' 'x\ny' -r
t comma '$.a, $.b' '{"a":1,"b":2}' '1\n2' -c
t optional_miss '$.gone?' '{}' 'null'
t optional_absent '$.gone' '{}' ''
t number '$.n' '{"n":1.0}' '1'
t number_exp '$.n' '{"n":1e2}' '100'
t nested_pipe '$.list | sort(.x)[0].y' '{"list":[{"x":2,"y":9},{"x":1,"y":3}]}' '3' -c
t builtin_length '$.a | length()' '{"a":[1,2,3]}' '3'
t cmp_number '$.age > 20' '{"age":36}' 'true' -c
t cmp_equal '$.age == 36' '{"age":36}' 'true' -c
t cmp_string '$.name <= "Ada"' '{"name":"Ada"}' 'true' -c
t cmp_bool '$.ok != false' '{"ok":true}' 'true' -c
t select_cmp '$.users | select(.age > 21) | map(.name)' \
    '{"users":[{"name":"Ada","age":36},{"name":"Linus","age":17}]}' \
    '["Ada"]' -c
t map_builtin '$.users | map(.name)' \
    '{"users":[{"name":"Ada"},{"name":"Linus"}]}' \
    '["Ada","Linus"]' -c
t add_numbers '$. | add()' '[1,2,3,4]' '10' -c
t add_strings '$. | add()' '["a","b","c"]' '"abc"' -c
t contains_string '$. | contains("ell")' '"hello"' 'true' -c
t contains_array '$. | contains(2)' '[1,2,3]' 'true' -c
t startswith '$. | startswith("he")' '"hello"' 'true' -c
t endswith '$. | endswith("lo")' '"hello"' 'true' -c
t join_builtin '$. | join("-")' '["a",1,null,"b"]' '"a-1--b"' -c
t split_builtin '$. | split(",")' '"a,b,,c"' '["a","b","","c"]' -c
t bool_and '$.age > 20 and $.ok' '{"age":36,"ok":true}' 'true' -c
t bool_or '$.age > 40 or $.ok' '{"age":17,"ok":true}' 'true' -c
t bool_precedence 'true or false and false' 'null' 'true' -c
t select_bool '$ | select(.age > 21 and .ok) | map(.name)' \
    '[{"name":"Ada","age":36,"ok":true},{"name":"Linus","age":40,"ok":false}]' \
    '["Ada"]' -c
t unique_builtin '$. | unique()' '[3,1,3,2]' '[1,2,3]' -c
t group_by_builtin '$. | group_by(.k)' \
    '[{"k":"b","v":1},{"k":"a","v":2},{"k":"b","v":3}]' \
    '[[{"k":"a","v":2}],[{"k":"b","v":1},{"k":"b","v":3}]]' -c
t flatten_builtin '$. | flatten()' '[[1,[2,[3]]],4]' '[1,2,3,4]' -c
t to_entries_builtin '$. | to_entries()' '{"a":1,"b":2}' \
    '[{"key":"a","value":1},{"key":"b","value":2}]' -c
t from_entries_builtin '$. | from_entries()' \
    '[{"key":"a","value":1},{"key":"b","value":2}]' '{"a":1,"b":2}' -c
t reverse_array '$. | reverse()' '[1,2,3]' '[3,2,1]' -c
t reverse_string '$. | reverse()' '"abc"' '"cba"' -c
t test_regex '$. | test("^h.*o$")' '"hello"' 'true' -c
t test_regex_false '$. | test("^x")' '"hello"' 'false' -c
t sort_keys '$. ' '{"b":1,"a":2,"c":{"z":3,"y":4}}' \
    '{"a":2,"b":1,"c":{"y":4,"z":3}}' -S -c
t ascii_output '$.s' '{"s":"caf\u00e9"}' '"caf\\u00e9"' -a -c
t ascii_surrogate '$.s' '{"s":"\ud83d\ude00"}' '"\\ud83d\\ude00"' -a -c
t indent_two '$' '{"a":[1]}' '{\n  "a": [\n    1\n  ]\n}' --indent 2
t indent_zero '$' '{"a":[1]}' '{\n"a": [\n1\n]\n}' --indent=0
t indent_tab '$' '{"a":[1]}' '{\n\t"a": [\n\t\t1\n\t]\n}' --tab
t slurp_all '$' '1 2 {"a":3}' '[1,2,{"a":3}]' -s -c
t null_input '$' 'ignored' 'null' -n -c
t unicode_out '$.s' '{"s":"caf\u00e9"}' 'café' -r

# ---- input from a file ----------------------------------------------------
CHECKS=$((CHECKS + 1))
tmp=$(mktemp)
printf '{"a":42}' > "$tmp"
got=$("$BIN" -c '$.a' "$tmp")
rm -f "$tmp"
[ "$got" = "42" ] || report "reading a file"

# ---- colors ---------------------------------------------------------------
CHECKS=$((CHECKS + 1))
got=$(printf '{"a":1}' | "$BIN" -C -c '$.a')
esc=$(printf '\033')
case $got in
*"$esc"*) ;;
*) report "forced colors" ;;
esac

CHECKS=$((CHECKS + 1))
got=$(printf '{"a":1}' | "$BIN" -M -c '$.a')
[ "$got" = "1" ] || report "-M output"

CHECKS=$((CHECKS + 1))
got=$(printf '{"a":1}' | CJQ_NO_COLOR=1 "$BIN" -c '$.a')
[ "$got" = "1" ] || report "CJQ_NO_COLOR output"

# ---- join output ----------------------------------------------------------
CHECKS=$((CHECKS + 1))
got=$(printf '{"a":"xy"}' | "$BIN" -r -j '$.a' | wc -c | tr -d ' ')
[ "$got" = "2" ] || report "join raw output (got $got bytes)"
CHECKS=$((CHECKS + 1))
got=$(printf '{"a":1}' | "$BIN" -c -j '$.a' | wc -c | tr -d ' ')
[ "$got" = "1" ] || report "join json output (got $got bytes)"

# ---- exit codes -----------------------------------------------------------
exit_code ok 0 '{"a":1}' "$BIN" '$.a'
exit_code falsy 1 'false' "$BIN" -e '$'
exit_code falsy_zero 1 '0' "$BIN" -e '$'
exit_code falsy_empty_string 1 '""' "$BIN" -e '$'
exit_code falsy_empty_array 1 '[]' "$BIN" -e '$'
exit_code falsy_empty_object 1 '{}' "$BIN" -e '$'
exit_code truthy 0 'true' "$BIN" -e '$'
exit_code empty_result 1 '{}' "$BIN" -e '$.gone'
exit_code missing_query 2 '' "$BIN"
exit_code bad_json 3 '{bad' "$BIN" '$'
exit_code bad_query 4 '{}' "$BIN" 'name('
exit_code trailing_json 3 '{"a":1} {"b":2}' "$BIN" '$'
exit_code unknown_flag 2 '{}' "$BIN" -Z '$'
exit_code unknown_builtin 4 '{"a":1}' "$BIN" '$.a | nope()'
exit_code bad_arity 4 '{"a":1}' "$BIN" '$.a | length(1)'
exit_code map_arity 4 '[]' "$BIN" '$. | map()'
exit_code unique_arity 4 '[]' "$BIN" '$. | unique(1)'
exit_code group_by_arity 4 '[]' "$BIN" '$. | group_by()'
exit_code flatten_arity 4 '[]' "$BIN" '$. | flatten(1)'
exit_code from_entries_arity 4 '[]' "$BIN" '$. | from_entries(1)'
exit_code reverse_arity 4 '[]' "$BIN" '$. | reverse(1)'
exit_code test_arity 4 '"x"' "$BIN" '$. | test()'
exit_code missing_query_file 2 '{}' "$BIN" -f /nonexistent/cjq-query '$.a'
exit_code bad_indent 2 '{}' "$BIN" --indent 9 '$'
exit_code bad_indent_empty 2 '{}' "$BIN" --indent= '$'
exit_code missing_indent 2 '{}' "$BIN" --indent

# ---- query files ----------------------------------------------------------
QFILE="${TMPDIR:-/tmp}/cjq-query-$$.cjq"
IFILE="${TMPDIR:-/tmp}/cjq-input-$$.json"
if printf '%s' '$.a > 0' >"$QFILE" 2>/dev/null &&
   printf '%s' '{"a":1}' >"$IFILE" 2>/dev/null; then
    CHECKS=$((CHECKS + 1))
    got=$(printf '{"a":1}' | "$BIN" -c -f "$QFILE" 2>/dev/null)
    [ "$got" = "true" ] || report "-f reads the query from a file"
    CHECKS=$((CHECKS + 1))
    got=$("$BIN" -c --from-file="$QFILE" "$IFILE" 2>/dev/null)
    [ "$got" = "true" ] || report "--from-file= works with a JSON file"
    rm -f "$QFILE" "$IFILE"
fi

# ---- help/version ---------------------------------------------------------
CHECKS=$((CHECKS + 1))
"$BIN" --version >/dev/null 2>&1 || report "--version exits 0"
CHECKS=$((CHECKS + 1))
"$BIN" --help >/dev/null 2>&1 || report "--help exits 0"

echo "e2e: $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
