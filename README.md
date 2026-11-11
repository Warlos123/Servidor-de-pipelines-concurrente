# Concurrent pipeline server

`pipelined` receives pipelines written in YAML, validates them and runs them concurrently on a limited number of agents. `pipectl` is the command-line client.

## Setup

Install the required dependencies (gcc with OpenMP, make and libyaml):

- `sudo apt install build-essential libyaml-dev`

Building the project:

- Run `make`
- The executables are created in `bin/`: `pipelined`, `pipectl` and the test programs.
- `make clean` removes `build/` and `bin/`.

## Run the server

- Run the server using the command: `./bin/pipelined 9000 4 /tmp/pipelines`
- Args:
    - The first argument is the port. 
    - The second argument is the number of agents: how many jobs can run at the same time across all the pipelines.
    - The third argument is the work directory.
    - Optional: `--omp-threads <max>` caps the OpenMP threads each data task (WordCount, Histogram) may use, no matter what its `threads` input asks for. With several agents running data tasks at once, agents x task threads can exceed the CPU cores; set the cap so that agents x omp-threads stays close to the number of cores. Without the flag the tasks use the thread count requested in the YAML.
- Stop it with Ctrl-C or with the `SHUTDOWN` command.

## Run the client

- Run the client using the command: `./bin/pipectl 127.0.0.1 9000 <command>`
- Args:
    - The first argument is the server IP address
    - The second argument is the server port number where the server is listening.
    - The rest is the command. Commands are written in uppercase.
- Commands:
    - `SUBMIT <file.yaml>`: sends a pipeline. If it is valid, prints the run id; if not, prints every error with its line or key path.
    - `STATUS <id>`: state and duration of the run and of each stage, job and step.
    - `LOGS <id> [job] [FOLLOW]`: log of the run or of one job. With `FOLLOW`, keeps printing the new lines until the run finishes.
    - `CANCEL <id>`: cancels the run; running steps are stopped and pending ones don't run.
    - `LIST`: runs known by the server with their state.
    - `SHUTDOWN`: orderly shutdown of the server.
- The client exits with 0 when the server answers `OK` and with 1 when it answers `ERR` or can't connect.

## Example pipelines

The `examples/` folder has pipelines that do real work (or prove engine semantics):

- `gutenberg.yaml`: downloads a real book from Project Gutenberg, analyzes it with
  WordCount/Histogram tasks plus a stats script in three parallel jobs, and consolidates
  a report. Also demonstrates `variables`, `$(VAR)` substitution and `timeoutInSeconds`.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/gutenberg.yaml`
- `ci_cd.yaml`: a full CI/CD flow with a real git server: a bare repo acts as the
  remote (`origin.git`), jobs clone from it, push two feature branches in parallel,
  fetch + merge with `--no-ff`, gcc build, functional tests (exit 1 on failure),
  and a tagged release. The remote travels between stages as an artifact because
  jobs don't share a filesystem; every push/fetch is a real `git push`/`git fetch`.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/ci_cd.yaml`
- `fan_out_fan_in.yaml`: generates a corpus and fans it out to three parallel jobs
  (WordCount, Histogram, CPU hash), then fans in to a final report.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/fan_out_fan_in.yaml`
- `etl.yaml`: a full ETL on live data from a public API (Open-Meteo, no key): three
  parallel extract jobs (venv + requests), three parallel transform jobs (dedup,
  date conversion, null filtering), a data-quality gate, `condition: failed()`
  quarantine and `condition: always()` cleanup, SQLite load with SQL aggregates,
  and matplotlib PNG charts + self-contained HTML dashboard. Set `FORZAR_FALLO: "1"`
  in the variables to watch the quarantine path abort the pipeline.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/etl.yaml`
- `invalid.yaml`: invalid on purpose; the server rejects it and returns every error.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/invalid.yaml`
- Sample artifacts produced by real runs of these pipelines are kept in `artifacts/`
  (textual evidence only; see `artifacts/README.md`).

## Run the tests

- Run `make test`
    - Runs the unit tests, the concurrency and determinism tests, and the client-server integration tests (`tests/test_integration.sh`).
    - Each program prints how many checks passed; `make test` stops at the first one that fails.
- Run `make test-race`
    - Builds the determinism test with `-DSIN_PROTECCION` (the shared data is no longer protected) to show the race condition. It is expected to fail.

## Metrics

- `bench/` has the measuring pipelines (wide, deep, data tasks and combined) and the scripts that generate them and run them:
    - `bash bench/run.sh` runs the whole battery (agents and threads sweeps, 5 runs per point) and leaves a CSV in `bench/results/`.
    - `python3 bench/analyze.py all` prints the medians, the speedup S(p), the efficiency E(p) and the fit to the law of Amdahl.
- See `bench/README.md` for details.

## Kabré Cluster (CeNAT)

- For instructions and scripts to sync, build, test, and benchmark on the Kabré supercomputer using SLURM, see `scripts/kabre/README.md`.
