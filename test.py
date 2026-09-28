#!/usr/bin/env python3
"""minios を QEMU で起動し、シリアル越しに操作して挙動を確かめる。

使い方: ./test.py            全テスト
        ./test.py boot       名前に boot を含むテストだけ
"""
import hashlib
import os
import re
import select
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
# KVM が使えれば使う（ローカル LLM を動かすには必須）。使えなければソフトウェアエミュレーション
ACCEL = ("-accel", "kvm", "-cpu", "host") if os.access("/dev/kvm", os.R_OK | os.W_OK) else ("-accel", "tcg")
ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


class Timeout(Exception):
    pass


class VM:
    def __init__(self, append="console=ttyS0 loglevel=0", disk=None, net=False, efi=False, usb_kbd=False,
                 esp=None, esp_on_usb=False, nic_model="virtio-net-pci", models=False, extra=()):
        if models:
            # ローカル LLM: モデル用のディスク（読み取り専用）と、推論のためのメモリと CPU
            extra = ("-drive", f"file={ROOT}/build/models.img,format=raw,if=virtio,readonly=on",
                     "-m", "8G", "-smp", "8", *extra)
        if disk:
            extra = ("-drive", f"file={disk},format=raw,if=virtio", *extra)
        extra = ("-nic", f"user,model={nic_model}" if net else "none", *extra)
        if usb_kbd:
            extra = ("-device", "qemu-xhci", "-device", "usb-kbd", *extra)
        # QEMU のモニタ（キー入力の送信、スクリーンショット）を UNIX ソケットで開けておく
        import tempfile
        self.mon_dir = tempfile.TemporaryDirectory()
        self.mon_path = os.path.join(self.mon_dir.name, "mon.sock")
        if efi:
            # 実機と同じ経路: UEFI ファームウェア → FAT ディスク上の BOOTX64.EFI。
            # -no-reboot を付けない。ACPI の電源断が効かなければ再起動を繰り返して終わらないので、それで確かめられる
            # ESP（FAT に見せるディレクトリ）は書き込まれるので、指定が無ければ一時ディレクトリにコピーして渡す。
            # IDE ディスクは読み取り専用にできない。esp_on_usb なら USB メモリとしてつなぐ（実機の USB 起動と同じ）
            import shutil, tempfile
            self.tmp = tempfile.TemporaryDirectory()
            if esp is None:
                esp = os.path.join(self.tmp.name, "esp")
                shutil.copytree(f"{ROOT}/build/efi", esp)
            vars_fd = os.path.join(self.tmp.name, "vars.fd")
            shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", vars_fd)
            if esp_on_usb:
                esp_drive = ("-device", "qemu-xhci", "-drive", f"if=none,id=stick,format=raw,file=fat:rw:{esp}",
                             "-device", "usb-storage,drive=stick")
            else:
                esp_drive = ("-drive", f"file=fat:rw:{esp},format=raw")
            boot = ("-m", "256M",
                    "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                    "-drive", f"if=pflash,format=raw,file={vars_fd}",
                    *esp_drive)
        else:
            boot = ("-m", "64M", "-kernel", f"{ROOT}/build/bzImage", "-initrd", f"{ROOT}/build/initramfs.cpio",
                    "-append", append, "-no-reboot")
        self.p = subprocess.Popen(
            ["qemu-system-x86_64", *ACCEL, *boot, "-display", "none", "-serial", "stdio",
             "-monitor", f"unix:{self.mon_path},server,nowait", *extra],
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
                raise Timeout(f"expect {pattern!r} timed out; last output: {clean[-300:]!r}")
            try:
                self._read(left)
            except EOFError:
                raise Timeout(f"expect {pattern!r}: VM exited; last output: {clean[-300:]!r}")

    def monitor(self, command):
        """QEMU のモニタ（HMP）にコマンドを送る"""
        import socket
        for _ in range(50):
            if os.path.exists(self.mon_path):
                break
            time.sleep(0.1)
        with socket.socket(socket.AF_UNIX) as sock:
            sock.connect(self.mon_path)
            sock.settimeout(5)
            out = b""
            while not out.endswith(b"(qemu) "):
                out += sock.recv(4096)
            sock.sendall(command.encode() + b"\n")
            out = b""
            while not out.endswith(b"(qemu) "):
                out += sock.recv(4096)
            return out.decode(errors="replace")

    def type_keys(self, text):
        """キーボードから打ったことにする（QEMU の sendkey）。英小文字・数字・空白・改行・一部の記号だけ"""
        names = {" ": "spc", "\n": "ret", ".": "dot", "-": "minus", "/": "slash", "=": "equal"}
        for ch in text:
            self.monitor(f"sendkey {names.get(ch, ch)}")

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


class ScratchVM(VM):
    """自作カーネル（kernel/）で起動する VM。QEMU のデバッグ用の終了装置で終わる（終了コードは (code << 1) | 1）。
    initramfs は Linux 版と同じ build/initramfs.cpio を Multiboot のモジュールとして渡す"""
    def __init__(self, append="", extra=()):
        if append:
            extra = ("-append", append, *extra)
        import tempfile
        self.mon_dir = tempfile.TemporaryDirectory()
        self.mon_path = os.path.join(self.mon_dir.name, "mon.sock")
        self.p = subprocess.Popen(
            ["qemu-system-x86_64", *ACCEL, "-kernel", f"{ROOT}/build/scratch/kernel.elf", "-m", "256M",
             "-initrd", f"{ROOT}/build/initramfs.cpio",
             "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-no-reboot",
             "-display", "none", "-serial", "stdio", "-monitor", f"unix:{self.mon_path},server,nowait", *extra],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.buf = b""
        self.log = b""


TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


# ---- テスト ----

# 入力を待っているとき、最後に出ているのは必ずプロンプト。出力の最後が "minios# " で終わっていればプロンプトとみなす。
# 行頭で判定すると、改行せずに終わるプログラムの後のプロンプトを見逃す。途中で判定すると、画面の中身（screen の出力）と取り違える
PROMPT = r"minios# \Z"


def booted(disk=None, net=False, models=False):
    """起動してプロンプトが出た VM を返す"""
    vm = VM(disk=disk, net=net, models=models)
    vm.expect(r"kernel: Linux \S+ x86_64")
    vm.expect(PROMPT)
    return vm


def cmd(vm, line, timeout=15):
    """1 行送り、次のプロンプトまでの出力（エコーした行を除く）を返す"""
    vm.send(line + "\n")
    out = vm.expect(PROMPT, timeout)
    out = out.replace("\r\n", "\n")
    out = out[:-len("minios# ")]          # 末尾のプロンプト
    lines = out.split("\n")[1:]          # 先頭はエコーした行
    if lines and lines[-1] == "":         # 改行で終わっていれば、その後ろの空の行
        lines.pop()
    return "\n".join(lines)


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
        import datetime
        guest = datetime.datetime.strptime(cmd(vm, "date"), "%Y-%m-%d %H:%M:%S UTC").replace(tzinfo=datetime.timezone.utc)
        assert abs((guest - datetime.datetime.now(datetime.timezone.utc)).total_seconds()) < 10, guest
        out = cmd(vm, "mem")
        assert re.search(r"total +\d+ KiB", out) and re.search(r"free +\d+ KiB", out), out
        assert cmd(vm, "echo  hello   world ") == "hello world", "echo"
        assert cmd(vm, "") == "", "empty line"
        assert cmd(vm, "nosuch x") == "nosuch: command not found"
        long_line = "echo " + "x" * 3000                      # 256 バイトを超える行も 1 行として読む（周 30）
        assert cmd(vm, long_line) == "x" * 3000
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
        assert re.search(r"^ +\d+  [RS]  sh$", out, re.M), out  # 端末ごとのシェル
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
        assert cmd(vm, "cat < a.txt") == cmd(vm, "cat a.txt")           # 入力のリダイレクト（組み込み）
        assert cmd(vm, "grep Linux < a.txt").startswith("Linux 7.2.8 ")  # 入力のリダイレクト（外部プログラム）
        assert cmd(vm, "grep Linux < a.txt > b.txt") == ""               # 入力と出力を両方
        assert cmd(vm, "cat b.txt").startswith("Linux 7.2.8 ")
        assert cmd(vm, "cat < nosuch") == "nosuch: no such file or directory"
        assert cmd(vm, "rm b.txt") == ""
        assert cmd(vm, "rm a.txt sub") == ""
        assert cmd(vm, "ls") == ""
        assert cmd(vm, "cd /") == ""
        assert cmd(vm, "rm /tmp") == ""
        assert "init" in cmd(vm, "ls /")
        assert re.search(r"^c +0  console$", cmd(vm, "ls /dev"), re.M)
        out = cmd(vm, "ls /licenses")  # 同梱した部品のライセンス（NOTICE.md）
        for name in ("NOTICE.md", "Linux-GPL-2.0", "Go-BSD-3-Clause", "ca-certificates-MPL-2.0", "TinyCC-LGPL-2.1"):
            assert name in out, out
    finally:
        vm.kill()


def new_disk():
    """空の minix ディスクイメージを一時ファイルに作ってパスを返す"""
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".img")
    os.close(fd)
    os.truncate(path, 8 << 20)
    subprocess.run(["mkfs.minix", "-3", path], check=True, capture_output=True)
    return path


@test
def no_disk():
    vm = VM()
    try:
        vm.expect(r"No disk: /data is not persistent")
        vm.expect(PROMPT)
        assert not re.search(r"^/data ", cmd(vm, "df"), re.M)
    finally:
        vm.kill()


@test
def disk_persists_across_reboot():
    disk = new_disk()
    try:
        vm = VM(disk=disk)
        try:
            vm.expect(r"\[ OK \] Mounted /dev\s")
            vm.expect(r"\[ OK \] Mounted /data \(/dev/vda\)")
            vm.expect(PROMPT)
            assert re.search(r"^/data +minix +\d+ +\d+$", cmd(vm, "df"), re.M)
            assert cmd(vm, "echo persistent > /data/note") == ""
            assert cmd(vm, "mkdir /data/dir") == ""
            vm.send("shutdown\n")
            vm.expect(r"Bye\.")
            assert vm.wait_exit() == 0
        finally:
            vm.kill()
        vm = booted(disk)
        try:
            assert cmd(vm, "cat /data/note") == "persistent"
            assert re.search(r"^d +\d+  dir/$", cmd(vm, "ls /data"), re.M)
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def sync_survives_power_loss():
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            cmd(vm, "echo hello > /data/f")
            cmd(vm, "sync")
        finally:
            vm.kill()  # sync の後に電源断
        vm = booted(disk)
        try:
            assert cmd(vm, "cat /data/f") == "hello"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def network():
    vm = VM(net=True)
    try:
        vm.expect(r"\[ OK \] Loopback lo 127\.0\.0\.1")
        vm.expect(r"\[ OK \] Network eth0 10\.0\.2\.15 via 10\.0\.2\.2 \(DHCP\)")
        vm.expect(PROMPT)
        out = cmd(vm, "ifconfig")
        assert re.search(r"^eth0: UP inet 10\.0\.2\.15 mask 255\.255\.255\.0 ether 52:54:00:12:34:56$", out, re.M), out
        assert re.search(r"^lo: UP inet 127\.0\.0\.1 mask 255\.0\.0\.0$", out, re.M), out
        assert "sit0" not in out, out  # IPv6 を切ったのでトンネルも無い
        out = cmd(vm, "ping 10.0.2.2 2")
        assert re.search(r"^reply from 10\.0\.2\.2 seq=2 time=\d+\.\d ms$", out, re.M), out
        assert out.endswith("2 sent, 2 received"), out
        assert cmd(vm, "ping 127.0.0.1 1").endswith("1 sent, 1 received")
        out = cmd(vm, "ping 10.0.2.99 1", timeout=5)
        assert out == "timeout seq=1\n1 sent, 0 received", out
        assert cmd(vm, "ping 1.2.3") == "usage: ping <a.b.c.d> [count]: invalid argument"
    finally:
        vm.kill()


@test
def no_network():
    vm = VM()
    try:
        vm.expect(r"No network interface")
        vm.expect(PROMPT)
        assert cmd(vm, "ifconfig") == "lo: UP inet 127.0.0.1 mask 255.0.0.0"
    finally:
        vm.kill()


@test
def external_programs():
    vm = booted()
    try:
        many = " ".join(f"w{i}" for i in range(100))            # 引数は 31 個を超えても渡る（周 30）
        assert cmd(vm, "hello " + many).endswith(many + " env=3")
        out = cmd(vm, "hello a b")
        assert re.fullmatch(r"hello from pid \d+ \(parent \d+\) a b env=3", out), out
        assert cmd(vm, "/bin/hello") .startswith("hello from pid ")
        assert cmd(vm, "false") == "[exit 1]"
        assert cmd(vm, "nosuch") == "nosuch: command not found"
        assert cmd(vm, "/nosuch/x") == "/nosuch/x: command not found"
        assert cmd(vm, "hello x > /out") == ""  # 子プロセスにもリダイレクトが引き継がれる
        assert cmd(vm, "cat /out").endswith(" x env=3")
        assert cmd(vm, "cat /bin/hello > /h2") == ""
        assert cmd(vm, "/h2") == "/h2: permission denied\n[exit 127]"
        assert cmd(vm, "chmod 755 /h2") == ""
        assert cmd(vm, "/h2 copied").endswith(" copied env=3")
        assert cmd(vm, "chmod 9 /h2") == "usage: chmod <octal-mode> <path>...: invalid argument"
    finally:
        vm.kill()


@test
def install_program_on_disk():
    """OS をビルドし直さずに、/data/bin に置いたプログラムで機能が増え、再起動後も残る"""
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            assert cmd(vm, "hi") == "hi: command not found"
            assert cmd(vm, "mkdir /data/bin") == ""
            assert cmd(vm, "cat /bin/hello > /data/bin/hi") == ""
            assert cmd(vm, "chmod 755 /data/bin/hi") == ""
            assert cmd(vm, "trust hi").startswith("trusted /data/bin/hi sha256 ")
            assert cmd(vm, "hi there").endswith(" there env=3")
            vm.send("shutdown\n")
            assert vm.wait_exit() == 0
        finally:
            vm.kill()
        vm = booted(disk)
        try:
            assert cmd(vm, "hi again").endswith(" again env=3")
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def init_supervises_shells():
    """init（PID 1）は端末ごとにシェルを起動し、終わったら起動し直す。孤児も回収する"""
    vm = VM()
    try:
        vm.expect(r"\[ OK \] Shell on ttyS0")
        vm.expect(PROMPT)
        sh1 = re.search(r"^ +(\d+)  [RS]  sh$", cmd(vm, "ps"), re.M).group(1)
        vm.send("exit\n")
        vm.expect(PROMPT)  # init が新しいシェルを起動する
        sh2 = re.search(r"^ +(\d+)  [RS]  sh$", cmd(vm, "ps"), re.M).group(1)
        assert sh1 != sh2, (sh1, sh2)
        vm.send("\x04")    # Ctrl+D でも終わって起動し直す
        vm.expect(PROMPT)
        child = re.fullmatch(r"left child (\d+)", cmd(vm, "orphan")).group(1)
        time.sleep(1)       # 孤児が終わるのを待つ
        out = cmd(vm, "ps")
        assert not re.search(rf"^ +{child} ", out, re.M), out  # 回収されてゾンビ（Z）が残っていない
    finally:
        vm.kill()


@test
def trust_policy():
    """/data/bin のプログラムは、記録があり、ハッシュが一致するものだけ実行する"""
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            cmd(vm, "mkdir /data/bin")
            cmd(vm, "cat /bin/hello > /data/bin/hi")
            cmd(vm, "chmod 755 /data/bin/hi")
            refused = "hi: not trusted (no record; install it with grow, or mark it with trust)"
            assert cmd(vm, "hi x") == refused                  # 名前で呼んでも
            assert cmd(vm, "hi x | cat") == refused            # パイプの中でも
            assert cmd(vm, "/data/bin/hi x") == refused        # フルパスでも
            out = cmd(vm, "trust hi")
            h = re.fullmatch(r"trusted /data/bin/hi sha256 ([0-9a-f]{64})", out).group(1)
            assert h == hashlib.sha256(open(f"{ROOT}/build/bin/hello", "rb").read()).hexdigest()  # ホストの計算と一致
            assert cmd(vm, "hi x").endswith(" x env=3")
            assert cmd(vm, "hi x | cat").endswith(" x env=3")
            rec = cmd(vm, "cat /data/bin/.grow/hi")
            assert re.fullmatch(rf"name hi\ntrusted-by human\nbinary-sha256 {h}\ninstalled \d{{4}}-\d\d-\d\dT\d\d:\d\d:\d\dZ", rec), rec
            cmd(vm, "cat /bin/false > /data/bin/hi")          # 記録の後に書き換える
            assert cmd(vm, "hi x") == "hi: not trusted (changed since it was installed; the hash does not match the record)"
            assert cmd(vm, "hello x").endswith(" x env=3")     # /bin（OS のイメージ）は調べない
            assert cmd(vm, "trust /x") == "trust: give a name in /data/bin, not a path: invalid argument"
            assert cmd(vm, "trust nosuch") == "/data/bin/nosuch: no such file or directory"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def seccomp_sandbox():
    """許したシステムコールだけで動かす。sandbox コマンドと、記録の syscalls による実行時の強制"""
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            killed = "[signal 31: used a system call that is not allowed]"
            out = cmd(vm, "sandbox write,exit_group /bin/hello a")
            assert out == "hello from pid " + killed, out                         # getpid で止められる
            assert cmd(vm, "sandbox write,exit_group,getpid,getppid /bin/hello a").endswith(" a env=3")
            assert cmd(vm, "sandbox write,nosuch /bin/hello") == "sandbox: unknown system call: nosuch\n[exit 2]"
            # 記録に syscalls があれば、シェルはその分だけを許して実行する
            cmd(vm, "mkdir /data/bin")
            cmd(vm, "cat /bin/hello > /data/bin/hi")
            cmd(vm, "chmod 755 /data/bin/hi")
            cmd(vm, "trust hi")
            assert cmd(vm, "hi x").endswith(" x env=3")                          # 人が trust したものは制限しない
            cmd(vm, "echo syscalls write exit_group getpid getppid >> /data/bin/.grow/hi")
            assert cmd(vm, "hi x").endswith(" x env=3")
            assert cmd(vm, "hi x | cat").endswith(" x env=3")
            cmd(vm, "trust hi")
            cmd(vm, "echo syscalls write exit_group >> /data/bin/.grow/hi")
            assert cmd(vm, "hi x") == "hello from pid " + killed
            cmd(vm, "trust hi")
            cmd(vm, "echo syscalls write bogus >> /data/bin/.grow/hi")               # 知らない名前なら動かさない
            assert cmd(vm, "hi x") == "hi: cannot apply the system call restriction in its record\n[exit 126]"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def sha256_matches_host():
    """sha256.h の SHA-256 がホストの hashlib と一致する（パディングの境目の長さを中心に）"""
    import tempfile
    out = subprocess.run(["gcc", "-static", "-nostdlib", "-Os", "-ffreestanding", "-fno-stack-protector",
                          "-Wno-unused-function", "-o", f"{ROOT}/build/test_sha256", f"{ROOT}/tools/test_sha256.c"],
                         capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    with tempfile.TemporaryDirectory() as d:
        files = []
        for n in (0, 1, 55, 56, 57, 63, 64, 65, 119, 120, 128, 4097, 100000):
            path = os.path.join(d, f"f{n}")
            with open(path, "wb") as f:
                f.write(os.urandom(n))
            files.append(path)
        files.append(f"{ROOT}/build/bin/gobox")
        out = subprocess.run([f"{ROOT}/build/test_sha256", *files], capture_output=True, text=True, check=True).stdout
        for line, path in zip(out.splitlines(), files):
            assert line == f"{hashlib.sha256(open(path, 'rb').read()).hexdigest()}  {path}", (line, path)


@test
def pipes():
    vm = booted()
    try:
        assert cmd(vm, "echo hello world | cat") == "hello world"
        assert cmd(vm, "help | grep ping") == "  ping      ping <a.b.c.d> [count]"
        assert re.fullmatch(r" +1  S  init", cmd(vm, "ps | grep init")), cmd(vm, "ps | grep init")
        assert cmd(vm, "help | grep sh | grep shut") == "  shutdown  power off (exit QEMU)"  # 3 段
        assert cmd(vm, "help | grep nosuch") == "[exit 1]"                             # 最後の段の終わり方を出す
        assert cmd(vm, "hello a | grep hello > /p.txt") == ""                          # 最後の段のリダイレクト
        assert re.fullmatch(r"hello from pid \d+ \(parent \d+\) a env=3", cmd(vm, "cat /p.txt"))
        assert cmd(vm, "echo x | nosuch") == "nosuch: command not found\n[exit 127]"
        assert cmd(vm, "echo x | | cat") == "|: invalid argument"
        vm.send("spin | cat\n")                       # Ctrl+C は全段に届く
        vm.expect(r"spinning")
        vm.send("\x03")
        vm.expect(r"\[signal 2\]")
        vm.expect(PROMPT)
        assert cmd(vm, "echo alive") == "alive"
    finally:
        vm.kill()


@test
def ctrl_c_stops_program():
    vm = booted()
    try:
        vm.send("spin\n")
        vm.expect(r"spinning\.\.\. press Ctrl\+C")
        vm.send("\x03")
        vm.expect(r"\[signal 2\]")
        vm.expect(PROMPT)
        assert cmd(vm, "echo alive") == "alive"      # init は SIGINT で死なない
        vm.send("\x03")                             # プロンプトで Ctrl+C しても何も起きない
        assert cmd(vm, "echo still alive").endswith("still alive")
        vm.send("spin\n")
        vm.expect(r"spinning")
        vm.send("\x1a")                             # Ctrl+Z は無効（止まった子を待ち続けないように）
        vm.send("\x03")
        vm.expect(r"\[signal 2\]")
        vm.expect(PROMPT)
    finally:
        vm.kill()


@test
def go_program():
    """Go の静的バイナリが動く（カーネルに FUTEX・EPOLL・EVENTFD が要る）"""
    vm = booted()
    try:
        out = cmd(vm, "gohello a b", timeout=30)
        assert re.search(r"^hello from Go go\S+, pid \d+, args \[a b\]$", out, re.M), out
        assert "goroutines: sum of squares = 30" in out, out
        assert "sleep ok: true" in out, out
        assert out.endswith("file: written by Go"), out
    finally:
        vm.kill()


class HostHTTPServer:
    """ホストの 127.0.0.1 に HTTP サーバーを立てる。ゲストからは 10.0.2.2:<port> で見える。
    index.html と、配布用のプログラム（build/bin/hello を bin/hello として）を置く"""
    def __enter__(self):
        import http.server, tempfile, threading, functools, shutil
        self.dir = tempfile.TemporaryDirectory()
        with open(os.path.join(self.dir.name, "index.html"), "w") as f:
            f.write("hello from host\n")
        os.mkdir(os.path.join(self.dir.name, "bin"))
        shutil.copy(f"{ROOT}/build/bin/hello", os.path.join(self.dir.name, "bin", "hello"))
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *a):
                pass
        handler = functools.partial(Quiet, directory=self.dir.name)
        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), handler)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.dir.cleanup()


@test
def compile_inside():
    """OS の中の TinyCC（cc）で C をコンパイルし、/data/bin に置いて使う。再起動後も残る"""
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            assert cmd(vm, "mkdir /data/bin") == ""
            assert cmd(vm, "cc -o /data/bin/hello2 /usr/share/minios/examples/hello.c") == ""
            assert cmd(vm, "trust hello2").startswith("trusted /data/bin/hello2 sha256 ")
            assert cmd(vm, "hello2 a b") == "hello, compiled inside minios a b"
            out = cmd(vm, "cc -o /x /nosuch.c")
            assert out == "tcc: error: file '/nosuch.c' not found\n[exit 1]", out
            out = cmd(vm, "ls /usr/include/minios")
            assert all(h in out for h in ("crt.h", "lib.h", "net.h")), out
            vm.send("shutdown\n")
            assert vm.wait_exit() == 0
        finally:
            vm.kill()
        vm = booted(disk)
        try:
            assert cmd(vm, "hello2 again") == "hello, compiled inside minios again"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def local_llm():
    """OS の中でローカル LLM（llama.cpp + Qwen2.5-Coder-1.5B）が、全部の CPU を使って動く"""
    if not os.path.exists(f"{ROOT}/build/models.img"):
        print("  (skipped: run ./fetch-third-party.sh and ./build-models.sh first)")
        return
    vm = VM(models=True)
    try:
        vm.expect(r"\[ OK \] Mounted /models \(/dev/vda, read-only\)")
        vm.expect(PROMPT)
        assert cmd(vm, "cat /sys/devices/system/cpu/online") == "0-7"  # ACPI（MADT）で 8 個とも見える
        out = cmd(vm, "llama-completion -m /models/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf -p Say_hello "
                      "-st --simple-io --temp 0 -n 16 -t 8 --no-display-prompt", timeout=120)
        m = re.search(r"eval time = .* ([\d.]+) tokens per second\)", out)
        assert m, out[-1000:]
        speed = float(m.group(1))
        print(f"  ({speed:.0f} tokens/s)")
        assert speed > 5, out[-1000:]  # CPU が 1 つしか見えないと 0.05 程度になる（周 23 で踏んだ）
    finally:
        vm.kill()


@test
def grow_program():
    """grow の仕組み: 生成、検証、承認（n なら入らない）、記録、版、実行時の検査。速さのため 1.5B のモデルで確かめる。
    AI の答えの中身（どのテストが通るか）はモデル次第なので、ここでは仕組みだけを確かめる（7B は grow_7b）"""
    if not os.path.exists(f"{ROOT}/build/models.img"):
        print("  (skipped: run ./fetch-third-party.sh and ./build-models.sh first)")
        return
    disk = new_disk()
    try:
        vm = booted(disk, models=True)
        try:
            # 承認で n と答えるとインストールされない
            vm.send("grow -model 1.5b sum print the sum of the integer arguments\n")
            out = vm.expect(r"\[y/N\] \Z", timeout=600)
            assert "qwen2.5-coder-1.5b" in out, out
            assert re.search(r"tests: \d+/\d+ passed \(own \d+, independent [1-9]\d*\)", out), out
            assert "--- allowed system calls (enforced with seccomp): " in out, out
            vm.send("n\n")
            out = vm.expect(PROMPT)
            assert "not installed" in out, out
            assert cmd(vm, "sum 1 2 3") == "sum: command not found"
            # y と答えるとインストールされ、出どころが記録される（種を固定しているので、同じ依頼からは同じ結果になる）
            vm.send("grow -model 1.5b sum print the sum of the integer arguments\n")
            vm.expect(r"\[y/N\] \Z", timeout=600)
            vm.send("y\n")
            out = vm.expect(PROMPT)
            assert "installed /data/bin/sum version 1 (record: /data/bin/.grow/sum)" in out, out
            assert "not trusted" not in cmd(vm, "sum 1 2 3")   # 記録とハッシュが一致するので実行できる
            rec = cmd(vm, "cat /data/bin/.grow/sum")
            for key in ("name sum", "request print the sum of the integer arguments", "version 1",
                        "history print the sum of the integer arguments",
                        "model qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
                        "model-sha256 cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046",
                        "source /data/src/sum.c", "source-sha256 ", "binary-sha256 ", "attempts ",
                        "syscalls ", "test ", "installed "):
                assert key in rec, rec
            assert re.search(r"^(disagreed-)?independent-test ", rec, re.M), rec
            assert '#include "crt.h"' in cmd(vm, "cat /data/src/sum.c")
            assert cmd(vm, "grow -log sum") == "* 1  print the sum of the integer arguments"
            assert cmd(vm, "grow -undo sum") == "grow: sum is at version 1; there is nothing to go back to\n[exit 1]"
            assert cmd(vm, "grow cat x") == "grow: shell builtin cat already exists\n[exit 2]"  # 組み込みや既存の名前は使えない
            assert cmd(vm, "grow grep x") == "grow: /bin/grep already exists\n[exit 2]"
            assert cmd(vm, "grow -model nosuch x y") == "grow: no model matching \"nosuch\" in /models\n[exit 1]"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def grow_7b():
    """7B のモデル（既定）で作って育てる。数分かかるので MINIOS_SLOW=1 のときだけ"""
    if os.environ.get("MINIOS_SLOW") != "1":
        print("  (skipped: set MINIOS_SLOW=1)")
        return
    disk = new_disk()
    try:
        vm = booted(disk, models=True)
        try:
            out = cmd(vm, "grow -y count print how many arguments there are", timeout=900)
            assert "qwen2.5-coder-7b" in out and "installed /data/bin/count version 1" in out, out
            assert cmd(vm, "count a b c") == "3"
            assert cmd(vm, "count") == "0"
            out = cmd(vm, "grow -y rev print each argument reversed, separated by spaces", timeout=900)
            assert "installed /data/bin/rev version 1" in out, out
            assert cmd(vm, "rev abc hello") == "cba olleh"
            # もうそうなっている変更は、版を上げない
            out = cmd(vm, "grow -y rev keep the order of the arguments, only reverse the letters of each one", timeout=900)
            assert "rev already does that (version 1 passes the new tests); nothing to change" in out, out
            assert cmd(vm, "grow -log rev") == "* 1  print each argument reversed, separated by spaces"
            rec = cmd(vm, "cat /data/bin/.grow/rev")
            # 分けたモデルは全部のファイルを通したハッシュ
            both = hashlib.sha256()
            for part in ("00001", "00002"):
                with open(f"{ROOT}/third_party/models/qwen2.5-coder-7b-instruct-q4_k_m-{part}-of-00002.gguf", "rb") as f:
                    while chunk := f.read(1 << 24):
                        both.update(chunk)
            assert f"model-sha256 {both.hexdigest()}" in rec, rec
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def gobox_links():
    """Go のプログラムは gobox 1 本にまとめ、/bin の名前はシンボリックリンク"""
    vm = booted()
    try:
        out = cmd(vm, "ls /bin")
        assert re.search(r"^l +\d+  fetch$", out, re.M) and re.search(r"^l +\d+  gohello$", out, re.M), out
        assert cmd(vm, "gobox") == "usage: gobox <command> [args...]\ncommands: [fetch gohello grow]\n[exit 2]"
        assert cmd(vm, "gobox gohello x", timeout=30).startswith("hello from Go ")
        assert cmd(vm, "fetch") == "usage: fetch [-o path] <url>\n[exit 2]"
    finally:
        vm.kill()


@test
def http_from_go():
    with HostHTTPServer() as srv:
        vm = VM(net=True)
        try:
            vm.expect(r"\[ OK \] DNS 10\.0\.2\.3 \(/etc/resolv\.conf\)")
            vm.expect(PROMPT)
            assert cmd(vm, "cat /etc/resolv.conf") == "nameserver 10.0.2.3"
            out = cmd(vm, f"fetch http://10.0.2.2:{srv.port}/index.html", timeout=30)
            assert out.rstrip() == "HTTP/1.0 200 OK\nhello from host", out
            out = cmd(vm, f"fetch http://10.0.2.2:{srv.port}/nosuch", timeout=30)
            assert out.startswith("HTTP/1.0 404 "), out
        finally:
            vm.kill()


@test
def install_program_over_network():
    """ネットワーク越しにプログラムを取ってきて /data/bin に置けば、OS をビルドし直さずに育つ"""
    disk = new_disk()
    try:
        with HostHTTPServer() as srv:
            vm = booted(disk, net=True)
            try:
                base = f"http://10.0.2.2:{srv.port}"
                assert cmd(vm, "mkdir /data/bin") == ""
                out = cmd(vm, f"fetch -o /data/bin/hi {base}/bin/hello", timeout=30)
                size = os.path.getsize(f"{ROOT}/build/bin/hello")
                assert out == f"saved {size} bytes to /data/bin/hi", out
                assert cmd(vm, "chmod 755 /data/bin/hi") == ""
                assert cmd(vm, "trust hi").startswith("trusted /data/bin/hi sha256 ")
                assert cmd(vm, "hi from network").endswith(" from network env=3")
                # 失敗したときは何も残さない
                out = cmd(vm, f"fetch -o /data/bin/none {base}/bin/nosuch", timeout=30)
                assert out.startswith(f"fetch: {base}/bin/nosuch 404 ") and out.endswith("[exit 1]"), out
                assert "none" not in cmd(vm, "ls /data/bin")
                vm.send("shutdown\n")
                assert vm.wait_exit() == 0
            finally:
                vm.kill()
        vm = booted(disk)  # ネットワーク無しで再起動しても残っている
        try:
            assert cmd(vm, "hi again").endswith(" again env=3")
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


def efi_image_fresh():
    """UEFI 版のイメージが最新の initramfs から作られているか。古ければテストを飛ばす"""
    image = f"{ROOT}/build/efi/EFI/BOOT/BOOTX64.EFI"
    if not os.path.exists(image) or os.path.getmtime(image) < os.path.getmtime(f"{ROOT}/build/initramfs.cpio"):
        print("  (skipped: run ./build-efi.sh first)")
        return False
    return True


def ppm_to_png(src, dst):
    """QEMU の screendump（PPM）を PNG にする"""
    import struct, zlib
    data = open(src, "rb").read()
    magic, size, _maxval, pixels = data.split(b"\n", 3)
    assert magic == b"P6"
    w, h = map(int, size.split())
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


@test
def efi_screen_and_keyboard():
    """UEFI 版で、画面（efifb + fbcon）の tty1 にシェルが出て、USB キーボードで操作できる。
    画面は /dev/vcs1（tty1 の文字の中身）で確かめ、build/screen.png にスクリーンショットを残す"""
    if not efi_image_fresh():
        return
    vm = VM(efi=True, usb_kbd=True)
    try:
        vm.expect(r"\[ OK \] Shell on tty1", timeout=120)
        vm.expect(PROMPT)
        # 起動直後、QEMU はキーを PS/2 キーボードに送り、USB キーボードの準備ができると USB に切り替える。
        # 切り替わる瞬間のキーは失われるので、USB キーボードの入力デバイス（3 つ目）が現れてから少し待って打つ
        for _ in range(100):
            if "1-1" in cmd(vm, "ls /sys/bus/usb/devices") and cmd(vm, "ls /sys/class/input").count("input") >= 3:
                break
            time.sleep(0.2)
        else:
            raise AssertionError("USB keyboard not found")
        time.sleep(0.5)
        screen = cmd(vm, "screen")
        assert re.search(r"^\|  minios \d+\.\d+$", screen, re.M) and re.search(r"^\|minios#$", screen, re.M), screen
        vm.type_keys("echo typed on usb\n")
        for _ in range(50):
            screen = cmd(vm, "screen")
            if re.search(r"^\|minios# echo typed on usb\n\|typed on usb\n\|minios#$", screen, re.M):
                break
            time.sleep(0.2)
        else:
            raise AssertionError(screen)
        vm.monitor(f"screendump {ROOT}/build/screen.ppm")
        ppm_to_png(f"{ROOT}/build/screen.ppm", f"{ROOT}/build/screen.png")
    finally:
        vm.kill()


@test
def efi_usb_stick():
    """USB メモリから UEFI で起動し、同じ USB メモリ（FAT）の minios-data を /data にして、再起動後も残る"""
    if not efi_image_fresh():
        return
    import shutil, tempfile
    with tempfile.TemporaryDirectory() as tmp:
        esp = os.path.join(tmp, "esp")
        shutil.copytree(f"{ROOT}/build/efi", esp)
        # QEMU の仮想 FAT（vvfat）は、ゲストがディレクトリを作ると QEMU ごと落ちる（handle_renames_and_mkdirs の assert）。
        # ディレクトリはホスト側で作っておき、ゲストはファイルだけ書く。本物の FAT なら mkdir も問題ない
        os.makedirs(os.path.join(esp, "minios-data", "bin"))
        vm = VM(efi=True, esp=esp, esp_on_usb=True)
        try:
            vm.expect(r"\[ OK \] Mounted /data \(/dev/sda1 minios-data\)", timeout=120)
            vm.expect(PROMPT)
            out = cmd(vm, "ls /boot")
            assert re.search(r"^d +\d+  EFI/$", out, re.M) and re.search(r"^d +\d+  minios-data/$", out, re.M), out
            assert cmd(vm, "echo saved on usb > /data/note") == ""
            # プログラムを置いて実行できる（FAT に実行権限は無いが、全ファイルを 755 扱いでマウントしている）
            assert cmd(vm, "cat /bin/hello > /data/bin/hi") == ""
            assert cmd(vm, "chmod 755 /data/bin/hi") == ""
            assert cmd(vm, "trust hi").startswith("trusted /data/bin/hi sha256 ")
            assert cmd(vm, "hi on stick").endswith(" on stick env=3")
            vm.send("shutdown\n")
            vm.expect(r"Bye\.")
            assert vm.wait_exit(30) == 0
        finally:
            vm.kill()
        with open(os.path.join(esp, "minios-data", "note")) as f:  # ホストから見ても USB メモリに書かれている
            assert f.read() == "saved on usb\n"
        vm = VM(efi=True, esp=esp, esp_on_usb=True)
        try:
            vm.expect(r"\[ OK \] Mounted /data \(/dev/sda1 minios-data\)", timeout=120)
            vm.expect(PROMPT)
            assert cmd(vm, "cat /data/note") == "saved on usb"
            assert cmd(vm, "hi again").endswith(" again env=3")
        finally:
            vm.kill()


@test
def efi_real_nic_dhcp():
    """UEFI 版で、実機の NIC（QEMU が再現する Intel e1000e）を DHCP で設定し、ホストと HTTP で話せる"""
    if not efi_image_fresh():
        return
    with HostHTTPServer() as srv:
        vm = VM(efi=True, net=True, nic_model="e1000e")
        try:
            vm.expect(r"\[ OK \] Network eth0 10\.0\.2\.15 via 10\.0\.2\.2 \(DHCP\)", timeout=120)
            vm.expect(PROMPT)
            out = cmd(vm, "cat /sys/class/net/eth0/device/uevent")
            assert "DRIVER=e1000e" in out, out
            out = cmd(vm, f"fetch http://10.0.2.2:{srv.port}/index.html", timeout=30)
            assert out.rstrip() == "HTTP/1.0 200 OK\nhello from host", out
        finally:
            vm.kill()


@test
def efi_boot():
    """UEFI 版（build-efi.sh）を OVMF から起動し、ACPI で電源が切れる。イメージが古ければ飛ばす"""
    if not efi_image_fresh():
        return
    disk = new_disk()
    try:
        vm = VM(efi=True, disk=disk, net=True)
        try:
            vm.expect(r"minios", timeout=120)
            vm.expect(r"\[ OK \] Mounted /data \(/dev/vda\)")
            vm.expect(r"\[ OK \] Network eth0")
            vm.expect(PROMPT)
            out = cmd(vm, "ls /sys/firmware")
            assert out == "d        0  efi/\nd        0  acpi/", out
            out = cmd(vm, "ping 10.0.2.2 3", timeout=15)  # 1 回目は ARP を挟むので遅れることがある
            assert re.search(r"3 sent, [1-3] received$", out), out
            out = cmd(vm, "echo from efi > /data/f")
            assert out == "", out
            vm.send("shutdown\n")
            vm.expect(r"Bye\.")
            assert vm.wait_exit(30) == 0
        finally:
            vm.kill()
        vm = booted(disk)  # UEFI 版で書いたディスクを、テスト用の版で読む
        try:
            assert cmd(vm, "cat /data/f") == "from efi"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def fuzz_dhcp():
    """DHCP の応答の解析（dhcp_find）を、ホストで AddressSanitizer・UBSan つきでファジングする"""
    out = subprocess.run(["gcc", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                          "-Wno-unused-function", "-o", f"{ROOT}/build/fuzz_dhcp", f"{ROOT}/tools/fuzz_dhcp.c"],
                         capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    out = subprocess.run([f"{ROOT}/build/fuzz_dhcp"], capture_output=True, text=True, timeout=120)
    assert out.returncode == 0 and "no errors" in out.stdout, out.stdout + out.stderr


@test
def internet_dns_and_https():
    """インターネットに出る。MINIOS_INTERNET=1 のときだけ実行する"""
    if os.environ.get("MINIOS_INTERNET") != "1":
        print("  (skipped: set MINIOS_INTERNET=1)")
        return
    vm = booted(net=True)
    try:
        out = cmd(vm, "fetch https://example.com/", timeout=30)
        assert out.startswith("HTTP/2.0 200 OK") and "Example Domain" in out, out
    finally:
        vm.kill()


@test
def boot_script():
    """/data/rc に書いたコマンドが起動時に動く。OS をビルドし直さずに振る舞いを変えられる"""
    disk = new_disk()
    try:
        vm = booted(disk)
        try:
            assert cmd(vm, "echo echo hello from rc > /data/rc") == ""
            assert cmd(vm, "echo mkdir /tmp   # comment >> /data/rc") == ""
            assert cmd(vm, "cat /data/rc") == "echo hello from rc\nmkdir /tmp # comment"  # source で読むときにコメントとして捨てる
            vm.send("shutdown\n")
            assert vm.wait_exit() == 0
        finally:
            vm.kill()
        vm = VM(disk=disk)
        try:
            vm.expect(r"\[ OK \] Running /data/rc")
            vm.expect(r"hello from rc")
            vm.expect(PROMPT)
            assert re.search(r"^d +\d+  tmp/$", cmd(vm, "ls /"), re.M)
            # source は入れ子にでき、自分自身を読むと深さの上限で止まる
            assert cmd(vm, "echo source /data/loop > /data/loop") == ""
            assert cmd(vm, "source /data/loop") == "/data/loop: too many levels (loop?)"
            assert cmd(vm, "source /nosuch") == "/nosuch: no such file or directory"
        finally:
            vm.kill()
    finally:
        os.unlink(disk)


@test
def scratch_boot():
    """自作カーネルが 64 ビットで起動し、割り込み（タイマー、シリアルの受信）と例外を扱える"""
    out = subprocess.run([f"{ROOT}/kernel/build.sh"], capture_output=True, text=True)
    assert out.returncode == 0, out.stdout + out.stderr
    vm = ScratchVM()
    try:
        vm.expect(r"minios scratch kernel")
        vm.expect(r"memory: 255 MiB")
        vm.expect(r"interrupts on")
        vm.expect(r"type keys \(q to quit\)")
        vm.send("hello")                      # シリアルの受信割り込みで受け取り、そのまま返す
        vm.expect(r"hello")
        time.sleep(1.1)
        vm.send("q")
        out = vm.expect(r"bye")
        assert re.search(r"uptime (\d+)\.\ds", out) and int(re.search(r"uptime (\d+)", out).group(1)) >= 1, out  # タイマー割り込み
        assert vm.wait_exit() == 1  # カーネルが 0 を書いた
    finally:
        vm.kill()
    # 例外を起こして、原因と場所を出して止まることを確かめる
    for option, expected in (("test=ud2", r"\*\*\* exception: invalid opcode \(vector 6, "),
                             ("test=pagefault", r"\*\*\* exception: page fault \(vector 14, .*, address 0x0000008000000000\)")):
        vm = ScratchVM(append=option)
        try:
            vm.expect(expected)
            assert vm.wait_exit() == 7  # カーネルが 3 を書いた
        finally:
            vm.kill()


@test
def scratch_runs_linux_binary():
    """Linux 用にビルドした minios のプログラムが、変えずに自作カーネルで動く（ELF、ユーザーモード、Linux と同じシステムコール）"""
    subprocess.run([f"{ROOT}/kernel/build.sh"], check=True, capture_output=True)
    vm = ScratchVM(append="run=/bin/hello a b c")
    try:
        vm.expect(r"exec /bin/hello")
        vm.expect(r"hello from pid 1 \(parent 0\) a b c env=3")   # argv・envp・getpid・getppid・write
        vm.expect(r"\[init exited with 0\]")
        assert vm.wait_exit() == 1
    finally:
        vm.kill()
    vm = ScratchVM(append="run=/bin/false")
    try:
        vm.expect(r"\[init exited with 1\]")                    # exit_group の終了コード
    finally:
        vm.kill()
    vm = ScratchVM(append="run=/bin/nosuch")
    try:
        vm.expect(r"exec failed: 2")                               # ENOENT
    finally:
        vm.kill()


@test
def scratch_processes_match_linux():
    """fork・execve・wait4・nanosleep・スケジューラ: forktest の出力が Linux と同じになる（PID の番号は除く）"""
    subprocess.run([f"{ROOT}/kernel/build.sh"], check=True, capture_output=True)
    vm = booted()
    try:
        linux = cmd(vm, "forktest")
    finally:
        vm.kill()
    vm = ScratchVM(append="run=/bin/forktest")
    try:
        out = vm.expect(r"forktest: done")
        scratch = out[out.index("forktest: pid"):].replace("\r\n", "\n")
    finally:
        vm.kill()
    pids = lambda s: re.sub(r"pid \d+|parent \d+", "pid N", s)
    assert pids(linux) == pids(scratch), (linux, scratch)


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
