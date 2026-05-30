#!/bin/sh
set -e

echo "=== Building ==="
cmake --build build -j"$(nproc)"

echo "=== Starting server ==="
./build/growth-server &
SERVER_PID=$!

# Give the server a moment to bind
sleep 0.5

echo "=== Starting client ==="
./build/growth-client

# Client exited — kill the server
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
echo "=== Done ==="
