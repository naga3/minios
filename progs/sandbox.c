// sandbox <システムコール,...> <プログラム> [引数...]: 決まったシステムコールだけを許して（seccomp）プログラムを実行する。
// 許していないシステムコールを呼ぶと、カーネルがプロセスを止める（SIGSYS）。grow のテストと、シェルが /data/bin を実行するときに使う
#include "../crt.h"
#include "../seccomp.h"

int main(int argc, char **argv, char **envp) {
    if (argc < 3) {
        print_err("usage: sandbox <syscall,...> <program> [args...]", -22);
        return 2;
    }
    const char *bad;
    size_t bad_len;
    long e = apply_syscall_filter(argv[1], &bad, &bad_len);
    if (e == -22) {
        write_lit(2, "sandbox: unknown system call: ");
        write_all(2, bad, bad_len);
        write_lit(2, "\n");
        return 2;
    }
    if (e < 0) {
        print_err("sandbox: seccomp", e);
        return 1;
    }
    e = syscall3(SYS_execve, argv[2], argv + 2, envp);
    // ここに来るのは execve が失敗したとき。write や exit_group が許されていなければ、このエラーは出せずに止められる
    print_err(argv[2], e);
    return 127;
}
