// ファイル操作を一通り確かめる（自作カーネルの段階 4b）。Linux でも同じ結果になる
#include "../crt.h"

static void show(const char *what, long v) {
    print(what); print(": "); print_i(v); print("\n");
}

static void list_entry(const char *name, unsigned char type, void *ctx) {
    (void)type;
    // 並び順はファイルシステム次第なので、名前を集めてから並べて出す
    char (*names)[32] = ctx;
    if (name[0] == '.') return;
    for (int i = 0; i < 16; i++)
        if (!names[i][0]) { str_copy(names[i], 32, name); return; }
}

static void list(const char *dir) {
    static char names[16][32];
    for (int i = 0; i < 16; i++) names[i][0] = 0;
    long e = each_dirent(dir, list_entry, names);
    if (e < 0) { show("list error", e); return; }
    for (int i = 0; i < 16 && names[i][0]; i++)  // 挿入ソート
        for (int j = i; j > 0 && !(slen(names[j - 1]) && seq(names[j - 1], names[j])); j--) {
            int k = 0;
            while (names[j - 1][k] && names[j - 1][k] == names[j][k]) k++;
            if ((unsigned char)names[j - 1][k] <= (unsigned char)names[j][k]) break;
            char t[32];
            str_copy(t, 32, names[j]); str_copy(names[j], 32, names[j - 1]); str_copy(names[j - 1], 32, t);
        }
    print(dir); print(":");
    for (int i = 0; i < 16 && names[i][0]; i++) { print(" "); print(names[i]); }
    print("\n");
}

int main(int argc, char **argv, char **envp) {
    (void)argc, (void)argv, (void)envp;
    char buf[256];
    struct stat st;
    show("mkdir /t", syscall3(SYS_mkdir, "/t", 0755, 0));
    show("mkdir /t again", syscall3(SYS_mkdir, "/t", 0755, 0));
    show("chdir /t", syscall3(SYS_chdir, "/t", 0, 0));
    long n = syscall3(SYS_getcwd, buf, sizeof buf, 0);
    print("getcwd: "); print(n > 0 ? buf : "?"); print("\n");
    show("write a.txt", write_file("a.txt", "hello\n", 6));
    long fd = syscall3(SYS_open, "a.txt", O_WRONLY | O_APPEND, 0);
    write_all(fd, "world\n", 6);
    syscall3(SYS_close, fd, 0, 0);
    n = read_file("/t/a.txt", buf, sizeof buf);
    print("read a.txt: "); print(buf);
    show("stat size", syscall3(SYS_stat, "a.txt", &st, 0) == 0 ? st.st_size : -1);
    show("stat is regular", (st.st_mode & S_IFMT) == 0100000);
    show("chmod", syscall3(SYS_chmod, "a.txt", 0600, 0));
    syscall3(SYS_stat, "a.txt", &st, 0);
    show("mode", st.st_mode & 0777);
    // lseek で途中から読む
    fd = syscall3(SYS_open, "a.txt", O_RDONLY, 0);
    show("lseek", syscall3(8 /* SYS_lseek */, fd, 6, 0));
    n = syscall3(SYS_read, fd, buf, 5);
    buf[n] = 0;
    print("read after lseek: "); print(buf); print("\n");
    show("read at end", (syscall3(SYS_read, fd, buf, 100), syscall3(SYS_read, fd, buf, 100)));
    syscall3(SYS_close, fd, 0, 0);
    // dup2 で標準出力をファイルに向けて戻す
    fd = syscall3(SYS_open, "out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    long saved = syscall3(SYS_dup, 1, 0, 0);
    syscall3(SYS_dup2, fd, 1, 0);
    print("redirected\n");
    syscall3(SYS_dup2, saved, 1, 0);
    syscall3(SYS_close, saved, 0, 0);
    syscall3(SYS_close, fd, 0, 0);
    read_file("out.txt", buf, sizeof buf);
    print("out.txt: "); print(buf);
    show("mkdir sub", syscall3(SYS_mkdir, "sub", 0755, 0));
    list("/t");
    show("open missing", syscall3(SYS_open, "/nosuch", O_RDONLY, 0));
    show("open dir as file for writing", syscall3(SYS_open, "/t", O_WRONLY, 0));
    show("rmdir non-empty", syscall3(SYS_rmdir, "/t", 0, 0));
    show("unlink a.txt", syscall3(SYS_unlink, "a.txt", 0, 0));
    show("unlink out.txt", syscall3(SYS_unlink, "out.txt", 0, 0));
    show("unlink dir", syscall3(SYS_unlink, "sub", 0, 0));
    show("rmdir sub", syscall3(SYS_rmdir, "sub", 0, 0));
    list("/t");
    show("chdir ..", syscall3(SYS_chdir, "..", 0, 0));
    syscall3(SYS_getcwd, buf, sizeof buf, 0);
    print("getcwd: "); print(buf); print("\n");
    show("rmdir /t", syscall3(SYS_rmdir, "/t", 0, 0));
    // initramfs のファイルとシンボリックリンク
    show("stat /bin/hello is regular", syscall3(SYS_stat, "/bin/hello", &st, 0) == 0 && (st.st_mode & S_IFMT) == 0100000);
    show("lstat /bin/fetch is a link", syscall3(SYS_lstat, "/bin/fetch", &st, 0) == 0 && (st.st_mode & S_IFMT) == S_IFLNK);
    show("stat /bin/fetch follows the link", syscall3(SYS_stat, "/bin/fetch", &st, 0) == 0 && (st.st_mode & S_IFMT) == 0100000);
    show("stat /dev/console is a char device", syscall3(SYS_stat, "/dev/console", &st, 0) == 0 && (st.st_mode & S_IFMT) == S_IFCHR);
    print("fstest: done\n");
    return 0;
}
