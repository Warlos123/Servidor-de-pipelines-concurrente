#define _POSIX_C_SOURCE 200809L

#include "yaml_parser.h"

#include <yaml.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MESSAGE_MAX 256

//ERROR LIST
void error_list_add(error_list_t *errors, const char *format, ...){
    char message[MESSAGE_MAX];

    //variable arguments (...) 
    va_list args;
    va_start(args, format);
    //va_list, writes in string, max size n
    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    //Create new slot in array of messages.
    //realloc copies an existing block into a new size block with the same content.
    char **items = realloc(errors->items, (errors->count + 1) * sizeof *items);

    if(items == NULL)
        return;

    errors->items = items;

    char *copy = strdup(message);

    if(copy == NULL)
        return;

    errors->items[errors->count] = copy;
    errors->count++;
}

void error_list_free(error_list_t *errors){
    for(size_t i = 0; i < errors->count; i++)
        free(errors->items[i]);

    free(errors->items);
    errors->items = NULL;
    errors->count = 0;
}


//PARSER 
pipeline_t *yaml_parse_pipeline(const char *yaml, size_t yaml_len, error_list_t *errors){
    yaml_parser_t parser;
    yaml_document_t document; 

    if(!yaml_parser_initialize(&parser)){
        error_list_add(errors, "out of memory while reading YAML");
        return NULL;
    }

    //reads from the buffer
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, yaml_len);

    //Tree of nodes(fails if it's not a valid YAML)
    if(!yaml_parser_load(&parser, &document)){
        //Say where is the error
        error_list_add(errors, "line %d: invalid YAML: %s", (int)parser.problem_mark.line + 1, parser.problem);
        yaml_parser_delete(&parser);
        return NULL;
    }

    pipeline_t *pipeline = NULL;
    yaml_node_t *root = yaml_document_get_root_node(&document);

    //No root
    if(root == NULL)
        error_list_add(errors, "the YAML is empty");

    //if the root is not a map, then its not a pipeline
    else if(root->type != YAML_MAPPING_NODE)
        error_list_add(errors, "line %d: the pipeline must be a map with 'stages'", (int)root->start_mark.line + 1);

    //valid pipeline
    else{
        //empty pipeline
        pipeline = calloc(1, sizeof *pipeline);

        if(pipeline == NULL)
            error_list_add(errors, "out of memory while reading YAML");

        //NEED TO IMPLEMENT FILL THE PIPELINE 
    }

    //Free the tree
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    return pipeline;

}