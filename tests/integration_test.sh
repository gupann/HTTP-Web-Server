#!/usr/bin/env bash

# this integration test starts the server once, runs a series of test_* functions, then exits 0 (success) or 1 (failure)
# add more cases by defining additional test_* functions and listing them in TESTS=(...)

set -euo pipefail

# global config

BUILD_DIR="build"
PORT=${PORT:-8080} # use 8080 if no port set in env
CFG_FILE="$(mktemp)" # temp file for server config, will be deleted
SERVER_BIN="${BUILD_DIR}/bin/webserver"


# lifecycle helpers

# this will be set
SERVER_PID=""

# build the code, write a minimal config, start the server, and wait for it to begin listening
setup_server() {
    echo "[SETUP] Building and starting server..."
    # echo "[SETUP] CWD = $(pwd)"
    
    # rm -f "CMakeCache.txt"
    # mkdir -p $BUILD_DIR && cd build && cmake .. && make && cd .. # follow same normal build steps from assignment 1

    # the below two lines are more reliable than above
    cmake -B "$BUILD_DIR" -S . >/dev/null # create files for build
    cmake --build "$BUILD_DIR" >/dev/null # execute the build

    echo "port $PORT;" > "$CFG_FILE" # write the port to our temp config file

    "$SERVER_BIN" "$CFG_FILE" >/dev/null 2>&1 & # run server in background
    SERVER_PID=$! # set SERVER_PID to PID of last background job (server in bg)

    # wait (max 5s) until port accepts connections.
    for _ in {1..25}; do
        nc -z localhost "$PORT" &>/dev/null && return # keep probing
        sleep .2
    done
    echo "Server failed to listen on port $PORT"
    exit 1
}

teardown_server() {
    echo "[TEARDOWN] Stopping server"
    [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$CFG_FILE"
}
trap teardown_server EXIT INT TERM


# test cases

# normal working case: server should echo request line in body
test_valid_request() {
    local body
    body=$(curl -s -S "http://localhost:$PORT/" -H "Connection: close")
    grep -qE "^GET / HTTP/1\.[01]" <<<"$body" # body should match the valid response form
}

# malformed request via nc should not yield an HTTP status line
test_invalid_request() {
    local reply
    reply=$(printf 'GARBAGE\r\n' | nc -q0 localhost "$PORT" || true)
    ! grep -qE '^HTTP/1\.[01] ' <<<"$reply" # reply should NOT match the valid response form
}


# register tests

TESTS=(
  test_valid_request
  test_invalid_request
)

# main

setup_server

failures=0
for t in "${TESTS[@]}"; do
  printf "[RUN] %-20s ... " "$t"
  if "$t"; then
    echo "PASS"
  else
    echo "FAIL"
    ((failures++))
  fi
done

if ((failures)); then
  echo "$failures test(s) failed"
  exit 1
else
  echo "All ${#TESTS[@]} test(s) passed"
  exit 0
fi

# notes:
# we currently use a single instance of the server for all test cases because there is no shared state. there is currently no need to restart.
# trap teardown_server EXIT INT TERM guarantees server shutdown on failure, so the server doesn't stray and continue running in the background
