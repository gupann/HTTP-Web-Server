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

    # ----------------------------------------------------------------------
    # 0. Build the code (only first time in this script run)
    # ----------------------------------------------------------------------
    cmake -B "$BUILD_DIR" -S . >/dev/null
    cmake --build "$BUILD_DIR" >/dev/null

    # ----------------------------------------------------------------------
    # 1. Prepare sample static directory used by existing tests
    # ----------------------------------------------------------------------
    mkdir -p "$STATIC_ROOT/static1"
    echo "<h1>Hello static1</h1>" > "$STATIC_ROOT/static1/index.html"

    # ----------------------------------------------------------------------
    # 2. Prepare Markdown docs + wrapper template for new tests
    # ----------------------------------------------------------------------
    DOCS_DIR="$(mktemp -d)"
    mkdir -p "$DOCS_DIR/sub"
    echo '# Hello MD'            > "$DOCS_DIR/index.md"
    echo '# Subpage'             > "$DOCS_DIR/sub/page.md"

    TPL_FILE="$(mktemp)"
    cat > "$TPL_FILE" <<TPL
<!DOCTYPE html>
<html><head><title>Docs</title></head>
<body>
{{content}}
</body></html>
TPL

    # ----------------------------------------------------------------------
    # 3. Write a minimal nginx-style config that references those paths
    # ----------------------------------------------------------------------
    cat > "$CFG_FILE" <<EOF
port $PORT;

location /static1 StaticHandler {
    root $STATIC_ROOT/static1;
}

location /echo EchoHandler {}

location /sleep SleepHandler {}

location /health HealthRequestHandler {}

location /crud CrudHandler {
    data_path $(mktemp -d)/crud;
}

location /docs MarkdownHandler {
    root $DOCS_DIR;
    template $TPL_FILE;
}

location /static/docs_assets StaticHandler {
    root ./static/docs_assets;
}
EOF

    # ----------------------------------------------------------------------
    # 4. Launch the server in the background
    # ----------------------------------------------------------------------
    "$SERVER_BIN" "$CFG_FILE" >/dev/null 2>&1 &
    SERVER_PID=$!

    # Wait (≤5 s) until the port accepts connections
    for _ in {1..25}; do
        nc -z localhost "$PORT" &>/dev/null && return
        sleep 0.2
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

# malformed request via nc should yield HTTP/1.x 400
test_invalid_request() {
    local reply
    # note the double CRLF so the parser considers the request "complete"
    reply=$(printf 'GARBAGE\r\n\r\n' | nc -q0 localhost "$PORT" || true)
    grep -qE '^HTTP/1\.[01] 400 ' <<<"$reply"
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

# /health must return 200 and body "OK"
test_health_handler() {
    local body code
    body=$(curl -s -w "%{http_code}" "http://localhost:$PORT/health")
    code=${body: -3}          # last 3 chars are the status
    body=${body::${#body}-3}  # strip them off
    [ "$code" -eq 200 ] && [ "$body" = "OK" ]
}

# test basic CRUD workflow
test_crud_basic_workflow() {
    # Create an entity
    local create_response code id
    create_response=$(curl -s -w "\nHTTP_STATUS:%{http_code}" -X POST \
        -H "Content-Type: application/json" \
        -d '{"name":"test"}' \
        "http://localhost:$PORT/crud/Items")
    
    code=$(echo "$create_response" | grep "HTTP_STATUS:" | cut -d: -f2)
    [ "$code" -eq 201 ]
    
    # Extract ID and read the entity
    id=$(echo "$create_response" | grep -o '"id":[0-9]*' | cut -d: -f2)
    local read_code
    read_code=$(curl -s -o /dev/null -w "%{http_code}" \
        "http://localhost:$PORT/crud/Items/$id")
    [ "$read_code" -eq 200 ]
    
    # Delete the entity
    local delete_code
    delete_code=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE \
        "http://localhost:$PORT/crud/Items/$id")
    [ "$delete_code" -eq 204 ]
}

# test CRUD error handling
test_crud_error_handling() {
    # Test invalid JSON
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
        -H "Content-Type: application/json" \
        -d '{invalid}' \
        "http://localhost:$PORT/crud/Test")
    [ "$code" -eq 400 ]
    
    # Test reading non-existent entity
    code=$(curl -s -o /dev/null -w "%{http_code}" \
        "http://localhost:$PORT/crud/Test/999")
    [ "$code" -eq 404 ]
}

# Markdown handler happy-path (HTML render)
test_markdown_render() {
    local body ct
    ct=$(curl -sI "http://localhost:$PORT/docs/index.md" | grep -i '^Content-Type:' | awk '{print $2}' | tr -d '\r')
    body=$(curl -s  "http://localhost:$PORT/docs/index.md")
    grep -q "<h1>Hello MD</h1>" <<<"$body" && [ "$ct" = "text/html" ]
}

# Directory index + 5-second cache
test_directory_index_cache() {
    # First hit – expect 200 + listing
    local first etag second
    first=$(curl -sD - "http://localhost:$PORT/docs/" -o /dev/null)
    etag=$(grep -i '^ETag:' <<<"$first" | awk '{print $2}' | tr -d '\r')
    grep -q "index.md" <<<"$first"

    # Second hit within 5s must return identical ETag (served from cache)
    second=$(curl -sD - "http://localhost:$PORT/docs/" -o /dev/null)
    grep -i '^ETag:' <<<"$second" | grep -q "$etag"
}

# Raw mode
test_markdown_raw_mode() {
    local ct body
    ct=$(curl -sI "http://localhost:$PORT/docs/index.md?raw=1" | grep -i '^Content-Type:' | awk '{print $2}' | tr -d '\r')
    body=$(curl -s  "http://localhost:$PORT/docs/index.md?raw=1")
    grep -q "# Hello MD" <<<"$body" && [ "$ct" = "text/markdown" ]
}

# Conditional GET (If-None-Match)
test_markdown_304() {
    local etag
    etag=$(curl -sD - "http://localhost:$PORT/docs/index.md" -o /dev/null | \
           grep -i '^ETag:' | awk '{print $2}' | tr -d '\r')
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" \
           -H "If-None-Match: $etag" \
           "http://localhost:$PORT/docs/index.md")
    [ "$code" -eq 304 ]
}

# Gzip compression (>1 KB + Accept-Encoding)
test_markdown_gzip() {
    # generate a big page (>1 KB) on the fly
    big_md="$DOCS_DIR/big.md"
    yes 'This is a long line to inflate file size.' | head -n 200 > "$big_md"

    local encoding
    encoding=$(curl -sI -H "Accept-Encoding: gzip" \
               "http://localhost:$PORT/docs/big.md" | grep -i '^Content-Encoding:' | awk '{print $2}' | tr -d '\r')
    [ "$encoding" = "gzip" ]
}

# Path traversal blocked
test_markdown_traversal() {
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" \
          "http://localhost:$PORT/docs/../../etc/passwd")
    [ "$code" -eq 404 ]
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
  test_health_handler
  test_crud_basic_workflow
  test_crud_error_handling
  test_markdown_render
  test_directory_index_cache
  test_markdown_raw_mode
  test_markdown_304
  test_markdown_gzip
  test_markdown_traversal
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
