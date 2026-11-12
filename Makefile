CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man/man1
BASHCOMP ?= $(PREFIX)/share/bash-completion/completions
ZSHCOMP  ?= $(PREFIX)/share/zsh/site-functions
FISHCOMP ?= $(PREFIX)/share/fish/vendor_completions.d

SRC := src/main.c src/json.c src/query.c
OBJ := $(SRC:src/%.c=obj/%.o)
VERSION := $(shell sed -n 's/.*CJQ_VERSION "\([^"]*\)".*/\1/p' src/main.c)
TAG := v$(VERSION)

all: cjq

obj/%.o: src/%.c src/json.h src/query.h
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

cjq: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) -lm

tests/unit: tests/unit.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/unit.c src/json.c src/query.c $(LDFLAGS) -lm

test: cjq tests/unit
	@echo "== unit =="
	./tests/unit
	@echo "== e2e =="
	./tests/run_tests.sh

# Static analysis with GCC's analyzer.  Leak warnings are disabled because
# the arena is intentionally one-shot; everything else is treated as an error
# by the caller's -Werror.
analyzer:
	@mkdir -p build/analyzer
	$(CC) $(CFLAGS) -fanalyzer -Wno-analyzer-malloc-leak -Isrc \
	      -c -o build/analyzer/json.o src/json.c
	$(CC) $(CFLAGS) -fanalyzer -Wno-analyzer-malloc-leak -Isrc \
	      -c -o build/analyzer/query.o src/query.c
	$(CC) $(CFLAGS) -fanalyzer -Wno-analyzer-malloc-leak -Isrc \
	      -c -o build/analyzer/main.o src/main.c
	@echo "analyzer: clean"

# Annotated release tag derived from CJQ_VERSION.
tag:
	@test -n "$(VERSION)" || { echo "cannot read CJQ_VERSION" >&2; exit 1; }
	@test -z "$$(git status --porcelain)" || \
	    { echo "working tree is dirty; commit first" >&2; exit 1; }
	git tag -a "$(TAG)" -m "cjq $(VERSION)"
	git push origin "$(TAG)"
	@echo "tagged and pushed $(TAG)"

# ASan + UBSan run, for CI or when chasing a bug.
#
# cjq is a one-shot CLI: values and AST nodes are intentionally never freed,
# so LeakSanitizer would flag every allocation.  Keep ASan for memory errors
# but disable leak detection; make UBSan failures fatal.
ASAN_OPTIONS  ?= detect_leaks=0
UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1

sanitizer:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
	      -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o build/cjq $(SRC) $(LDFLAGS) -lm
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
	      -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -Isrc -o build/unit tests/unit.c src/json.c src/query.c $(LDFLAGS) -lm
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" \
	    ./build/unit
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" \
	    CJQ_BIN=$(CURDIR)/build/cjq ./tests/run_tests.sh

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR) $(DESTDIR)$(BASHCOMP) \
	           $(DESTDIR)$(ZSHCOMP) $(DESTDIR)$(FISHCOMP)
	install -m 0755 cjq $(DESTDIR)$(BINDIR)/cjq
	install -m 0644 man/cjq.1 $(DESTDIR)$(MANDIR)/cjq.1
	install -m 0644 completions/cjq.bash $(DESTDIR)$(BASHCOMP)/cjq
	install -m 0644 completions/_cjq $(DESTDIR)$(ZSHCOMP)/_cjq
	install -m 0644 completions/cjq.fish $(DESTDIR)$(FISHCOMP)/cjq.fish

fmt:
	clang-format -i src/*.c src/*.h tests/unit.c

clean:
	rm -rf obj cjq build tests/unit

.PHONY: all test sanitizer analyzer tag install fmt clean
