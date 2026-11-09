#ifndef STATE_H
#define STATE_H

#include "model.h"

const char *status_name(status_t status);

bool status_is_final(status_t status);

bool status_can_transition(status_t from, status_t to);

int job_set_status(run_t *run, size_t job, status_t to);

status_t stage_aggregate(const run_t *run, size_t stage);

status_t run_aggregate(const run_t *run);

bool condition_met(condition_t condition, const status_t *deps_status, size_t n_deps);

#endif