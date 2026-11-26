#include "test_support.h"

/* apply_modifiers is static inside keyboard.c; include the TU so the
 * host test can reach it. --gc-sections drops the hardware-facing
 * functions we never call. */
#include "../../src/drivers/keyboard.c"

int main(void) {
    check(apply_modifiers('a', 0, 0) == 'a', "a: no modifiers");
    check(apply_modifiers('a', 1, 0) == 'A', "a: shift");
    check(apply_modifiers('a', 0, 1) == 'A', "a: caps");
    check(apply_modifiers('a', 1, 1) == 'a', "a: shift+caps cancels");
    /* Shift+digit goes through shift_symbols: '1' -> '!' */
    check(apply_modifiers('1', 1, 0) == '!', "digit: shift symbol");
    check(apply_modifiers('1', 0, 1) == '1', "digit: caps no-op");
    return finish_test_suite("apply_modifiers");
}
