---
paths:
  - "test/**"
---

# Host tests

- The native envs use CLion's bundled MinGW: `test/tools/native_toolchain.py` picks the newest `C:/Program Files/JetBrains/CLion */bin/mingw/bin`, prints the g++ it chose and fails loudly if none exists. It replaces `version_increment.py`, so a test run never bumps `version.txt`. Tests include headers only; `src/*.cpp` is never compiled (a suite that needs one `#include`s it).
- Suites: `test_boot` (boot sweep byte-identical to `test/fixtures/golden_boot_v<n>.txt`), `test_v1_goldens` (V1 glyph + frame dumps vs the goldens taken before the V2 work), `test_v2_glyphs` (V2 slot invariants + brightness compensation), `test_rules`, `test_layers`, `test_sweep`, `test_intro` (both boards). Shared code in `test/common/`, the Arduino/FastLED host shim in `test/shim/`.
- A new suite is a `test/test_<name>/test_main.cpp` (runs on both boards); `test_v1_*` / `test_v2_*` run on one board only.
- Fixtures are `-text` in `.gitattributes`. A golden mismatch writes the actual output to `.pio/test-out/<env>/` and names the first differing line. Rebaseline only by copying that output over the fixture on purpose, with the reason in the commit; a mismatch caused by the toolchain is never rebaselined.
