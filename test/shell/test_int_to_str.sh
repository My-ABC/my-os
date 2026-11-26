#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sh "$ROOT/test/support/compile_c_test" "$ROOT/test/c/test_int_to_str.c" \
    "$ROOT/src/lib/stdio.c"
