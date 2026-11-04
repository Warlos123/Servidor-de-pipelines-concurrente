#ifndef REGISTRY_H
#define REGISTRY_H

#include "model.h"
#include "scheduler.h"

#define REG_NOT_FOUND (-1)
#define REG_FINISHED (-2)

typedef struct {
    run_t **runs;
    size_t count;
    size_t capacity;
    omp_lock_t lock;
} registry_t;

int registry_init(registry_t *registry);

void registry_destroy(registry_t *registry);

int registry_submit(registry_t *registry, scheduler_t *scheduler, pipeline_t *pipeline, uint64_t *run_id);

int registry_status(registry_t *registry, uint64_t run_id, char *out, size_t size);

int registry_list(registry_t *registry, char *out, size_t size);

int registry_cancel(registry_t *registry, uint64_t run_id);

run_t *registry_find(registry_t *registry, uint64_t run_id);

void registry_close_logs(registry_t *registry);

#endif