#!/usr/bin/env bash
# V2 glyph-layer invariants against ../../src (see check_v2.cpp).
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=gnu++11 -Wall -O0 -I shim -I ../../src -DBOARD_REV=2 -DCONFIG_IDF_TARGET_ESP32S3=1 -o check_v2 check_v2.cpp
./check_v2
