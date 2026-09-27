// cc [tcc の引数...]: minios のプログラムをコンパイルする。
// libc は無いので -nostdlib -static で、minios の API（crt.h、lib.h、net.h）を /usr/include/minios から読む
#include "../crt.h"

int main(int argc, char **argv, char **envp) {
    char *args[64];
    int n = 0;
    args[n++] = "tcc";
    args[n++] = "-nostdlib";
    args[n++] = "-static";
    args[n++] = "-I/usr/include/minios";
    for (int i = 1; i < argc && n < 63; i++) args[n++] = argv[i];
    args[n] = 0;
    long e = syscall3(SYS_execve, "/bin/tcc", args, envp);
    print_err("/bin/tcc", e);
    return 127;
}
