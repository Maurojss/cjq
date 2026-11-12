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

# tlines NAME QUERY INPUT EXPECTED FLAG...
# Like t, but INPUT is expanded with printf %b so \\n means a real newline.
tlines() {
    name=$1
    query=$2
    input=$3
    expected=$4
    shift 4
    CHECKS=$((CHECKS + 1))
    got=$(printf '%b' "$input" | "$BIN" "$@" "$query" 2>/dev/null)
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
t keys_unsorted '$. | keys_unsorted()' '{"b":2,"a":1}' '["b","a"]' -c
t min_by '$. | min_by(.v)' '[{"n":"a","v":3},{"n":"b","v":1}]' \
    '{"n":"b","v":1}' -c
t max_by '$. | max_by(.v)' '[{"n":"a","v":3},{"n":"b","v":1}]' \
    '{"n":"a","v":3}' -c
t fromjson '$. | fromjson()' '"{\"a\":1}"' '{"a":1}' -c
t fromjson_invalid '$. | fromjson()' '"nope"' '' -c
t tojson '$. | tojson()' '{"a":[1,2]}' '"{\"a\":[1,2]}"' -c
t tostring '$. | map(tostring())' '[1,"x",true,null]' \
    '["1","x","true","null"]' -c
t tonumber '$. | tonumber()' '"42"' '42' -c
t tonumber_trim '$. | tonumber()' '" 3.5 "' '3.5' -c
t tonumber_bad '$. | tonumber()' '"x"' '' -c
t ascii_downcase '$. | ascii_downcase()' '"AbC"' '"abc"' -c
t ascii_upcase '$. | ascii_upcase()' '"AbC"' '"ABC"' -c
t ltrimstr '$. | ltrimstr("foo")' '"foobar"' '"bar"' -c
t ltrimstr_miss '$. | ltrimstr("x")' '"foobar"' '"foobar"' -c
t rtrimstr '$. | rtrimstr("bar")' '"foobar"' '"foo"' -c
t rtrimstr_miss '$. | rtrimstr("x")' '"foobar"' '"foobar"' -c
t match '$. | match("[a-z]+")' '"abc123"' \
    '{"offset":0,"length":3,"string":"abc","captures":[]}' -c
t match_capture '$. | match("([a-z]+)([0-9]+)")' '"abc123"' \
    '{"offset":0,"length":6,"string":"abc123","captures":[{"offset":0,"length":3,"string":"abc","name":null},{"offset":3,"length":3,"string":"123","name":null}]}' -c
t match_none '$. | match("z")' '"abc"' '' -c
t range_one 'range(4)' 'null' '0\n1\n2\n3' -c
t range_two 'range(2; 5)' 'null' '2\n3\n4' -c
t range_step 'range(0; 7; 3)' 'null' '0\n3\n6' -c
t range_negative 'range(5; 0; -2)' 'null' '5\n3\n1' -c
t path_nested 'path(.a.b[1])' '{"a":{"b":[10,20]}}' '["a","b",1]' -c
t path_wildcard 'path(.a[*].x)' '{"a":[{"x":1},{"x":2}]}' '["a",0,"x"]\n["a",1,"x"]' -c
t del_key 'del(.a)' '{"a":1,"b":2}' '{"b":2}' -c
t del_index 'del(.a[1])' '{"a":[1,2,3]}' '{"a":[1,3]}' -c
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
exit_code nul_string 3 '"a\u0000b"' "$BIN" '$'
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
exit_code min_by_arity 4 '[]' "$BIN" '$. | min_by()'
exit_code max_by_arity 4 '[]' "$BIN" '$. | max_by()'
exit_code keys_unsorted_arity 4 '{}' "$BIN" '$. | keys_unsorted(1)'
exit_code fromjson_arity 4 '""' "$BIN" '$. | fromjson(1)'
exit_code tojson_arity 4 '{}' "$BIN" '$. | tojson(1)'
exit_code tostring_arity 4 '{}' "$BIN" '$. | tostring(1)'
exit_code tonumber_arity 4 '{}' "$BIN" '$. | tonumber(1)'
exit_code ascii_downcase_arity 4 '{}' "$BIN" '$. | ascii_downcase(1)'
exit_code ascii_upcase_arity 4 '{}' "$BIN" '$. | ascii_upcase(1)'
exit_code ltrimstr_arity 4 '{}' "$BIN" '$. | ltrimstr()'
exit_code rtrimstr_arity 4 '{}' "$BIN" '$. | rtrimstr()'
exit_code match_arity 4 '{}' "$BIN" '$. | match()'
exit_code range_arity 4 '{}' "$BIN" '$. | range()'
exit_code path_arity 4 '{}' "$BIN" '$. | path()'
exit_code del_arity 4 '{}' "$BIN" '$. | del()'
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

# ---- CLI variables -------------------------------------------------------
t var_arg '$who' 'null' 'world' -r --arg who world
t var_arg_empty '$who | length()' 'null' '0' --arg who ''
t var_arg_json '$n' 'null' '42' -c --argjson n 42
t var_arg_json_obj '$obj.x[1]' 'null' '2' -c --argjson obj '{"x":[1,2]}'
t var_arg_json_null '$obj' 'null' 'null' -c --argjson obj null
t var_missing '$nope' '{"a":1}' '' -c
t var_pipe '$who | length()' 'null' '5' --arg who world
exit_code bad_argjson 2 'null' "$BIN" --argjson n nope '$n'
exit_code bad_argjson_trailing 2 'null' "$BIN" --argjson n '1 2' '$n'
exit_code missing_arg 2 'null' "$BIN" --arg name

# ---- raw input -----------------------------------------------------------
tlines raw_lines '.' 'alpha\nbeta\n' 'alpha\nbeta' -R -r
tlines raw_slurp '.' 'alpha\nbeta\n' 'alpha\nbeta' -R -s -r
tlines raw_length 'length()' 'abc\nde\n' '3\n2' -R
tlines raw_crlf '.' 'a\r\nb\r\n' 'a\nb' -R -r
tlines raw_empty '.' '' '' -R

# ---- nested-query limit ---------------------------------------------------
DEEPQ="${TMPDIR:-/tmp}/cjq-deep-$$.cjq"
if awk 'BEGIN{for(i=0;i<1000;i++)printf "map("; printf "."; for(i=0;i<1000;i++)printf ")"; print ""}' >"$DEEPQ" 2>/dev/null; then
    exit_code deep_query 4 '{}' "$BIN" -f "$DEEPQ"
    rm -f "$DEEPQ"
fi

# ---- help/version ---------------------------------------------------------
CHECKS=$((CHECKS + 1))
"$BIN" --version >/dev/null 2>&1 || report "--version exits 0"
CHECKS=$((CHECKS + 1))
"$BIN" --help >/dev/null 2>&1 || report "--help exits 0"

echo "e2e: $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
