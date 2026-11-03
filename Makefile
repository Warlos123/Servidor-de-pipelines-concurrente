CC      = gcc
CFLAGS  = -std=c2x -Wall -Wextra -pedantic -O2 -pthread -fopenmp -Iinclude
BUILD   = build
BIN     = bin

ifeq ($(SIN_PROTECCION),1)
CFLAGS += -DSIN_PROTECCION
endif

OBJS_CORE   = $(BUILD)/model.o $(BUILD)/state.o $(BUILD)/dag.o $(BUILD)/job_queue.o $(BUILD)/scheduler.o
OBJS_NET    = $(BUILD)/net_util.o $(BUILD)/protocol.o
OBJS_SERVER = $(BUILD)/commands.o $(BUILD)/job_execute.o

ALL_BINS    = $(BIN)/pipelined $(BIN)/pipectl \
$(BIN)/test_core $(BIN)/test_scheduler $(BIN)/test_parser

all: $(ALL_BINS)

$(BIN)/pipelined: $(BUILD)/pipelined.o $(OBJS_NET) $(OBJS_CORE) $(OBJS_SERVER) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/pipectl: $(BUILD)/pipectl.o $(OBJS_NET) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_core: $(BUILD)/test_core.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_parser: $(BUILD)/test_parser.o $(BUILD)/yaml_parser.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ -lyaml 

test: all
	@echo "Ejecutando suite de pruebas local..."
	@$(BIN)/test_core && $(BIN)/test_scheduler && $(BIN)/test_parser

$(BUILD)/%.o: src/server/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/core/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/client/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD) $(BIN):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BIN)

.PHONY: all test clean