#ifndef COMMANDS_H
#define COMMANDS_H
 
#include "protocol.h"
#include "scheduler.h"
#include "registry.h"
#include <stddef.h>
 
//Executes an already validated command and writes the reply.
int command_execute(const command_t *command, const char *yaml, size_t yaml_len, char *body, size_t size);

void command_logs(const command_t *command, int file_descriptor);

// Stops accepting connections and starts the shutdown.
void server_request_shutdown(void);

// 0 once SHUTDOWN or Ctrl-C started the shutdown.
int server_is_running(void);

// Scheduler shared by all handlers.
scheduler_t *server_scheduler(void);

// Registry of every run, shared by all handlers.
registry_t *server_registry(void);
 
#endif //COMMANDS_H
 
