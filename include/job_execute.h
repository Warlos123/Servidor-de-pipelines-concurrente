#ifndef JOB_EXECUTE_H
#define JOB_EXECUTE_H

#include "model.h"

//Runs all the steps of one job and returns its final status.
//Implemented by C.
status_t execute_job(run_t *run, size_t job, void *work_directory);

#endif //JOB_EXECUTE_H