#!/usr/bin/env bash

# Run from repository root: bash bench/run_bench.sh

SERVER=./bin/pipelined
CLIENT=./bin/pipectl
HOST=127.0.0.1
PORT=20001
AGENTS=4
WORK="/tmp/bench_work"
rm -rf "$WORK"
mkdir -p "$WORK"

"$SERVER" "$PORT" "$AGENTS" "$WORK" > /dev/null 2>&1 &
SERVER_PID=$!

# Wait for server
for i in {1..10}; do
    if "$CLIENT" "$HOST" "$PORT" LIST > /dev/null 2>&1; then
        break
    fi
    sleep 0.5
done

RUN_TIMES=()
for i in {1..5}; do
    echo "Run $i..."
    START=$(date +%s.%N)
    
    # Submit and get ID
    RUN_ID=$("$CLIENT" "$HOST" "$PORT" SUBMIT bench/measure.yaml)
    
    # Wait
    while true; do
        STATUS=$("$CLIENT" "$HOST" "$PORT" STATUS "$RUN_ID" | head -1 | awk '{print $4}')
        if [[ "$STATUS" == "succeeded" || "$STATUS" == "failed" ]]; then
            break
        fi
        sleep 0.1
    done
    
    END=$(date +%s.%N)
    ELAPSED=$(awk "BEGIN {print $END - $START}")
    RUN_TIMES+=("$ELAPSED")
    echo "  Finished in $ELAPSED seconds"
done

kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null

# Print times
echo "Times: ${RUN_TIMES[*]}"
# Calculate median (simple sorting)
IFS=$'\n' sorted=($(sort -n <<<"${RUN_TIMES[*]}")); unset IFS
median=${sorted[2]}
echo "Median time: $median seconds"
