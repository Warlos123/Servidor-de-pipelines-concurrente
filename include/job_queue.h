#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include "model.h"

#include <semaphore.h>

typedef struct job_node {
    run_t *run;
    size_t job;
    struct job_node *next;
} job_node_t;

typedef struct {
    job_node_t *head;
    job_node_t *tail;
    size_t size;
    bool closed;
    omp_lock_t lock;
    sem_t items;
} job_queue_t;

int job_queue_init(job_queue_t *queue);

void job_queue_destroy(job_queue_t *queue);

int job_queue_push(job_queue_t *queue, run_t *run, size_t job);

bool job_queue_pop(job_queue_t *queue, run_t **run, size_t *job);

void job_queue_close(job_queue_t *queue, size_t n_waiters);

size_t job_queue_size(job_queue_t *queue);

#endif