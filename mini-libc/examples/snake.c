/* SPDX-License-Identifier: MIT */
/* snake.c — mini-libc 贪吃蛇示例
 *
 * 在文本控制台上玩贪吃蛇：WASD 移动，吃到食物变长并加分。
 * 渲染用 con_clear + con_cursor_set 直接画到字符网格；
 * 输入用 IO_GETCHAR 非阻塞轮询（无键时返回 0）。
 *
 * 编译：
 *   i686-elf-gcc -m32 -nostdlib -I./mini-libc/include \
 *       mini-libc/examples/snake.c -o build/snake.elf -L./build -lmyc
 */

#include <myos.h>
#include <console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>

#define W 20
#define H 10
#define MAXLEN (W * H)
#define TICK_US 120000UL   /* 每步 120ms */

static int sx[MAXLEN], sy[MAXLEN];   /* 蛇身，头在 [0] */
static int len;
static int dx = 1, dy = 0;           /* 方向 */
static int fx, fy;                   /* 食物 */
static int score;
static int over;

static int kbhit(void) {
    /* IO_GETCHAR_NB: 非阻塞——无键或非前台返回 0 */
    return (int)syscall0(SYS_IO, IO_GETCHAR_NB);
}

static void place_food(void) {
    int i, x, y, on;
    for (;;) {
        x = rand() % W;
        y = rand() % H;
        on = 0;
        for (i = 0; i < len; ++i)
            if (sx[i] == x && sy[i] == y) { on = 1; break; }
        if (!on) { fx = x; fy = y; return; }
    }
}

static void draw(void) {
    int i;
    con_clear();
    for (i = 0; i < len; ++i) {
        con_cursor_set((uint16_t)sy[i] + 1, (uint16_t)sx[i]);
        putchar(i == 0 ? '@' : '#');
    }
    con_cursor_set((uint16_t)fy + 1, (uint16_t)fx);
    putchar('o');
    con_cursor_set(H + 2, 0);
    printf("score=%d  WASD move, Ctrl+C quit\n", score);
    if (over) {
        con_cursor_set(H + 3, 0);
        printf("GAME OVER  final score=%d\n", score);
    }
}

static void step(void) {
    /* 穿墙：出左边从右边进，出上边从下边进 */
    int nx = (sx[0] + dx + W) % W;
    int ny = (sy[0] + dy + H) % H;
    int i;
    for (i = 0; i < len; ++i)
        if (sx[i] == nx && sy[i] == ny) { over = 1; return; }
    if (nx == fx && ny == fy) {
        if (len < MAXLEN) {
            memmove(&sx[1], &sx[0], len * sizeof(int));
            memmove(&sy[1], &sy[0], len * sizeof(int));
            ++len;
        }
        ++score;
        place_food();
    } else {
        memmove(&sx[1], &sx[0], (len - 1) * sizeof(int));
        memmove(&sy[1], &sy[0], (len - 1) * sizeof(int));
    }
    sx[0] = nx; sy[0] = ny;
}

int main(void) {
    int c;
    srand((unsigned)time(0));
    len = 3;
    sx[0] = W / 2; sy[0] = H / 2;
    sx[1] = sx[0] - 1; sy[1] = sy[0];
    sx[2] = sx[0] - 2; sy[2] = sy[0];
    place_food();
    draw();
    while (!over) {
        c = kbhit();
        if (c == 'w' || c == 'W') { if (dy == 0) { dx = 0; dy = -1; } }
        else if (c == 's' || c == 'S') { if (dy == 0) { dx = 0; dy = 1; } }
        else if (c == 'a' || c == 'A') { if (dx == 0) { dx = -1; dy = 0; } }
        else if (c == 'd' || c == 'D') { if (dx == 0) { dx = 1; dy = 0; } }
        step();
        draw();
        usleep(TICK_US);
    }
    return 0;
}
