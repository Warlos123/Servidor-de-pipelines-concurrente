#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "model.h"
#include "yaml_parser.h"

// Checks the rules that need the whole pipeline duplicate names, dependsOn that names something that doesn't exist, and cycles.
void validate_pipeline(const pipeline_t *pipeline, error_list_t *errors);

#endif //VALIDATOR_H