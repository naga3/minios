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
# KVM が使えれば使う（ローカル LLM を動かすには必須）。使えなければソフトウェアエミュレーション
ACCEL = ("-accel", "kvm", "-cpu", "host") if os.access("/dev/kvm", os.R_OK | os.W_OK) else ("-accel", "tcg")
ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


class Timeout(Exception):
    pass


class VM:
    def __init__(self, append="console=ttyS0 loglevel=0", disk=None, net=False, efi=False, usb_kbd=False,
                 esp=None, esp_on_usb=False, nic_model="virtio-net-pci", extra=()):
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


TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


# ---- テスト ----

PROMPT = r"(?m)^minios# "  # 行頭だけ。画面の中身（screen の出力）に含まれる "minios# " と取り違えないように


def booted(disk=None, net=False):
    """起動してプロンプトが出た VM を返す"""
    vm = VM(disk=disk, net=net)
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
        import datetime
        guest = datetime.datetime.strptime(cmd(vm, "date"), "%Y-%m-%d %H:%M:%S UTC").replace(tzinfo=datetime.timezone.utc)
        assert abs((guest - datetime.datetime.now(datetime.timezone.utc)).total_seconds()) < 10, guest
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
def gobox_links():
    """Go のプログラムは gobox 1 本にまとめ、/bin の名前はシンボリックリンク"""
    vm = booted()
    try:
        out = cmd(vm, "ls /bin")
        assert re.search(r"^l +\d+  fetch$", out, re.M) and re.search(r"^l +\d+  gohello$", out, re.M), out
        assert cmd(vm, "gobox") == "usage: gobox <command> [args...]\ncommands: [fetch gohello]\n[exit 2]"
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
