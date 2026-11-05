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

out_has() { grep -qF -- "$1" "$TMP/out"; }
out_lines() { wc -l < "$TMP/out"; }



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

ELAPSED=$(echo "$END - $START" | bc)
check "never more jobs than agents (took ${ELAPSED}s)" [ "$(echo "$ELAPSED >= 1.9" | bc)" -eq 1 ]



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