#!/usr/bin/env bash
#
# runner: bench/run.sh [all|wide|deep|data|combined]
#
# Metrics battery of the project. Measures:
#   wide     . 16 independent Cpu@1 jobs, varying the agents (1 2 4 8)
#   deep     . chain of 16 jobs with dependsOn, varying the agents (1 2 4 8)
#   data     . WordCount@1 and Histogram@1 on the generated corpus,
#              varying the OpenMP threads (1 2 4 8)
#   combined . three wide pipelines competing for the same agents,
#              varying the agents (1 2 4 8)
#
# Every point repeats 5 times and the analysis (bench/analyze.py) takes
# the median of each one. The raw runs are saved in a CSV in bench/results/
# with a header of comments with the hardware and software of the environment.
#
# Environment variables:
#   BENCH_SIZE       corpus size (default: 512M)
#   BENCH_REPS       repetitions per point (default: 5)
#   BENCH_AGENTS     agent list wide/deep/combined (default: "1 2 4 8")
#   BENCH_THREADS    thread list for the data scenario (default: "1 2 4 8")
#   BENCH_TIMEOUT    maximum seconds per measured run (default: 900)
set -u

cd "$(dirname "$0")/.." || exit 1

ROOT=$(pwd)
SERVER=$ROOT/bin/pipelined
CLIENT=$ROOT/bin/pipectl
REPS=${BENCH_REPS:-5}
RUN_TIMEOUT=${BENCH_TIMEOUT:-900}
AGENTS_LIST=${BENCH_AGENTS:-"1 2 4 8"}
THREADS_LIST=${BENCH_THREADS:-"1 2 4 8"}

DATA_DIR=$ROOT/bench/data
RESULTS_DIR=$ROOT/bench/results
CORPUS_SIZE=${BENCH_SIZE:-512M}
CORPUS=$DATA_DIR/corpus_${CORPUS_SIZE}.txt

mkdir -p "$DATA_DIR" "$RESULTS_DIR"

CSV=$RESULTS_DIR/$(uname -n)_$(date +%Y%m%d_%H%M%S).csv
PORT=$((20000 + RANDOM % 20000))
SERVER_PID=""
WORKDIR=""

log() { echo "[bench] $*"; }

# ---------- environment header ----------

cpu_model=$(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | sed 's/^ *//')
cpu_count=$(nproc 2>/dev/null || echo '?')
mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
gcc_ver=$(gcc -dumpversion 2>/dev/null)
kernel=$(uname -srm)
distro=$(. /etc/os-release 2>/dev/null; echo "$PRETTY_NAME")

{
    echo "# course CI-0117 project benchmark"
    echo "# date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "# host: $(uname -n) | kernel: $kernel | distro: $distro"
    echo "# cpu: $cpu_model | cores: $cpu_count | mem_kb: $mem_kb"
    echo "# gcc: $gcc_ver"
    echo "scenario,param,run,seconds"
} > "$CSV"

# ---------- helpers ----------

start_server() {
    local agents=$1
    shift
    WORKDIR=$(mktemp -d /tmp/pipelined_bench_XXXXXX)
    "$SERVER" "$PORT" "$agents" "$WORKDIR" "$@" > "$WORKDIR/server.log" 2>&1 &
    SERVER_PID=$!

    local i
    for i in $(seq 1 100); do
        if "$CLIENT" 127.0.0.1 "$PORT" LIST > /dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
    done

    log "the server didn't start"
    cat "$WORKDIR/server.log"
    exit 1
}

stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        "$CLIENT" 127.0.0.1 "$PORT" SHUTDOWN > /dev/null 2>&1
        for i in $(seq 1 100); do
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -9 "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    SERVER_PID=""
    [ -n "$WORKDIR" ] && rm -rf "$WORKDIR"
    WORKDIR=""
}

trap stop_server EXIT

# wait_run <id> <deadline>: waits for the run to finish; on success prints
# the elapsed wall-clock seconds since the submission.
wait_run() {
    local id=$1
    local deadline=$2
    local start=$3
    local status elapsed

    while :; do
        status=$("$CLIENT" 127.0.0.1 "$PORT" STATUS "$id" 2>/dev/null | head -1 | awk '{print $4}')
        case "$status" in
            queued|running|"")
                if [ "$(date +%s)" -ge "$deadline" ]; then
                    log "TIMEOUT: run $id did not finish in $RUN_TIMEOUT s (status: $status)"
                    return 1
                fi
                sleep 0.05
                ;;
            *) break ;;
        esac
    done

    if [ "$status" != "succeeded" ]; then
        log "the run $id ended in '$status'"
        return 1
    fi

    echo "$(awk -v e="$(date +%s.%N)" -v s="$start" 'BEGIN {print e - s}')"
    return 0
}

# record <scenario> <param> <run> <seconds>: adds a row to the CSV.
record() {
    echo "$1,$2,$3,$4" >> "$CSV"
}

# submit_run <yaml> [<deadline> <start>]: submits and waits; returns 1 on failure.
submit_run() {
    local yaml=$1
    local id out

    out=$("$CLIENT" 127.0.0.1 "$PORT" SUBMIT "$yaml" 2>&1)
    if [ $? -ne 0 ]; then
        log "SUBMIT rejected the pipeline: $out"
        return 1
    fi
    echo "$out"
}

# ---------- corpus ----------

make_corpus() {
    # gen_corpus is deterministic: regenerating in every invocation is cheap
    # and makes sure the file always matches the reported size.
    log "generating the corpus: $CORPUS ($CORPUS_SIZE) with bin/gen_corpus"
    ./bin/gen_corpus "$CORPUS.tmp" "$CORPUS_SIZE" || exit 1
    mv "$CORPUS.tmp" "$CORPUS"
}

# ---------- scenario: wide ----------

bench_wide() {
    local yaml=$ROOT/bench/wide.yaml
    local agents rep out

    for agents in $AGENTS_LIST; do
        start_server "$agents"
        for rep in $(seq 1 "$REPS"); do
            local start=$(date +%s.%N)
            local id
            id=$(submit_run "$yaml") || { stop_server; return 1; }
            out=$(wait_run "$id" "$(( $(date +%s) + RUN_TIMEOUT ))" "$start") || { stop_server; return 1; }
            record wide "$agents" "$rep" "$out"
            log "wide agents=$agents rep=$rep: ${out}s"
        done
        stop_server
    done
}

# ---------- scenario: deep ----------

bench_deep() {
    local yaml=$ROOT/bench/deep.yaml
    local agents rep out

    for agents in $AGENTS_LIST; do
        start_server "$agents"
        for rep in $(seq 1 "$REPS"); do
            local start=$(date +%s.%N)
            local id
            id=$(submit_run "$yaml") || { stop_server; return 1; }
            out=$(wait_run "$id" "$(( $(date +%s) + RUN_TIMEOUT ))" "$start") || { stop_server; return 1; }
            record deep "$agents" "$rep" "$out"
            log "deep agents=$agents rep=$rep: ${out}s"
        done
        stop_server
    done
}

# ---------- scenario: data ----------

bench_data() {
    local threads rep out
    local tmp

    for threads in $THREADS_LIST; do
        for task in wc hist; do
            local tmpl=$ROOT/bench/data_${task}.yaml
            tmp=$(mktemp "$RESULTS_DIR/data_${task}_${threads}.XXXXXX.yaml")
            sed -e "s|@CORPUS@|$CORPUS|g" -e "s|@THREADS@|$threads|g" "$tmpl" > "$tmp"

            start_server 1
            for rep in $(seq 1 "$REPS"); do
                local start=$(date +%s.%N)
                local id
                id=$(submit_run "$tmp") || { rm -f "$tmp"; stop_server; return 1; }
                out=$(wait_run "$id" "$(( $(date +%s) + RUN_TIMEOUT ))" "$start") || { rm -f "$tmp"; stop_server; return 1; }
                record "data_${task}" "$threads" "$rep" "$out"
                log "data_${task} threads=$threads rep=$rep: ${out}s"
            done
            stop_server
            rm -f "$tmp"
        done
    done
}

# ---------- scenario: combined ----------

bench_combined() {
    # Three wide pipelines (48 jobs) competing for the same agents.
    local agents rep
    local yaml=$ROOT/bench/wide.yaml
    local ids pids elapsed

    for agents in $AGENTS_LIST; do
        start_server "$agents"
        for rep in $(seq 1 "$REPS"); do
            local start=$(date +%s.%N)
            local deadline=$(( $(date +%s) + RUN_TIMEOUT ))
            ids=()
            for i in 1 2 3; do
                ids+=("$(submit_run "$yaml")") || { stop_server; return 1; }
            done
            # ---------- wait for the three of them ----------
            for id in "${ids[@]}"; do
                wait_run "$id" "$deadline" "$start" > /dev/null || { stop_server; return 1; }
            done
            elapsed=$(awk -v e="$(date +%s.%N)" -v s="$start" 'BEGIN {print e - s}')
            record combined "$agents" "$rep" "$elapsed"
            log "combined agents=$agents rep=$rep: ${elapsed}s"
        done
        stop_server
    done
}

# ---------- main ----------

[ -x "$SERVER" ] || { echo "missing $SERVER: run make first"; exit 1; }
[ -x "$CLIENT" ] || { echo "missing $CLIENT: run make first"; exit 1; }

WHAT=${1:-all}

case "$WHAT" in
    all|wide|deep|data|combined) ;;
    *) echo "unknown scenario '$WHAT' (all, wide, deep, data, combined)"; exit 1 ;;
esac

[ "$WHAT" = "all" ] || [ "$WHAT" = "data" ] || [ "$WHAT" = "combined" ] && make_corpus

log "results in $CSV"

[ "$WHAT" = "all" ] || [ "$WHAT" = "wide" ]     && bench_wide
[ "$WHAT" = "all" ] || [ "$WHAT" = "deep" ]     && bench_deep
[ "$WHAT" = "all" ] || [ "$WHAT" = "data" ]     && bench_data
[ "$WHAT" = "all" ] || [ "$WHAT" = "combined" ] && bench_combined

log "finished: $CSV"
