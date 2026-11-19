#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$ROOT/test/compile_c_test" "$ROOT/test/test_rtc.c" "$ROOT/src/rtc.c"
