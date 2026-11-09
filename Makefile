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

# ASan + UBSan run, for CI or when chasing a bug.
sanitizer:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
	      -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o build/cjq $(SRC) $(LDFLAGS) -lm
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
	      -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o build/unit tests/unit.c src/json.c src/query.c $(LDFLAGS) -lm
	./build/unit
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

.PHONY: all test sanitizer install fmt clean
