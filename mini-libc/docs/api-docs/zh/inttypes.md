# `inttypes.h`

覆盖所有 stdint 宽度的 `printf`/`scanf` 长度宏
（LP32：`int64_t`→`ll`，`intmax_t`=int 故 MAX 宏无
长度修饰）：`PRId* PRIi* PRIo* PRIu* PRIx* PRIX*`
及 `SCN*` scanf 对应宏，含 `LEAST/FAST/MAX/PTR` 变体。

`imaxabs imaxdiv strtoimax strtoumax` + `imaxdiv_t{quot,rem}`
—— 委托给 32 位 stdlib 实现。

注意：stdio 格式化器目前只处理 int/unsigned int；
64 位 `PR*` 宏展开正确但 `%lld` 尚未解析。
