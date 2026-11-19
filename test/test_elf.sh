#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$ROOT/test/compile_c_test" "$ROOT/test/test_elf.c" "$ROOT/src/elf.c"
