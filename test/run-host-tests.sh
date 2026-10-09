#!/usr/bin/env bash
# Build and run the host-side effect harnesses. No PlatformIO, no hardware:
# plain g++ against test/wled_stub.hpp.
set -euo pipefail

TEST_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
OUT_DIR=${TMPDIR:-/tmp}/wled-sprite-usermod-tests
mkdir -p "$OUT_DIR"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/wind_harness" "$TEST_DIR/wind_harness.cpp"

"$OUT_DIR/wind_harness" "$@"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/leaves_harness" "$TEST_DIR/leaves_harness.cpp"
"$OUT_DIR/leaves_harness"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/flow_harness" "$TEST_DIR/flow_harness.cpp"
"$OUT_DIR/flow_harness"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/exchange_harness" "$TEST_DIR/exchange_harness.cpp"
"$OUT_DIR/exchange_harness"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/precip_harness" "$TEST_DIR/precip_harness.cpp"
"$OUT_DIR/precip_harness"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/cloud_deck_harness" "$TEST_DIR/cloud_deck_harness.cpp"
"$OUT_DIR/cloud_deck_harness"

g++ -std=c++17 -O1 -Wall -Wno-unused-function \
  -o "$OUT_DIR/chaos_harness" "$TEST_DIR/chaos_harness.cpp"
"$OUT_DIR/chaos_harness"
