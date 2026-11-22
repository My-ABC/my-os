/* SPDX-License-Identifier: MIT */
#include <inttypes.h>
#include <stdlib.h>

/* intmax_t/uintmax_t are int/unsigned int on LP32, so the imax
 * family delegates to the 32-bit implementations in stdlib.c. */

intmax_t imaxabs(intmax_t j) {
    return j < 0 ? (intmax_t)(-(uintmax_t)j) : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

intmax_t strtoimax(const char *nptr, char **endptr, int base) {
    return (intmax_t)strtol(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
    return (uintmax_t)strtoul(nptr, endptr, base);
}
