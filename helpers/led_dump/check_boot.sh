#!/usr/bin/env bash
# Boot sweep byte-identity (task #52): V1 and V2 frames vs golden_boot_v{1,2}.txt.
#   ./check_boot.sh           diff against the goldens
#   ./check_boot.sh --golden  write the goldens - refuses if they already exist;
#                             they were taken once from the pre-layer sweep.
set -euo pipefail
cd "$(dirname "$0")"

GOLDEN=0
[ "${1:-}" = "--golden" ] && GOLDEN=1

rc=0
for rev in 1 2; do
    if [ "$rev" = 1 ]; then target="-DCONFIG_IDF_TARGET_ESP32S2=1"; else target="-DCONFIG_IDF_TARGET_ESP32S3=1"; fi
    g++ -std=gnu++11 -Wall -O0 -I shim -I ../../src -DBOARD_REV=$rev $target -o boot_dump boot_dump.cpp
    ./boot_dump > out_boot_v$rev.txt

    if [ "$GOLDEN" = 1 ]; then
        if [ -e golden_boot_v$rev.txt ]; then
            echo "refusing: golden_boot_v$rev.txt exists (baseline of the pre-layer sweep)" >&2
            exit 2
        fi
        cp out_boot_v$rev.txt golden_boot_v$rev.txt
        echo "golden_boot_v$rev.txt written: $(wc -l < golden_boot_v$rev.txt) frames"
        continue
    fi

    if diff -q golden_boot_v$rev.txt out_boot_v$rev.txt > /dev/null; then
        echo "OK: v$rev boot frames identical ($(wc -l < out_boot_v$rev.txt) frames)"
    else
        diff -u golden_boot_v$rev.txt out_boot_v$rev.txt | head -20
        echo "FAIL: v$rev boot frames differ" >&2
        rc=1
    fi
done
exit $rc
