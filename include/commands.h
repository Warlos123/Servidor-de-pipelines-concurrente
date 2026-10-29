#ifndef COMMANDS_H
#define COMMANDS_H
 
#include "protocol.h"
#include <stddef.h>
 
//Executes an already validated command and writes the reply.
int command_execute(const command_t *command, const char *yaml, size_t yaml_len, char *body, size_t size);

//Implemented in fdtd_server.c
//stops accepting connections and starts the shutdown.
void server_request_shutdown(void);
 
#endif //COMMANDS_H
 
