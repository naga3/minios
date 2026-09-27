// OS の中でコンパイルするお手本: cc -o /data/bin/hello2 /usr/share/minios/examples/hello.c
#include "crt.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    print("hello, compiled inside minios");
    for (int i = 1; i < argc; i++) { print(" "); print(argv[i]); }
    print("\n");
    return 0;
}
