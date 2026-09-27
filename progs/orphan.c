// 子を残してすぐ終わる。子は親より後に終わるので孤児になり、PID 1 が回収する（回収されなければゾンビとして残る）
#include "../crt.h"

int main(int argc, char **argv, char **envp) {
    (void)argc, (void)argv, (void)envp;
    long pid = syscall3(SYS_fork, 0, 0, 0);
    if (pid == 0) {
        struct { long sec, nsec; } t = {0, 200000000};
        syscall3(SYS_nanosleep, &t, 0, 0);
        return 0;
    }
    print("left child "); print_u(pid); print("\n");
    return 0;
}
