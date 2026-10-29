
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define JOB_NAME_MAX 64

#define MAX_YAML_BYTES (1024 * 1024) //1 MiB, so a client cant send a giant YAML and shutdown the server

#define MAX_RESPONSE_BYTES (16 * 1024 * 1024) //16 MiB

typedef enum {
    C_SUBMIT,
    C_STATUS,
    C_LOGS,
    C_CANCEL,
    C_LIST,
    C_SHUTDOWN,
    C_INVALID
} c_type_t;


typedef struct  {
    c_type_t type;
    unsigned long id;
    size_t body_len;
    char job[JOB_NAME_MAX];
    int follow; //1 if follow, 0 if not
    char error[64];
} command_t;


int protocol_parse(const char *line, command_t *out);

int protocol_send_response(int file_descriptor, int ok, const char *body);

int protocol_read_response(int file_descriptor, int *ok, char **body, size_t *len);

#endif //PROTOCOL_H