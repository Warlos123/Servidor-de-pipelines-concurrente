#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "job_execute.h"
#include "job_context.h"
#include "logstore.h"
#include "artifacts.h"
#include "builtin_tasks.h"
#include "model.h"
#include "state.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


const char *var_map_get(const var_map_t *m, const char *key)
{
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].key, key) == 0) {
            return m->entries[i].value;
        }
    }
    return NULL;
}

int var_map_set(var_map_t *m, const char *key, const char *value)
{
   
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].key, key) == 0) {
            char *v = strdup(value);
            if (v == NULL) return -1;
            free(m->entries[i].value);
            m->entries[i].value = v;
            return 0;
        }
    }
   
    if (m->count == m->capacity) {
        size_t newcap = (m->capacity == 0) ? VAR_MAP_INITIAL : m->capacity * 2;
        var_entry_t *ne = realloc(m->entries, newcap * sizeof *ne);
        if (ne == NULL) return -1;
        m->entries  = ne;
        m->capacity = newcap;
    }
    char *k = strdup(key);
    char *v = strdup(value);
    if (k == NULL || v == NULL) {
        free(k); free(v);
        return -1;
    }
    m->entries[m->count].key   = k;
    m->entries[m->count].value = v;
    m->count++;
    return 0;
}

void var_map_destroy(var_map_t *m)
{
    for (size_t i = 0; i < m->count; i++) {
        free(m->entries[i].key);
        free(m->entries[i].value);
    }
    free(m->entries);
    m->entries  = NULL;
    m->count    = 0;
    m->capacity = 0;
}



static char *expand_vars(const char *src, const var_map_t *vars)
{
    if (src == NULL) return NULL;

    size_t src_len = strlen(src);
   
    size_t cap = src_len * 2 + 1;
    char  *out = malloc(cap);
    if (out == NULL) return NULL;
    size_t used = 0;

    for (size_t i = 0; i < src_len; ) {
        if (src[i] == '$' && src[i + 1] == '(') {
   
            size_t j = i + 2;
            while (j < src_len && src[j] != ')') j++;
            if (src[j] == ')') {
                size_t name_len = j - (i + 2);
                char  name[256];
                if (name_len < sizeof name) {
                    memcpy(name, src + i + 2, name_len);
                    name[name_len] = '\0';
                    const char *val = var_map_get(vars, name);
                    if (val == NULL) val = ""; /* unknown variable = empty */
                    size_t vlen = strlen(val);
                    
                    while (used + vlen >= cap) {
                        cap *= 2;
                        char *tmp = realloc(out, cap);
                        if (tmp == NULL) { free(out); return NULL; }
                        out = tmp;
                    }
                    memcpy(out + used, val, vlen);
                    used += vlen;
                    i = j + 1;
                    continue;
                }
            }
        }
        
        if (used + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(out, cap);
            if (tmp == NULL) { free(out); return NULL; }
            out = tmp;
        }
        out[used++] = src[i++];
    }
    out[used] = '\0';
    return out;
}


static int mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static char *make_workspace(const char *workdir, uint64_t run_id,
                             size_t job_index)
{
    size_t len = strlen(workdir) + 64;
    char  *p   = malloc(len);
    if (p == NULL) return NULL;
    snprintf(p, len, "%s/runs/%llu/job_%zu",
             workdir, (unsigned long long)run_id, job_index);
    if (mkdir_p(p) < 0) {
        free(p);
        return NULL;
    }
    return p;
}


#define READ_END  0
#define WRITE_END 1

typedef struct {
    pid_t   pid;
    int     pipe_fd;    
    pid_t   pgid;       
} child_t;

static int spawn_script(const char *cmd, const char *workspace,
                         child_t *out_child)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    
    if (fcntl(pipefd[WRITE_END], F_SETFD, FD_CLOEXEC) < 0) {
        close(pipefd[READ_END]);
        close(pipefd[WRITE_END]);
        return -1;
    }

    posix_spawn_file_actions_t fa;
    posix_spawnattr_t          attr;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&attr);

    posix_spawn_file_actions_adddup2(&fa, pipefd[WRITE_END], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pipefd[WRITE_END], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[READ_END]);

    
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0); 

    char *argv[] = { "sh", "-c", (char *)cmd, NULL };
    extern char **environ;


    posix_spawn_file_actions_addchdir_np(&fa, workspace);

    pid_t pid;
    int   rc = posix_spawn(&pid, "/bin/sh", &fa, &attr, argv, environ);

    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);

    close(pipefd[WRITE_END]); 

    if (rc != 0) {
        close(pipefd[READ_END]);
        return -1;
    }

    out_child->pid     = pid;
    out_child->pipe_fd = pipefd[READ_END];
    out_child->pgid    = pid; 
    return 0;
}

static void kill_child(const child_t *child)
{
    killpg(child->pgid, SIGTERM);
    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    killpg(child->pgid, SIGKILL);
}


static int drain_and_wait(child_t *child, job_context_t *ctx,
                          int timeout_seconds)
{
    char   line_buf[4096];
    size_t line_used = 0;
    char   read_buf[1024];
    ssize_t n;

    /* Track start time for timeout */
    struct timespec t_start;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* Set pipe non-blocking so we can poll */
    int flags = fcntl(child->pipe_fd, F_GETFL, 0);
    fcntl(child->pipe_fd, F_SETFL, flags | O_NONBLOCK);

    for (;;) {
        /* Check cancellation */
        if (atomic_load(ctx->cancel_flag)) {
            kill_child(child);
            break;
        }

        /* Check timeout */
        if (timeout_seconds > 0) {
            struct timespec t_now;
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            long elapsed = (long)(t_now.tv_sec - t_start.tv_sec);
            if (elapsed >= timeout_seconds) {
                log_append(ctx->log, "##[error] Timeout exceeded.");
                kill_child(child);
                break;
            }
        }

        n = read(child->pipe_fd, read_buf, sizeof read_buf - 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* No data yet; check if child finished */
                int status;
                pid_t w = waitpid(child->pid, &status, WNOHANG);
                if (w == child->pid) {
                    /* Drain remaining bytes */
                    fcntl(child->pipe_fd, F_SETFL, flags & ~O_NONBLOCK);
                    while ((n = read(child->pipe_fd, read_buf,
                                     sizeof read_buf - 1)) > 0) {
                        read_buf[n] = '\0';
                        /* Process line by line */
                        for (ssize_t i = 0; i < n; i++) {
                            char c = read_buf[i];
                            if (line_used < sizeof line_buf - 1) {
                                line_buf[line_used++] = c;
                            }
                            if (c == '\n' || line_used == sizeof line_buf - 1) {
                                line_buf[line_used] = '\0';
                                log_append(ctx->log, line_buf);
                                /* Check for ##setvariable */
                                if (strncmp(line_buf, "##setvariable ", 14) == 0) {
                                    char *eq = strchr(line_buf + 14, '=');
                                    if (eq != NULL) {
                                        *eq = '\0';
                                        var_map_set(&ctx->vars,
                                                    line_buf + 14, eq + 1);
                                    }
                                }
                                line_used = 0;
                            }
                        }
                    }
                    if (line_used > 0) {
                        line_buf[line_used] = '\0';
                        log_append(ctx->log, line_buf);
                        line_used = 0;
                    }
                    close(child->pipe_fd);
                    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                }
                
                struct timespec ts = {0, 10000000L}; 
                nanosleep(&ts, NULL);
                continue;
            }
            break; 
        }
        if (n == 0) {
           
            break;
        }

        read_buf[n] = '\0';
        for (ssize_t i = 0; i < n; i++) {
            char c = read_buf[i];
            if (line_used < sizeof line_buf - 1) {
                line_buf[line_used++] = c;
            }
            if (c == '\n' || line_used == sizeof line_buf - 1) {
                line_buf[line_used] = '\0';
                log_append(ctx->log, line_buf);
               
                if (strncmp(line_buf, "##setvariable ", 14) == 0) {
                    char *eq = strchr(line_buf + 14, '=');
                    if (eq != NULL) {
                        *eq = '\0';
                        
                        char *key = line_buf + 14;
                        char *val = eq + 1;
                        
                        size_t vl = strlen(val);
                        while (vl > 0 && (val[vl - 1] == '\n' ||
                                          val[vl - 1] == '\r')) {
                            val[--vl] = '\0';
                        }
                        var_map_set(&ctx->vars, key, val);
                    }
                }
                line_used = 0;
            }
        }
    }

   
    if (line_used > 0) {
        line_buf[line_used] = '\0';
        log_append(ctx->log, line_buf);
    }

    close(child->pipe_fd);

    int status;
    waitpid(child->pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int run_step(const step_def_t *step, job_context_t *ctx,
                    const artifact_store_t *arts)
{
    switch (step->kind) {

    case STEP_SCRIPT: {
        char *cmd = expand_vars(step->script, &ctx->vars);
        if (cmd == NULL) return -1;

        char msg[PATH_MAX + 32];
        snprintf(msg, sizeof msg, "##[command] %s", cmd);
        log_append(ctx->log, msg);

        child_t child;
        int rc = spawn_script(cmd, ctx->workspace, &child);
        free(cmd);
        if (rc < 0) {
            log_append(ctx->log, "##[error] Failed to spawn child process.");
            return -1;
        }
        return drain_and_wait(&child, ctx, ctx->timeout_seconds);
    }

    case STEP_TASK: {
        
        const char *task = step->task;
        int rc;
        if (strncmp(task, "Sleep@", 6) == 0) {
            rc = task_sleep(step->inputs, step->n_inputs, ctx);
        } else if (strncmp(task, "Cpu@", 4) == 0) {
            rc = task_cpu(step->inputs, step->n_inputs, ctx);
        } else if (strncmp(task, "WordCount@", 10) == 0) {
            rc = task_wordcount(step->inputs, step->n_inputs, ctx);
        } else if (strncmp(task, "Histogram@", 10) == 0) {
            rc = task_histogram(step->inputs, step->n_inputs, ctx);
        } else {
            char msg[128];
            snprintf(msg, sizeof msg, "##[error] Unknown task: %s", task);
            log_append(ctx->log, msg);
            rc = -1;
        }
        return rc;
    }

    case STEP_PUBLISH: {
        char *src = expand_vars(step->path, &ctx->vars);
        if (src == NULL) return -1;
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, "##[publish] artifact '%s' from '%s'",
                 step->artifact, src);
        log_append(ctx->log, msg);
        int rc = artifact_publish(arts, step->artifact, src, ctx->workspace);
        free(src);
        if (rc < 0) {
            log_append(ctx->log, "##[error] publish failed.");
        }
        return rc;
    }

    case STEP_DOWNLOAD: {
        char *dest = expand_vars(step->path, &ctx->vars);
        if (dest == NULL) return -1;
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, "##[download] artifact '%s' to '%s'",
                 step->artifact, dest);
        log_append(ctx->log, msg);
        int rc = artifact_download(arts, step->artifact, dest, ctx->workspace);
        free(dest);
        if (rc < 0) {
            log_append(ctx->log, "##[error] download failed.");
        }
        return rc;
    }

    default:
        log_append(ctx->log, "##[error] Unknown step kind.");
        return -1;
    }
}


static void inherit_vars(job_context_t *ctx, const run_t *run, size_t job_idx)
{
    const pipeline_t *p = run->pipeline;

   
    for (size_t i = 0; i < p->n_variables; i++) {
        var_map_set(&ctx->vars, p->variables[i].key, p->variables[i].value);
    }


    if (run->n_jobs > 1024) return; /* safety guard */
    bool is_ancestor[1024] = {false};

    
    const job_rt_t *this_job = &run->jobs[job_idx];
    for (size_t i = 0; i < this_job->n_deps; i++) {
        is_ancestor[this_job->deps[i]] = true;
    }

    
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t g = 0; g < run->n_jobs; g++) {
            if (!is_ancestor[g]) continue;
            for (size_t i = 0; i < run->jobs[g].n_deps; i++) {
                if (!is_ancestor[run->jobs[g].deps[i]]) {
                    is_ancestor[run->jobs[g].deps[i]] = true;
                    changed = true;
                }
            }
        }
    }

    
    for (size_t g = 0; g < run->n_jobs; g++) {
        if (!is_ancestor[g]) continue;
       
        const var_map_t *exported = &run->jobs[g].vars;
        for (size_t i = 0; i < exported->count; i++) {
            var_map_set(&ctx->vars, exported->entries[i].key, exported->entries[i].value);
        }
    }
}


status_t execute_job(run_t *run, size_t job_idx, void *work_directory)
{
    const char    *workdir = (const char *)work_directory;
    const job_rt_t *job_rt = &run->jobs[job_idx];
    const job_def_t *job_def = job_rt->def;

    
    job_context_t ctx = {0};
    ctx.run_id         = run->id;
    ctx.job_index      = job_idx;
    ctx.cancel_flag    = &run->cancel_requested;
    ctx.timeout_seconds = job_def->timeout_seconds;

    
    job_log_t *log = &run->jobs[job_idx].log;
    ctx.log = log;

    
    ctx.workspace = make_workspace(workdir, run->id, job_idx);
    if (ctx.workspace == NULL) {
        return ST_FAILED;
    }

    
    ctx.vars.entries  = NULL;
    ctx.vars.count    = 0;
    ctx.vars.capacity = 0;
    inherit_vars(&ctx, run, job_idx);

    
    var_map_t inherited = {0};
    for (size_t i = 0; i < ctx.vars.count; i++) {
        var_map_set(&inherited, ctx.vars.entries[i].key, ctx.vars.entries[i].value);
    }

    
    artifact_store_t arts;
    if (artifact_store_init(&arts, workdir, run->id) < 0) {
        var_map_destroy(&inherited);
        var_map_destroy(&ctx.vars);
        free(ctx.workspace);
        return ST_FAILED;
    }

   
    bool any_failed    = false;
    bool partial       = false;

    for (size_t s = 0; s < job_def->n_steps; s++) {
        
        if (atomic_load(&run->cancel_requested)) {
            any_failed = true;
            break;
        }

        const step_def_t *step = &job_def->steps[s];

        /* Record step start time */
        clock_gettime(CLOCK_REALTIME, &job_rt->steps[s].start);
        job_set_status(run, job_idx, ST_RUNNING); 
        int rc = run_step(step, &ctx, &arts);

        clock_gettime(CLOCK_REALTIME, &job_rt->steps[s].end);

        if (rc != 0) {
            if (step->continue_on_error) {
                partial = true;
                
            } else {
                any_failed = true;
                break;
            }
        }
    }

    status_t final;
    if (atomic_load(&run->cancel_requested)) {
        final = ST_CANCELED;
    } else if (any_failed) {
        final = ST_FAILED;
    } else if (partial) {
        final = ST_PARTIALLY_SUCCEEDED;
    } else {
        final = ST_SUCCEEDED;
    }


    var_map_t *exports = &run->jobs[job_idx].vars;
    for (size_t i = 0; i < ctx.vars.count; i++) {
        const char *key = ctx.vars.entries[i].key;
        const char *value = ctx.vars.entries[i].value;
        const char *before = var_map_get(&inherited, key);
        if (before == NULL || strcmp(before, value) != 0) {
            var_map_set(exports, key, value);
        }
    }
    var_map_destroy(&inherited);

    /* ---- Cleanup ---- */
    log_close(log);
    artifact_store_destroy(&arts);
    var_map_destroy(&ctx.vars);
    free(ctx.workspace);

    return final;
}