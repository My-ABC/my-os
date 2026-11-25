#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 无法使用true, false, 我的mini-libc没有定义bool类型

int main() {
    while (1) { // 初始化游戏循环
        srand(time(NULL)); // 设置种子为当前时间
        int number = rand() % 100 + 1; // 生成随机数（1-100）
        while (1) {
            printf("Guess the number (1-100): ");
            int guess;
            if (scanf("%d", &guess) != 1) {
                printf("Invalid input. Please enter a number.\n");
                continue;
            }
            if (guess < 1 || guess > 100) {
                printf("Please guess a number between 1 and 100.\n");
                continue;
            }
            if (guess < number) {
                printf("Too low! Try again.\n");
            } else if (guess > number) {
                printf("Too high! Try again.\n");
            } else {
                printf("Congratulations! You guessed the number %d!\n", number);
                break; // Start a new game
            }
        }

        printf("Do you want to play again? (y/n): ");
        char play_again;
        if (scanf(" %c", &play_again) != 1 || (play_again != 'y' && play_again != 'Y')) {
            printf("Thank you for playing!\n");
            break;
        }
    }
    return 0;
}