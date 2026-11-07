# Benchmarks (metrics)

The metrics battery measures speedup, efficiency and the fit of Amdahl with
the pipelines of the project. It runs everything in local; the same command is
meant to run on the second environment to compare environments.

## Components

- `bin/gen_corpus` (compiles with `make`): generates the deterministic corpus
  used by the data tasks. Same arguments, same file on any machine.
- `wide.yaml`: 16 independent jobs of `Cpu@1` (~0.5 s each).
- `deep.yaml`: chain of 16 jobs with `dependsOn` (~0.1 s each): the critical
  path is the whole pipeline.
- `data_wc.yaml` / `data_hist.yaml`: `WordCount@1` and `Histogram@1` on the
  corpus; `run.sh` replaces `@CORPUS@` and `@THREADS@` for each run.
- `run.sh`: the battery: varies agents and threads, repeats each point 5
  times and leaves the raw runs in a CSV in `bench/results/`.
- `analyze.py`: takes the median of every point and calculates S(p), E(p),
  estimates the parallelizable fraction *f* and compares with Amdahl.

## Running

```sh
make                                   # pipelined, pipectl and gen_corpus
bash bench/run.sh                      # all the scenarios
bash bench/run.sh wide                 # just one scenario (wide|deep|data|combined)
python3 bench/analyze.py all           # tables with the analysis
python3 bench/analyze.py bench/results/some_env.csv
python3 bench/plot.py                  # graphs in bench/results/plots/
```

Adjustable with environment variables: `BENCH_SIZE` (corpus, default 512M),
`BENCH_REPS` (5), `BENCH_AGENTS`, `BENCH_THREADS` and `BENCH_TIMEOUT`.

The CSV header has date, host, kernel, distribution, CPU, cores, RAM and GCC
version: those lines make the environment file-comparable with the runs of the
other machine.

Note: with 512M of corpus and 5 repetitions the complete battery takes about
5 minutes on 4-core machines.
