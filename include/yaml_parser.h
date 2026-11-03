#ifndef YAML_PARSER_H
#define YAML_PARSER_H

#include "model.h"

#include <stddef.h>

typedef struct{
    char **items;
    size_t count;
} error_list_t;

//Adds one message, use '...' to say it can have multiple parameters
void error_list_add(error_list_t *errors, const char *format, ...);

void error_list_free(error_list_t *errors);

//Reads yaml_len bytes of YAML and builds a pipeline_t
//Returns NULL and adds to errors if it can't be read.
pipeline_t *yaml_parse_pipeline(const char *yaml, size_t yaml_len, error_list_t *errors);


#endif //YAML_PARSER_H