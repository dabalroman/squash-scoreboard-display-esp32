#!/usr/bin/env bash
# V1 sweep-layer invariants against ../../src (see check_v1.cpp).
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=gnu++11 -Wall -O0 -I shim -I ../../src -DBOARD_REV=1 -DCONFIG_IDF_TARGET_ESP32S2=1 -o check_v1 check_v1.cpp
./check_v1
