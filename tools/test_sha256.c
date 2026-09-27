// sha256.h をホストの sha256sum と比べる（パディングの境目の長さを中心に）。test.py から実行する
//   gcc -o build/test_sha256 tools/test_sha256.c && build/test_sha256 <file>...
#include "../crt.h"
#include "../sha256.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        char hex[65];
        long e = sha256_file(argv[i], hex);
        if (e < 0) { print_err(argv[i], e); return 1; }
        print(hex); print("  "); print(argv[i]); print("\n");
    }
    return 0;
}
