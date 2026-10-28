
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "job.h"

typedef enum {
    C_SUBMIT,
    C_STATUS,
    C_PEEK,
    C_RESULT,
    C_SHUTDOWN,
    C_INVALID
} c_type_t;


typedef struct  {
    c_type_t type;
    job_params_t params;
    unsigned long id;
    int i, j;
    char error[64];
} command_t;


int protocol_parse(const char *line, command_t *out);

#endif //PROTOCOL_H