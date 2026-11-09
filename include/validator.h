#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "model.h"
#include "yaml_parser.h"

void validate_pipeline(const pipeline_t *pipeline, error_list_t *errors);

#endif //VALIDATOR_H