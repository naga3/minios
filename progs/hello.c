// 引数と自分の PID・親の PID・環境変数の数を出す
#include "../crt.h"

int main(int argc, char **argv, char **envp) {
    print("hello from pid "); print_u(syscall3(SYS_getpid, 0, 0, 0));
    print(" (parent "); print_u(syscall3(SYS_getppid, 0, 0, 0)); print(")");
    for (int i = 1; i < argc; i++) { print(" "); print(argv[i]); }
    int envc = 0;
    while (envp[envc]) envc++;
    print(" env="); print_u(envc); print("\n");
    return 0;
}
