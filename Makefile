CC      = gcc
CFLAGS  = -std=c2x -Wall -Wextra -pedantic -O2 -pthread -fopenmp -Iinclude
BUILD   = build
BIN     = bin

ifeq ($(SIN_PROTECCION),1)
CFLAGS += -DSIN_PROTECCION
endif

OBJS_CORE   = $(BUILD)/model.o $(BUILD)/state.o $(BUILD)/dag.o \
              $(BUILD)/job_queue.o $(BUILD)/scheduler.o $(BUILD)/registry.o \
              $(BUILD)/logstore.o
OBJS_NET    = $(BUILD)/net_util.o $(BUILD)/protocol.o
OBJS_SERVER = $(BUILD)/commands.o $(BUILD)/pipelined.o
OBJS_YAML   = $(BUILD)/yaml_parser.o $(BUILD)/validator.o
OBJS_EXEC   = $(BUILD)/exec_job_execute.o $(BUILD)/artifacts.o
OBJS_TASKS  = $(BUILD)/task_sleep_cpu.o $(BUILD)/task_data.o

ALL_BINS    = $(BIN)/pipelined $(BIN)/pipectl \
$(BIN)/test_core $(BIN)/test_scheduler $(BIN)/test_registry \
$(BIN)/test_commands $(BIN)/test_parser $(BIN)/test_determinism

all: $(ALL_BINS)

$(BIN)/pipelined: $(BUILD)/pipelined.o $(OBJS_NET) $(OBJS_CORE) \
                  $(OBJS_EXEC) $(OBJS_TASKS) $(OBJS_YAML) $(BUILD)/commands.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ -lyaml

$(BIN)/pipectl: $(BUILD)/pipectl.o $(OBJS_NET) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_core: $(BUILD)/test_core.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_scheduler: $(BUILD)/test_scheduler.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_commands: $(BUILD)/test_commands.o $(BUILD)/commands.o $(OBJS_YAML) $(OBJS_NET) $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ -lyaml

$(BIN)/test_determinism: $(BUILD)/test_determinism.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/test_parser: $(BUILD)/test_parser.o $(OBJS_YAML) $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ -lyaml

$(BIN)/test_registry: $(BUILD)/test_registry.o $(OBJS_CORE) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

test: all
	@echo "Ejecutando suite de pruebas local..."
	@$(BIN)/test_core && $(BIN)/test_scheduler && $(BIN)/test_registry && \
	$(BIN)/test_commands && $(BIN)/test_parser && $(BIN)/test_determinism

# --- Pattern rules for each source directory ---

$(BUILD)/%.o: src/server/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/core/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/client/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Explicit rules for src/exec (prefix "exec_" to avoid name collisions) ---

$(BUILD)/exec_job_execute.o: src/exec/job_execute.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/logstore.o: src/exec/logstore.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/artifacts.o: src/exec/artifacts.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Explicit rules for src/tasks ---

$(BUILD)/task_sleep_cpu.o: src/tasks/task_sleep_cpu.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/task_data.o: src/tasks/task_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD) $(BIN):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BIN)

.PHONY: all test clean test-race


test-race:
	$(MAKE) clean
	$(MAKE) SIN_PROTECCION=1 $(BIN)/test_determinism
	-$(BIN)/test_determinism
	$(MAKE) clean