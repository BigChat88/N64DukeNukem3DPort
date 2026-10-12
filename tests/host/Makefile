# Host tests of the N64 platform code that does not need the console:
#   make -C tests/host
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
TEST_CFLAGS = -Istubs -I../../src/game -fsanitize=address,undefined

BUILD = build
TESTS = test_savefs test_fx

all: test
.PHONY: all test clean

$(BUILD)/test_savefs: test_savefs.c ../../src/n64/n64_savefs.c stubs/libdragon.h
$(BUILD)/test_fx: test_fx.c ../../src/n64/n64_fx.c stubs/libdragon.h

$(addprefix $(BUILD)/,$(TESTS)):
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -o $@ $<

test: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $^; do echo "== $$t"; $$t; done

clean:
	rm -rf $(BUILD)
