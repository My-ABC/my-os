# `inttypes.h`

Length macros for `printf`/`scanf` over all stdint widths
(LP32: `int64_t`→`ll`, `intmax_t`=int so MAX macros carry no
length): `PRId* PRIi* PRIo* PRIu* PRIx* PRIX*` and the `SCN*`
scanf counterparts, incl. `LEAST/FAST/MAX/PTR` variants.

`imaxabs imaxdiv strtoimax strtoumax` + `imaxdiv_t{quot,rem}` —
delegate to the 32-bit stdlib implementations.

Note: the stdio formatter currently handles int/unsigned int;
64-bit `PR*` macros expand correctly but `%lld` is not parsed.
