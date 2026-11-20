#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sh "$ROOT/test/support/compile_c_test" "$ROOT/test/c/test_device_drivers.c" \
    "$ROOT/src/drivers/keyboard.c" "$ROOT/src/drivers/pic.c" \
    "$ROOT/src/drivers/pit.c" "$ROOT/src/drivers/serial.c"
