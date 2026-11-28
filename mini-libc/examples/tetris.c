/* SPDX-License-Identifier: MIT */
/* tetris.c — mini-libc 俄罗斯方块示例
 *
 * 10x20 棋盘 + 边框，7 种四连块各 4 个旋转状态。方向键
 * 0x01-0x04 控制（上=旋转，下=硬降到底），空格旋转，'q' 退出，Ctrl+C 由
 * 内核 SIGINT 默认处置终止进程。
 *
 * 渲染：所有内容先画进一块"画布"缓冲，再与上一帧影子缓冲
 * 逐格比较，只用 con_cursor_set + putchar 写出差异格。
 * 下落、旋转、消行都走同一条 diff 路径，因此天然满足
 * "只重绘变化的格子"（消行时整屏逐行比较，未变的行不写）。
 *
 * 布局按 con_size 实际尺寸自适应：优先右侧预览面板，放不下
 * 依次退化为面板置底、无预览、无边框的 10x20 裸棋盘。
 *
 * 编译：make tetris   （产物 build/tetris.elf）
 */

#include <myos.h>
#include <console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>

#define BW 10              /* 棋盘宽（不含边框） */
#define BH 20              /* 棋盘高（不含边框） */
#define NP 7               /* 方块种类数 */
#define TICK_US 10000UL    /* 主循环轮询周期：1 个 PIT tick = 10ms */

/* 与内核 include/drivers/keyboard.h 一致的扩展键编码 */
#define KEY_UP      0x01
#define KEY_DOWN    0x02
#define KEY_LEFT    0x03
#define KEY_RIGHT   0x04
#define KEY_CTRL_C  0x05

#define MAXW 40
#define MAXH 40

/* 7 种方块 x 4 旋转 x 4 格坐标 (x,y)，坐标系左上角为原点 */
static const signed char SHP[NP][4][4][2] = {
    /* I */
    {{{0,1},{1,1},{2,1},{3,1}},
     {{2,0},{2,1},{2,2},{2,3}},
     {{0,2},{1,2},{2,2},{3,2}},
     {{1,0},{1,1},{1,2},{1,3}}},
    /* O */
    {{{1,0},{2,0},{1,1},{2,1}},
     {{1,0},{2,0},{1,1},{2,1}},
     {{1,0},{2,0},{1,1},{2,1}},
     {{1,0},{2,0},{1,1},{2,1}}},
    /* T */
    {{{1,0},{0,1},{1,1},{2,1}},
     {{1,0},{1,1},{2,1},{1,2}},
     {{0,1},{1,1},{2,1},{1,2}},
     {{1,0},{0,1},{1,1},{1,2}}},
    /* S */
    {{{1,0},{2,0},{0,1},{1,1}},
     {{1,0},{1,1},{2,1},{2,2}},
     {{1,1},{2,1},{0,2},{1,2}},
     {{0,0},{0,1},{1,1},{1,2}}},
    /* Z */
    {{{0,0},{1,0},{1,1},{2,1}},
     {{2,0},{1,1},{2,1},{1,2}},
     {{0,1},{1,1},{1,2},{2,2}},
     {{1,0},{0,1},{1,1},{0,2}}},
    /* J */
    {{{0,0},{0,1},{1,1},{2,1}},
     {{1,0},{2,0},{1,1},{1,2}},
     {{0,1},{1,1},{2,1},{2,2}},
     {{1,0},{1,1},{0,2},{1,2}}},
    /* L */
    {{{2,0},{0,1},{1,1},{2,1}},
     {{1,0},{1,1},{1,2},{2,2}},
     {{0,1},{1,1},{2,1},{0,2}},
     {{0,0},{1,0},{1,1},{1,2}}},
};

/* ---- 布局：画布即屏幕坐标，原点 (0,0) ---- */
typedef struct {
    int w, h;          /* 画布宽高（字符单元格） */
    int bx, by;        /* 棋盘（含边框）左上角 */
    int bordered;      /* 是否画边框 */
    int px, py;        /* 预览面板原点；px<0 表示无面板 */
    int score_row;     /* 分数行画布行号；<0 表示不单独显示 */
} Layout;

static const Layout LAYOUTS[] = {
    /* 右侧面板：12x22 边框棋盘 + 10 列面板 */
    {25, 23, 1, 1, 1, 15, 2, -1},
    /* 面板置底：适合又窄又高的屏 */
    {13, 35, 1, 1, 1,  1, 24, -1},
    /* 只有边框棋盘 + 底部分数行 */
    {12, 23, 0, 0, 1, -1, -1, 22},
    /* 边框棋盘，无分数行 */
    {12, 22, 0, 0, 1, -1, -1, -1},
    /* 10x20 裸棋盘 + 分数行 */
    {10, 21, 0, 0, 0, -1, -1, 20},
    /* 10x20 裸棋盘，最小可用布局 */
    {10, 20, 0, 0, 0, -1, -1, -1},
};

static Layout lay;
static int CW, CH;                    /* 当前画布尺寸 */
static uint16_t scr_rows, scr_cols;   /* con_size 实测值 */
static unsigned char cv[MAXW * MAXH]; /* 本帧画布 */
static unsigned char sh[MAXW * MAXH]; /* 上帧影子（0 = 未知，首帧全画） */

/* ---- 游戏状态 ---- */
static unsigned char board[BH][BW];   /* 0=空，否则 piece+1 */
static int ptype, prot, px, py;       /* 当前活动方块 */
static int ntype;                     /* 下一块预览 */
static long score;
static int lines;
static int over, quit;

/* ================= 输入 ================= */

static int getchar_nb(void) {
    /* mini-libc 没有 getchar_nb() 封装，直接走 SYS_IO/IO_GETCHAR_NB；
     * 无键或非前台进程返回 0。Ctrl+C 的 0x05 字节被内核静默消费，
     * 以 SIGINT 投递（默认处置 = 致命，进程直接退出）。 */
    return (int)syscall0(SYS_IO, IO_GETCHAR_NB);
}

static unsigned long drop_us(void) {
    /* 初始 500ms，每满 5 行消 -50ms，下限 80ms */
    long us = 500000L - (lines / 5) * 50000L;
    return (unsigned long)(us < 80000L ? 80000L : us);
}

/* ---- 真实时钟：RDTSC ----
 * 内核只置 CR4.PAE 未置 TSD，Ring 3 可直接 rdtsc。
 * TSC 频率在启动时用一次 usleep(200ms)（PIT tick 截止，
 * 误差 <10ms）标定；拿不到频率时退回按 TICK_US 记账。 */
static uint64_t tsc_hz;

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void tsc_calibrate(void) {
    uint64_t a = rdtsc();
    usleep(200000UL);
    uint64_t b = rdtsc();
    if (b > a) tsc_hz = (b - a) * 5ULL;   /* 200ms → ×5 = cycles/s */
}

/* 64/64 无符号除法：-nostdlib 链接没有 __udivdi3，
 * 每帧调一次、迭代 64 次的开销可忽略。 */
static uint64_t udiv64(uint64_t n, uint64_t d) {
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1U);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    return q;
}

/* 自上次调用以来的真实流逝微秒数 */
static unsigned long elapsed_us(uint64_t *last) {
    uint64_t now, dcy;
    unsigned long dt;
    if (tsc_hz == 0) return TICK_US;      /* 标定失败：退回固定节拍 */
    now = rdtsc();
    dcy = now - *last;
    *last = now;
    if (dcy >= tsc_hz) return 1000000UL;  /* >1s：封顶，防瞬移 */
    dt = (unsigned long)udiv64(dcy * 1000000ULL, tsc_hz);
    return dt > 50000UL ? 50000UL : dt;   /* 单帧最多记 50ms */
}

/* ================= 方块逻辑 ================= */

static int collide(int t, int r, int x, int y) {
    int i, cx, cy;
    for (i = 0; i < 4; ++i) {
        cx = x + SHP[t][r][i][0];
        cy = y + SHP[t][r][i][1];
        if (cx < 0 || cx >= BW) return 1;
        if (cy < 0) continue;          /* 棋盘上方允许探出 */
        if (cy >= BH || board[cy][cx] != 0) return 1;
    }
    return 0;
}

static void try_left(void)  { if (!collide(ptype, prot, px - 1, py)) --px; }
static void try_right(void) { if (!collide(ptype, prot, px + 1, py)) ++px; }

static void lock_piece(void);

/* 硬降：直接落到不能再下落的位置，然后锁定 */
static void hard_drop(void) {
    while (!collide(ptype, prot, px, py + 1)) ++py;
    lock_piece();
}

static void try_rot(void) {
    static const int kicks[] = {0, -1, 1, -2, 2};
    int nr = (prot + 1) & 3, i, nx;
    for (i = 0; i < 5; ++i) {
        nx = px + kicks[i];
        if (!collide(ptype, nr, nx, py)) {
            prot = nr;
            px = nx;
            return;
        }
    }
}

static void spawn(void) {
    static const int sx = (BW - 4) / 2;
    ptype = ntype;
    ntype = rand() % NP;
    prot = 0;
    px = sx;
    py = 0;
}

static void gravity_step(void) {
    if (!collide(ptype, prot, px, py + 1)) {
        ++py;
        return;
    }
    lock_piece();
}

/* 落定：写入棋盘、消行、出下一块 */
static void lock_piece(void) {
    {
        int i, cx, cy;
        for (i = 0; i < 4; ++i) {
            cx = px + SHP[ptype][prot][i][0];
            cy = py + SHP[ptype][prot][i][1];
            if (cy >= 0 && cy < BH && cx >= 0 && cx < BW)
                board[cy][cx] = (unsigned char)(ptype + 1);
        }
    }
    /* 消行：找满行，从底向上压实 */
    {
        unsigned char full[BH];
        int r, w, nfull = 0;
        static const int pts[5] = {0, 100, 300, 500, 800};
        memset(full, 0, sizeof(full));
        for (r = 0; r < BH; ++r) {
            int c, n = 0;
            for (c = 0; c < BW; ++c)
                if (board[r][c] != 0) ++n;
            if (n == BW) { full[r] = 1; ++nfull; }
        }
        if (nfull != 0) {
            score += pts[nfull];
            lines += nfull;
            w = BH - 1;
            for (r = BH - 1; r >= 0; --r) {
                if (full[r]) continue;
                if (w != r) memcpy(board[w], board[r], BW);
                --w;
            }
            while (w >= 0) { memset(board[w], 0, BW); --w; }
        }
    }
    spawn();
    if (collide(ptype, prot, px, py)) over = 1;   /* 堆到顶 */
}

static void handle_key(int c) {
    switch (c) {
    case KEY_LEFT:  try_left();  break;
    case KEY_RIGHT: try_right(); break;
    case KEY_DOWN:  hard_drop(); break;
    case KEY_UP:    try_rot();   break;   /* 上=旋转（规格外的便利映射） */
    case ' ':       try_rot();   break;
    case 'q': case 'Q': quit = 1; break;
    case KEY_CTRL_C: quit = 1;   break;   /* 正常到不了：内核已转 SIGINT */
    default: break;                        /* 其余按键一律不打印 */
    }
}

/* ================= 渲染 ================= */

static void cvput(int x, int y, char c) {
    if (x >= 0 && x < CW && y >= 0 && y < CH)
        cv[y * CW + x] = (unsigned char)c;
}

static void text_at(int x, int y, const char *s) {
    int i;
    for (i = 0; s[i] != '\0' && x + i < CW; ++i)
        cvput(x + i, y, s[i]);
}

static int ox(void) { return lay.bx + (lay.bordered ? 1 : 0); }
static int oy(void) { return lay.by + (lay.bordered ? 1 : 0); }

static void draw_border(void) {
    int x, y, x1 = lay.bx + BW + 1, y1 = lay.by + BH + 1;
    for (x = lay.bx; x <= x1; ++x) {
        cvput(x, lay.by, '-');
        cvput(x, y1, '-');
    }
    for (y = lay.by; y <= y1; ++y) {
        cvput(lay.bx, y, '|');
        cvput(x1, y, '|');
    }
    cvput(lay.bx, lay.by, '+');
    cvput(x1, lay.by, '+');
    cvput(lay.bx, y1, '+');
    cvput(x1, y1, '+');
}

static void draw_piece_cells(int t, int r, int x, int y, char ch) {
    int i, cx, cy;
    for (i = 0; i < 4; ++i) {
        cx = ox() + x + SHP[t][r][i][0];
        cy = oy() + y + SHP[t][r][i][1];
        cvput(cx, cy, ch);
    }
}

static void draw_panel(void) {
    char b[48];
    int i;
    if (lay.px < 0) return;
    text_at(lay.px, lay.py, "NEXT");
    /* 下一块 4x4 预览，单字符格 */
    for (i = 0; i < 4; ++i) {
        int cx = lay.px + 1 + SHP[ntype][0][i][0];
        int cy = lay.py + 1 + SHP[ntype][0][i][1];
        cvput(cx, cy, '#');
    }
    snprintf(b, sizeof(b), "SCORE=%ld", score);
    text_at(lay.px, lay.py + 6, b);
    snprintf(b, sizeof(b), "LINES=%d", lines);
    text_at(lay.px, lay.py + 7, b);
    snprintf(b, sizeof(b), "DROP=%lums", drop_us() / 1000UL);
    text_at(lay.px, lay.py + 8, b);
    text_at(lay.px, lay.py + 10, "Q=QUIT");
}

static void draw_score_line(void) {
    char b[64];
    if (lay.score_row < 0) return;
    snprintf(b, sizeof(b), "SCORE=%ld  LINES=%d  DROP=%lums",
             score, lines, drop_us() / 1000UL);
    text_at((CW - (int)strlen(b)) / 2, lay.score_row, b);
}

static void build_canvas(void) {
    int r, c, i;
    for (i = 0; i < CW * CH; ++i) cv[i] = ' ';
    if (lay.bordered) draw_border();
    for (r = 0; r < BH; ++r)
        for (c = 0; c < BW; ++c)
            if (board[r][c] != 0)
                cvput(ox() + c, oy() + r, '#');
    draw_piece_cells(ptype, prot, px, py, '#');
    draw_panel();
    draw_score_line();
}

/* 逐格 diff：只写与上帧不同的单元格 */
static void flush(void) {
    int i, cx, cy;
    for (i = 0; i < CW * CH; ++i) {
        if (cv[i] == sh[i]) continue;
        cy = i / CW;
        cx = i % CW;
        /* 双保险：绝不写屏幕最后一格，vga_putchar 会在那里滚屏 */
        if (cy >= scr_rows || cx >= scr_cols) continue;
        if (cy == (int)scr_rows - 1 && cx == (int)scr_cols - 1) continue;
        con_cursor_set((uint16_t)cy, (uint16_t)cx);
        putchar((int)cv[i]);
        sh[i] = cv[i];
    }
}

static void choose_layout(void) {
    size_t i, n = sizeof(LAYOUTS) / sizeof(LAYOUTS[0]);
    if (con_size(&scr_rows, &scr_cols) != 0 || scr_rows == 0 || scr_cols == 0) {
        scr_rows = 25;
        scr_cols = 80;
    }
    for (i = 0; i < n; ++i) {
        /* 画布放在 (0,0)：需要 h <= rows-1，避免占用最后一行 */
        if (LAYOUTS[i].w <= (int)scr_cols &&
            LAYOUTS[i].h <= (int)scr_rows - 1) {
            lay = LAYOUTS[i];
            break;
        }
    }
    if (i == n) {
        /* 比最小布局还小：裁剪到屏幕，尽量能玩 */
        lay.w = (int)scr_cols < 10 ? (int)scr_cols : 10;
        lay.h = (int)scr_rows - 1 < 20 ? (int)scr_rows - 1 : 20;
        if (lay.w < 1) lay.w = 1;
        if (lay.h < 1) lay.h = 1;
        lay.bx = lay.by = 0;
        lay.bordered = 0;
        lay.px = lay.score_row = -1;
    }
    CW = lay.w;
    CH = lay.h;
}

int main(void) {
    unsigned long elapsed = 0;
    uint64_t last_tsc = 0;
    int c, drained;

    choose_layout();
    srand((unsigned)time(NULL));
    memset(board, 0, sizeof(board));
    ntype = rand() % NP;
    spawn();
    tsc_calibrate();
    last_tsc = rdtsc();

    con_clear();                 /* 只在开局清屏一次 */
    build_canvas();
    flush();                     /* sh 全 0 → 首帧整画布画出 */

    while (!over && !quit) {
        /* 排空键盘缓冲：每 tick 最多处理 32 个按键 */
        for (drained = 0; drained < 32; ++drained) {
            c = getchar_nb();
            if (c == 0) break;
            handle_key(c);
            if (over || quit) break;
        }
        /* 按真实流逝时间记账：被调度器抢占/睡眠粒度造成的
         * 额外延迟不再被算成游戏时间，方块才会按 drop_us()
         * 的设计速度下落。 */
        elapsed += elapsed_us(&last_tsc);
        if (elapsed >= drop_us()) {
            elapsed -= drop_us();
            gravity_step();
        }
        if (over || quit) break;
        build_canvas();
        flush();
        usleep(TICK_US);          /* 10ms 轮询节拍，期间不重绘 */
    }

    con_clear();                 /* Game Over / 退出时才再清屏 */
    if (over)
        printf("Game Over, score=%d\n", (int)score);
    else
        printf("Quit, score=%d\n", (int)score);
    return 0;
}
