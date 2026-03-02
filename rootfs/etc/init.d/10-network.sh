#!/bin/sh
# ClawOS init script: Basic network setup
# /etc/claw/init.d/10-network.sh

echo "[init] configuring network..."

# Bring up loopback
ip link set lo up 2>/dev/null || ifconfig lo up 2>/dev/null

# Try DHCP on first ethernet interface
for iface in eth0 ens0 enp0s0; do
    if [ -d "/sys/class/net/$iface" ]; then
        ip link set "$iface" up 2>/dev/null
        udhcpc -i "$iface" -q -n 2>/dev/null || true
        break
    fi
done

echo "[init] network ready"
