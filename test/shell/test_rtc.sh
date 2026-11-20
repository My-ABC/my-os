#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sh "$ROOT/test/support/compile_c_test" "$ROOT/test/c/test_rtc.c" \
    "$ROOT/src/drivers/rtc.c"
