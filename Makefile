CC      ?= gcc
BUILD   ?= build
BIN     := $(BUILD)/riskv
TESTBIN := $(BUILD)/test_cpu

CFLAGS  ?= -O2 -g
override CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
                   -Wmissing-prototypes -Isrc -MMD -MP

LIB_SRC := $(filter-out src/main.c,$(wildcard src/*.c))
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
MAIN_OBJ := $(BUILD)/main.o

.PHONY: all test asan-test examples clean help

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

$(TESTBIN): tests/test_cpu.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TESTBIN)
	./$(TESTBIN)

# Rebuild everything with AddressSanitizer + UBSan and run the suite (incl. fuzzer).
asan-test:
	$(MAKE) BUILD=build-asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

examples: $(BIN)
	$(BIN) examples/hello.hex
	$(BIN) -s examples/sum.hex; echo "exit code: $$?"
	-$(BIN) examples/fault.hex

clean:
	rm -rf build build-asan

help:
	@echo "make            build $(BIN)"
	@echo "make test       build and run unit tests + fuzzer"
	@echo "make asan-test  same, under ASan/UBSan"
	@echo "make examples   run the bundled example programs"

-include $(wildcard $(BUILD)/*.d)
