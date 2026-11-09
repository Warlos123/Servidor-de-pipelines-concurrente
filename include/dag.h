#ifndef DAG_H
#define DAG_H

#include "model.h"

int dag_build(run_t *run);

size_t dag_release_dependents(run_t *run, size_t job, size_t *ready);

#endif