// 止められるまで 0.1 秒ごとに眠り続ける（Ctrl+C の確認用）
#include "../crt.h"

int main(int argc, char **argv, char **envp) {
    (void)argc, (void)argv, (void)envp;
    print("spinning... press Ctrl+C\n");
    struct { long sec, nsec; } t = {0, 100000000};
    for (;;) syscall3(SYS_nanosleep, &t, 0, 0);
}
