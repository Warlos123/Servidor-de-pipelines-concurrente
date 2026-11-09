#!/usr/bin/env bash
# Client-server integration tests: starts pipelined and drives it with pipectl.
# Run from the repository root (make test does it): bash tests/test_integration.sh

SERVER=./bin/pipelined
CLIENT=./bin/pipectl
HOST=127.0.0.1
AGENTS=2

CHECKS=0
FAILURES=0

# Temporary directory for the YAML files, the server's work directory and its output.
TMP=$(mktemp -d)
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -INT "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

# check <description> <command...>: runs the command and counts it as passed if it succeeds.
check() {
    local description=$1
    shift
    CHECKS=$((CHECKS + 1))

    if "$@"; then
        return 0
    fi

    FAILURES=$((FAILURES + 1))
    echo "  FAIL $description"
}

# Compare float: compare_floats val1 op val2
compare_floats() {
    awk -v v1="$1" -v op="$2" -v v2="$3" 'BEGIN {
        if (op == ">=") exit !(v1 + 0 >= v2 + 0)
        if (op == "<=") exit !(v1 + 0 <= v2 + 0)
        exit 1
    }'
}

# Calculate elapsed: calc_elapsed end start
calc_elapsed() {
    awk -v end="$1" -v start="$2" 'BEGIN {print end - start}'
}

# client <args...>: runs pipectl against the test server; output goes to $TMP/out.
client() {
    "$CLIENT" "$HOST" "$PORT" "$@" > "$TMP/out" 2>&1
}

# Status of a run, the 4th word of the first STATUS line: run <id> "<name>" <status> <time>
run_status() {
    "$CLIENT" "$HOST" "$PORT" STATUS "$1" 2>/dev/null | head -1 | awk '{print $4}'
}

# wait_finished <id> <seconds>: waits until the run is no longer queued or running.
wait_finished() {
    local tries=$(($2 * 10))
    local i

    for ((i = 0; i < tries; i++)); do
        case $(run_status "$1") in
            queued|running|"") sleep 0.1 ;;
            *) return 0 ;;
        esac
    done

    return 1
}

# wait_status <id> <status> <seconds>: waits until the run status matches the target status.
wait_status() {
    local id=$1
    local target=$2
    local timeout=$3
    local tries=$(($timeout * 10))
    for ((i = 0; i < tries; i++)); do
        if [ "$(run_status "$id")" = "$target" ]; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

out_has() { grep -qF -- "$1" "$TMP/out"; }
out_lines() { wc -l < "$TMP/out"; }

# no_orphan <pattern>: passes when no process matches the pattern.
no_orphan() {
    if pgrep -f "$1" > /dev/null; then
        return 1
    fi
    return 0
}



# A free-looking port, so the tests don't clash with a server already running.
PORT=$((20000 + RANDOM % 20000))
mkdir -p "$TMP/work"

"$SERVER" "$PORT" "$AGENTS" "$TMP/work" > "$TMP/server.log" 2>&1 &
SERVER_PID=$!

for ((i = 0; i < 50; i++)); do
    if "$CLIENT" "$HOST" "$PORT" LIST > /dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "integration: the server didn't start"
    cat "$TMP/server.log"
    exit 1
fi

cp tests/cancel_task.yaml "$TMP/cancel_task.yaml"
cp tests/publish_fail.yaml "$TMP/publish_fail.yaml"






cat > "$TMP/invalid.yaml" <<'YAML'
trigger: main
stages:
- stage: build
  jobs:
  - job: a
    dependsOn: b
    steps: [{script: echo a}]
  - job: b
    dependsOn: a
    steps: [{script: echo b}]
- stage: build
  dependsOn: tset
  jobs: [{job: c, steps: [{script: echo c}]}]
YAML

cat > "$TMP/valid.yaml" <<'YAML'
name: integration
variables:
  WHO: world
stages:
- stage: build
  jobs:
  - job: hello
    steps:
    - script: echo "hello $(WHO)"
- stage: test
  jobs:
  - job: check
    steps:
    - script: echo "second stage"
YAML

cat > "$TMP/slow.yaml" <<'YAML'
stages:
- stage: s
  jobs:
  - job: forever
    steps:
    - script: sleep 30
YAML

cat > "$TMP/agents.yaml" <<'YAML'
stages:
- stage: wide
  jobs:
  - {job: a, steps: [{task: Sleep@1, inputs: {seconds: 1}}]}
  - {job: b, steps: [{task: Sleep@1, inputs: {seconds: 1}}]}
  - {job: c, steps: [{task: Sleep@1, inputs: {seconds: 1}}]}
  - {job: d, steps: [{task: Sleep@1, inputs: {seconds: 1}}]}
YAML



echo "malformed requests"

client FOO
check "unknown command exits with 1" [ $? -eq 1 ]
check "unknown command reason" out_has "unknown command"

client STATUS abc
check "invalid id exits with 1" [ $? -eq 1 ]
check "invalid id reason" out_has "invalid id"

client STATUS 999
check "unknown run reason" out_has "run 999 not found"



echo "invalid pipeline"

client SUBMIT "$TMP/invalid.yaml"
check "invalid pipeline exits with 1" [ $? -eq 1 ]
check "unknown key reported" out_has "unknown key 'trigger'"
check "duplicate stage reported" out_has "duplicate stage name 'build'"
check "cycle reported" out_has "dependency cycle"
check "unknown stage reported" out_has "unknown stage 'tset'"
check "all 4 errors in one answer" [ "$(out_lines)" -eq 4 ]






echo "valid pipeline"

client SUBMIT "$TMP/valid.yaml"
check "valid pipeline accepted" [ $? -eq 0 ]
VALID_ID=$(cat "$TMP/out")

check "valid run finishes" wait_finished "$VALID_ID" 10
check "valid run succeeded" [ "$(run_status "$VALID_ID")" = "succeeded" ]

client LOGS "$VALID_ID"
check "logs have the script output" out_has "hello world"
check "logs have the second stage" out_has "second stage"
check "logs have a header per job" out_has "== build/hello =="

client LOGS "$VALID_ID" check
check "logs of one job" out_has "second stage"
check "logs of one job only" [ "$(grep -c "hello world" "$TMP/out")" -eq 0 ]

client LIST
check "list shows the run" out_has "integration succeeded"



echo "logs --follow"

client SUBMIT "$TMP/valid.yaml"
FOLLOW_ID=$(cat "$TMP/out")

timeout 10 "$CLIENT" "$HOST" "$PORT" LOGS "$FOLLOW_ID" FOLLOW > "$TMP/out" 2>&1
check "follow ends when the run ends" [ $? -eq 0 ]
check "follow received the lines" out_has "second stage"



echo "cancel"

client SUBMIT "$TMP/slow.yaml"
SLOW_ID=$(cat "$TMP/out")
sleep 0.5

client CANCEL "$SLOW_ID"
check "cancel accepted" out_has "canceling"
check "canceled run finishes quickly" wait_finished "$SLOW_ID" 5
check "run is canceled" [ "$(run_status "$SLOW_ID")" = "canceled" ]





echo "agent limit"

# 4 jobs of 1 second with 2 agents: at most 2 at a time, so about 2 seconds, never about 1.
START=$(date +%s.%N)
client SUBMIT "$TMP/agents.yaml"
AGENTS_ID=$(cat "$TMP/out")
check "wide run finishes" wait_finished "$AGENTS_ID" 10
END=$(date +%s.%N)

ELAPSED=$(calc_elapsed "$END" "$START")
check "never more jobs than agents (took ${ELAPSED}s)" compare_floats "$ELAPSED" ">=" "1.9"



# ---------- job timeout ----------

echo "job timeout"

cat > "$TMP/timeout_works.yaml" <<'YAML'
name: timeout_works
stages:
- stage: s
  jobs:
  - job: naps
    timeoutInSeconds: 2
    steps:
    - script: sleep 1.5
YAML

cat > "$TMP/timeout_sleep_task.yaml" <<'YAML'
name: timeout_sleep_task
stages:
- stage: s
  jobs:
  - job: sleeper
    timeoutInSeconds: 2
    steps:
    - task: Sleep@1
      inputs: {seconds: 10}
YAML

cat > "$TMP/timeout_total.yaml" <<'YAML'
name: timeout_total
stages:
- stage: s
  jobs:
  - job: three_naps
    timeoutInSeconds: 2
    steps:
    - script: sleep 0.8
    - script: sleep 0.8
    - script: sleep 0.8
YAML

client SUBMIT "$TMP/timeout_works.yaml"
check "under the limit accepted" [ $? -eq 0 ]
WORKS_ID=$(cat "$TMP/out")
check "under the limit finishes" wait_finished "$WORKS_ID" 10
check "under the limit succeeded" [ "$(run_status "$WORKS_ID")" = "succeeded" ]

client SUBMIT "$TMP/timeout_sleep_task.yaml"
check "sleep task accepted" [ $? -eq 0 ]
TASK_ID=$(cat "$TMP/out")
START=$(date +%s.%N)
check "sleep task finishes" wait_finished "$TASK_ID" 10
END=$(date +%s.%N)
ELAPSED=$(calc_elapsed "$END" "$START")
check "sleep task timeout stops it early (${ELAPSED}s)" compare_floats "$ELAPSED" "<=" "5"
check "sleep task run failed" [ "$(run_status "$TASK_ID")" = "failed" ]
client LOGS "$TASK_ID" sleeper
check "sleep task logged the timeout" out_has "##[error] Timeout exceeded."

client SUBMIT "$TMP/timeout_total.yaml"
check "total timeout accepted" [ $? -eq 0 ]
TOTAL_ID=$(cat "$TMP/out")
check "total timeout finishes" wait_finished "$TOTAL_ID" 10
check "total timeout run failed" [ "$(run_status "$TOTAL_ID")" = "failed" ]
client LOGS "$TOTAL_ID"
check "total timeout logged" out_has "##[error] Timeout exceeded."



# ---------- background children die with the step ----------

echo "background children"

cat > "$TMP/background.yaml" <<'YAML'
name: background
stages:
- stage: s
  jobs:
  - job: bg
    steps:
    - script: sleep 5.4321 & echo "spawned"
YAML

START=$(date +%s.%N)
client SUBMIT "$TMP/background.yaml"
check "background accepted" [ $? -eq 0 ]
BG_ID=$(cat "$TMP/out")
check "background finishes quickly" wait_finished "$BG_ID" 5
END=$(date +%s.%N)
ELAPSED=$(calc_elapsed "$END" "$START")
check "background step doesn't wait for its child (${ELAPSED}s)" compare_floats "$ELAPSED" "<=" "3"
check "no orphan child survives" no_orphan "sleep 5.4321"



# ---------- artifacts with spaces and apostrophes ----------

echo "artifact names with spaces and quotes"

cat > "$TMP/spaces.yaml" <<'YAML'
name: spaces
stages:
- stage: s
  jobs:
  - job: j
    steps:
    - script: mkdir -p "my dir/weird 'name" && echo "contenido secreto" > "my dir/weird 'name/text file 'x'.txt"
    - publish: my dir/weird 'name
      artifact: artifact 'con' espacios
    - download: artifact 'con' espacios
      path: dest 'in' spaces
    - script: cat "dest 'in' spaces/weird 'name/text file 'x'.txt" | grep "contenido secreto" && echo "SPACES_OK"
YAML

client SUBMIT "$TMP/spaces.yaml"
check "spaces accepted" [ $? -eq 0 ]
SPACES_ID=$(cat "$TMP/out")
check "spaces finishes" wait_finished "$SPACES_ID" 10
check "spaces succeeds" [ "$(run_status "$SPACES_ID")" = "succeeded" ]
client LOGS "$SPACES_ID"
check "spaces downloaded correctly" out_has "SPACES_OK"



# ---------- unknown task ----------

echo "unknown task"

cat > "$TMP/unknown_task.yaml" <<'YAML'
name: unknown_task
stages:
- stage: s
  jobs:
  - job: j
    steps:
    - task: Nope@9
      inputs: {seconds: 1}
YAML

client SUBMIT "$TMP/unknown_task.yaml"
check "unknown task rejected" [ $? -eq 1 ]
check "unknown task reported" out_has "unknown task 'Nope@9'"



# ---------- workspace isolation ----------

echo "workspace isolation"

cat > "$TMP/isolation.yaml" <<'YAML'
name: isolation
stages:
- stage: leave
  jobs:
  - job: writer
    steps:
    - script: echo "archivo privado" > left.txt
- stage: check
  jobs:
  - job: checker
    steps:
    - script: "test ! -f left.txt && echo ISOLATED_OK"
YAML

client SUBMIT "$TMP/isolation.yaml"
check "isolation accepted" [ $? -eq 0 ]
ISO_ID=$(cat "$TMP/out")
check "isolation finishes" wait_finished "$ISO_ID" 10
check "isolation succeeds" [ "$(run_status "$ISO_ID")" = "succeeded" ]
client LOGS "$ISO_ID"
check "workspaces don't share files" out_has "ISOLATED_OK"



# ---------- wordcount with 1 and N threads ----------

echo "wordcount threads determinism"

cat > "$TMP/wc_threads.yaml" <<'YAML'
name: wc_threads
stages:
- stage: prep
  jobs:
  - job: corpus
    steps:
    - script: seq 200 | awk '{print $0 " hola mundo texto repetido"}' > corpus.txt && echo "ha" >> corpus.txt
    - publish: corpus.txt
      artifact: corpus
- stage: count
  jobs:
  - job: one
    steps:
    - download: corpus
      path: .
    - task: WordCount@1
      inputs: {file: corpus.txt, threads: 1, output: out1.txt, top: 5}
    - publish: out1.txt
      artifact: out1
  - job: many
    steps:
    - download: corpus
      path: .
    - task: WordCount@1
      inputs: {file: corpus.txt, threads: 8, output: out8.txt, top: 5}
    - publish: out8.txt
      artifact: out8
- stage: compare
  jobs:
  - job: cmp
    steps:
    - download: out1
      path: a
    - download: out8
      path: b
    - script: diff a/out1.txt b/out8.txt && echo "WC_THREADS_MATCH"
YAML

client SUBMIT "$TMP/wc_threads.yaml"
check "wordcount threads accepted" [ $? -eq 0 ]
WC_ID=$(cat "$TMP/out")
check "wordcount threads finishes" wait_finished "$WC_ID" 20
check "wordcount threads succeeds" [ "$(run_status "$WC_ID")" = "succeeded" ]
client LOGS "$WC_ID"
check "1 thread equals N threads" out_has "WC_THREADS_MATCH"



# ---------- continueOnError ----------

echo "continueOnError"
client SUBMIT tests/continue_on_error.yaml
check "continueOnError accepted" [ $? -eq 0 ]
CONT_ID=$(cat "$TMP/out")
check "continueOnError finishes" wait_finished "$CONT_ID" 10
check "continueOnError partially succeeds" [ "$(run_status "$CONT_ID")" = "partiallySucceeded" ]



# ---------- publish fail ----------

echo "publish fail"

client SUBMIT "$TMP/publish_fail.yaml"
check "publish fail accepted" [ $? -eq 0 ]
FAIL_ID=$(cat "$TMP/out")
check "publish fail run finishes" wait_finished "$FAIL_ID" 10
check "publish fail run failed" [ "$(run_status "$FAIL_ID")" = "failed" ]
client LOGS "$FAIL_ID"
check "logs have the error" out_has "##[error] publish failed."


# ---------- cancel data task ----------

echo "cancel data task"
client SUBMIT "$TMP/cancel_task.yaml"
CANCEL_ID=$(cat "$TMP/out")
# Wait until running
check "cancel task starts" wait_status "$CANCEL_ID" "running" 5
client CANCEL "$CANCEL_ID"
check "cancel accepted" out_has "canceling"
check "canceled run finishes" wait_status "$CANCEL_ID" "canceled" 5


# ---------- concurrent clients ----------

echo "concurrent clients"

# Wait only for the clients: a bare wait would also wait for the server, which never ends.
PIDS=()
for ((i = 0; i < 50; i++)); do
    "$CLIENT" "$HOST" "$PORT" LIST > "$TMP/list_$i" 2>&1 &
    PIDS+=($!)
done
for ((i = 0; i < ${#PIDS[@]}; i++)); do
    wait "${PIDS[$i]}"
done

ANSWERED=0
for ((i = 0; i < 50; i++)); do
    if grep -q "integration" "$TMP/list_$i"; then
        ANSWERED=$((ANSWERED + 1))
    fi
done
check "50 concurrent clients answered ($ANSWERED)" [ "$ANSWERED" -eq 50 ]



echo "shutdown"

client SHUTDOWN
check "shutdown accepted" out_has "shutting down"

for ((i = 0; i < 50; i++)); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

check "server exited" [ "$(kill -0 "$SERVER_PID" 2>/dev/null; echo $?)" -ne 0 ]

client LIST
check "no connections after shutdown" [ $? -ne 0 ]


echo
echo "$((CHECKS - FAILURES)) of $CHECKS checks passed"
[ "$FAILURES" -eq 0 ]