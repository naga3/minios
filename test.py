#!/usr/bin/env python3
"""minios を QEMU で起動し、シリアル越しに操作して挙動を確かめる。

使い方: ./test.py            全テスト
        ./test.py boot       名前に boot を含むテストだけ
"""
import os
import re
import select
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


class Timeout(Exception):
    pass


class VM:
    def __init__(self, append="console=ttyS0 loglevel=0", extra=()):
        self.p = subprocess.Popen(
            ["qemu-system-x86_64", "-m", "64M",
             "-kernel", f"{ROOT}/build/bzImage",
             "-initrd", f"{ROOT}/build/initramfs.cpio",
             "-append", append,
             "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot",
             *extra],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.buf = b""   # まだ expect で消費していない出力
        self.log = b""   # 全出力

    def _read(self, timeout):
        r, _, _ = select.select([self.p.stdout], [], [], timeout)
        if not r:
            return False
        chunk = os.read(self.p.stdout.fileno(), 4096)
        if not chunk:
            raise EOFError
        self.buf += chunk
        self.log += chunk
        return True

    def expect(self, pattern, timeout=15):
        """pattern（正規表現、ANSI エスケープ除去後）が出るまで待ち、それまでの出力を返す"""
        rx = re.compile(pattern.encode() if isinstance(pattern, str) else pattern)
        deadline = time.monotonic() + timeout
        while True:
            clean = ANSI.sub(b"", self.buf)
            m = rx.search(clean)
            if m:
                self.buf = clean[m.end():]
                return clean[:m.end()].decode(errors="replace")
            left = deadline - time.monotonic()
            if left <= 0:
                raise Timeout(f"expect {pattern!r} timed out")
            try:
                self._read(left)
            except EOFError:
                raise Timeout(f"expect {pattern!r}: VM exited")

    def send(self, s):
        self.p.stdin.write(s.encode())
        self.p.stdin.flush()

    def wait_exit(self, timeout=15):
        deadline = time.monotonic() + timeout
        while self.p.poll() is None and time.monotonic() < deadline:
            try:
                self._read(0.1)
            except EOFError:
                break
        return self.p.wait(max(0.1, deadline - time.monotonic()))

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()


TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


# ---- テスト ----

PROMPT = r"minios# "


def booted():
    """起動してプロンプトが出た VM を返す"""
    vm = VM()
    vm.expect(r"kernel: Linux \S+ x86_64")
    vm.expect(PROMPT)
    return vm


def cmd(vm, line, timeout=15):
    """1 行送り、次のプロンプトまでの出力（エコーした行を除く）を返す"""
    vm.send(line + "\n")
    out = vm.expect(PROMPT, timeout)
    out = out.replace("\r\n", "\n")
    lines = out.split("\n")
    return "\n".join(lines[1:-1])  # 先頭はエコー、末尾はプロンプト


@test
def boot_and_shutdown():
    vm = booted()
    try:
        vm.send("shutdown\n")
        vm.expect(r"Bye\.")
        assert vm.wait_exit() == 0
    finally:
        vm.kill()


@test
def basic_commands():
    vm = booted()
    try:
        out = cmd(vm, "help")
        for name in ("help", "uname", "uptime", "mem", "echo", "clear", "shutdown"):
            assert name in out, (name, out)
        assert cmd(vm, "uname").startswith("Linux 7.2.8 "), "uname"
        assert re.fullmatch(r"up \d+s", cmd(vm, "uptime")), "uptime"
        out = cmd(vm, "mem")
        assert re.search(r"total +\d+ KiB", out) and re.search(r"free +\d+ KiB", out), out
        assert cmd(vm, "echo  hello   world ") == "hello world", "echo"
        assert cmd(vm, "") == "", "empty line"
        assert cmd(vm, "nosuch x") == "nosuch: command not found"
    finally:
        vm.kill()


@test
def proc_and_sys():
    vm = booted()
    try:
        out = cmd(vm, "mounts")
        assert re.search(r"^proc /proc proc ", out, re.M), out
        assert re.search(r"^sysfs /sys sysfs ", out, re.M), out
        out = cmd(vm, "ps")
        assert re.search(r"^ +1  [RS]  init$", out, re.M), out
        assert re.search(r"^ +2  S  kthreadd$", out, re.M), out
        out = cmd(vm, "dmesg")
        assert "Linux version 7.2.8" in out, out[:200]
        assert "Run /init as init process" in out, out[-500:]
    finally:
        vm.kill()


@test
def files_and_redirect():
    vm = booted()
    try:
        assert cmd(vm, "pwd") == "/"
        assert cmd(vm, "mkdir /tmp") == ""
        assert cmd(vm, "cd /tmp") == ""
        assert cmd(vm, "pwd") == "/tmp"
        assert cmd(vm, "echo hello > a.txt") == ""
        assert cmd(vm, "echo world >> a.txt") == ""
        assert cmd(vm, "cat a.txt") == "hello\nworld"
        assert cmd(vm, "uname >a.txt") == ""        # 空白なしでも、どのコマンドでもリダイレクトできる
        assert cmd(vm, "cat a.txt").startswith("Linux 7.2.8 ")
        assert re.fullmatch(r"- +\d+  a\.txt", cmd(vm, "ls")), cmd(vm, "ls")
        assert cmd(vm, "mkdir sub") == ""
        assert re.search(r"^d +\d+  sub/$", cmd(vm, "ls /tmp"), re.M)
        assert cmd(vm, "cat nosuch") == "nosuch: no such file or directory"
        assert cmd(vm, "echo x > /nosuchdir/f") == "/nosuchdir/f: no such file or directory"
        assert cmd(vm, "rm a.txt sub") == ""
        assert cmd(vm, "ls") == ""
        assert cmd(vm, "cd /") == ""
        assert cmd(vm, "rm /tmp") == ""
        assert "init" in cmd(vm, "ls /")
        assert re.search(r"^c +0  console$", cmd(vm, "ls /dev"), re.M)
    finally:
        vm.kill()


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    failed = 0
    for fn in TESTS:
        if only not in fn.__name__:
            continue
        t0 = time.monotonic()
        try:
            fn()
            print(f"PASS {fn.__name__} ({time.monotonic() - t0:.1f}s)")
        except Exception as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {type(e).__name__}: {e}")
    sizes = {f: os.path.getsize(f"{ROOT}/build/{f}") for f in ("bzImage", "init", "initramfs.cpio")}
    # ファイルは 4KB 単位で並ぶので、中身の大きさ（text + data）も出す
    text, data = subprocess.run(["size", f"{ROOT}/build/init"], capture_output=True, text=True).stdout.split("\n")[1].split()[:2]
    sizes["init_code"] = int(text) + int(data)
    print("SIZE " + " ".join(f"{k}={v}" for k, v in sizes.items()))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
