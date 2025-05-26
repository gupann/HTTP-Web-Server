#!/usr/bin/env bash

# this integration test starts the server once, runs a series of test_* functions, then exits 0 (success) or 1 (failure)
# add more cases by defining additional test_* functions and listing them in TESTS=(...)

set -euo pipefail

# global config

BUILD_DIR="build"
PORT=${PORT:-8080} # use 8080 if no port set in env
CFG_FILE="$(mktemp)" # temp file for server config, will be deleted
SERVER_BIN="${BUILD_DIR}/bin/webserver"

# where to put our static files
STATIC_ROOT="$(mktemp -d)"

# lifecycle helpers

# this will be set
SERVER_PID=""

# build the code, write a minimal config, start the server, and wait for it to begin listening
setup_server() {
    echo "[SETUP] Building and starting server..."
    cmake -B "$BUILD_DIR" -S . >/dev/null
    cmake --build "$BUILD_DIR" >/dev/null

    # prepare a sample static directory
    mkdir -p "$STATIC_ROOT/static1"
    echo "<h1>Hello static1</h1>" > "$STATIC_ROOT/static1/index.html"

    # write config with port + locations
    cat > "$CFG_FILE" <<EOF
port $PORT;

location /static1 StaticHandler {
    root $STATIC_ROOT/static1;
}

location /echo EchoHandler {}

location /sleep SleepHandler {}
EOF

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
    rm -rf "$STATIC_ROOT"
}
trap teardown_server EXIT INT TERM


# test cases

# normal working case: server should echo request line in body
test_echo_request() {
    local body
    body=$(curl -s -S "http://localhost:$PORT/echo/" -H "Connection: close")
    grep -qE "^GET /echo/ HTTP/1\.[01]" <<<"$body" # body should match the valid response form
}

# malformed request via nc should not yield an HTTP status line
test_invalid_request() {
    local reply
    reply=$(printf 'GARBAGE\r\n' | nc -q0 localhost "$PORT" || true)
    ! grep -qE '^HTTP/1\.[01] ' <<<"$reply" # reply should NOT match the valid response form
}

# static-file serving returns the file contents
test_static_file() {
    local body
    body=$(curl -s "http://localhost:$PORT/static1/index.html")
    grep -q "<h1>Hello static1</h1>" <<<"$body"
}

# requesting a missing static file returns 404
test_static_404() {
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT/static1/notfound.txt")
    [ "$code" -eq 404 ]
}

# echo handler on /echo prefix
test_echo_handler() {
    local body
    body=$(curl -s "http://localhost:$PORT/echo/foo/bar" -H "X-Test: yes")
    grep -qE "^GET /echo/foo/bar HTTP/1\.[01]" <<<"$body"
    grep -q "X-Test: yes" <<<"$body"
}

# static file returns correct Content-Type
test_static_content_type() {
    local ct
    ct=$(curl -sI "http://localhost:$PORT/static1/index.html" | grep -i '^Content-Type:' | awk '{print $2}' | tr -d '\r')
    [ "$ct" = "text/html" ]
}

# non-static handler returns 404
test_404_handler() {
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT/notfound")
    [ "$code" -eq 404 ]
}

# /sleep blocks ~3 s; /echo must still return immediately
test_concurrent_requests() {
    # Fire blocking request in the background
    curl -s "http://localhost:$PORT/sleep" > /dev/null &
    sleeper_pid=$!

    start_ms=$(date +%s%3N)
    curl -s "http://localhost:$PORT/echo/quick" > /dev/null
    end_ms=$(date +%s%3N)
    elapsed=$((end_ms - start_ms))

    wait $sleeper_pid   # ensure background curl completes

    # Expect echo to finish in less than the 3 000 ms sleep delay
    [ "$elapsed" -lt 3000 ]
}


# register tests

TESTS=(
  test_echo_request
  test_invalid_request
  test_static_file
  test_static_404
  test_echo_handler
  test_static_content_type
  test_404_handler
  test_concurrent_requests
)

# main

setup_server

failures=0
for t in "${TESTS[@]}"; do
  printf "[RUN] %-25s ... " "$t"
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
