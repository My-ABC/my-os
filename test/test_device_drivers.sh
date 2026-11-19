#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$ROOT/test/compile_c_test" "$ROOT/test/test_device_drivers.c" \
    "$ROOT/src/keyboard.c" "$ROOT/src/pic.c" "$ROOT/src/pit.c" \
    "$ROOT/src/serial.c"
