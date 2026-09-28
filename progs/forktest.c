// fork・execve・wait4・nanosleep を確かめる（自作カーネルの段階 4a）。Linux でも同じ結果になる
#include "../crt.h"

static void sleep_ms(long ms) {
    struct { long sec, nsec; } t = {ms / 1000, ms % 1000 * 1000000};
    syscall3(SYS_nanosleep, &t, 0, 0);
}

int main(int argc, char **argv, char **envp) {
    (void)argc, (void)argv;
    print("forktest: pid "); print_u(syscall3(SYS_getpid, 0, 0, 0)); print("\n");
    // 子を 3 つ作る。子は少し眠ってから、それぞれの終了コードで終わる
    long pids[3];
    for (int i = 0; i < 3; i++) {
        long pid = syscall3(SYS_fork, 0, 0, 0);
        if (pid == 0) {
            sleep_ms(30 * (3 - i));  // 後に作った子ほど先に終わる
            return 10 + i;
        }
        pids[i] = pid;
    }
    // 作った順に待つ（終わった順とは逆）
    for (int i = 0; i < 3; i++) {
        int status = 0;
        long r = syscall6(SYS_wait4, pids[i], (long)&status, 0, 0, 0, 0);
        print("child "); print_u(i); print(": wait4 "); print(r == pids[i] ? "ok" : "wrong pid");
        print(", exit "); print_u((status >> 8) & 0xff); print("\n");
    }
    // 子で別のプログラムを exec して、終わるのを待つ
    long pid = syscall3(SYS_fork, 0, 0, 0);
    if (pid == 0) {
        char *args[] = {"hello", "from", "exec", 0};
        syscall3(SYS_execve, "/bin/hello", args, envp);
        return 127;
    }
    int status = 0;
    syscall6(SYS_wait4, pid, (long)&status, 0, 0, 0, 0);
    print("exec child exit "); print_u((status >> 8) & 0xff); print("\n");
    // 子がいないのに待つと ECHILD（10）
    print("wait4 with no children: "); print_i(syscall6(SYS_wait4, -1, (long)&status, 0, 0, 0, 0)); print("\n");
    print("forktest: done\n");
    return 0;
}
