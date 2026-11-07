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

The `examples/` folder has pipelines ready to submit:

- `example.yaml`: two stages with artifacts, WordCount and Histogram.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/example.yaml`
- `invalid.yaml`: invalid on purpose; the server rejects it and returns every error.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/invalid.yaml`
- `follow.yaml`: prints one line per second for 15 seconds, for following the logs and for canceling.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/follow.yaml`
    - `./bin/pipectl 127.0.0.1 9000 LOGS <id> FOLLOW`
    - `./bin/pipectl 127.0.0.1 9000 CANCEL <id>`
- `wide.yaml`: six independent jobs of 2 seconds. Submit it several times at once to see more jobs than agents.
    - `./bin/pipectl 127.0.0.1 9000 SUBMIT examples/wide.yaml & ./bin/pipectl 127.0.0.1 9000 SUBMIT examples/wide.yaml`
    - `./bin/pipectl 127.0.0.1 9000 LIST`

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
