#!/bin/bash

IFACE="enx00e04c531f40"
PC_IP="192.168.50.10"
TOPST_IP="192.168.50.20"

echo "[PC_NET_WATCH] start IFACE=$IFACE"

while true
do
  if ! ip link show "$IFACE" >/dev/null 2>&1; then
    echo "[PC_NET_WATCH] ERROR: interface not found: $IFACE"
    sleep 2
    continue
  fi

  CARRIER=$(cat /sys/class/net/$IFACE/carrier 2>/dev/null || echo 0)

  if [ "$CARRIER" != "1" ]; then
    echo "[PC_NET_WATCH] carrier=0, wait cable/link"
    sudo ip link set "$IFACE" up 2>/dev/null || true
    sleep 2
    continue
  fi

  # 192.168.50.10이 없으면 다시 부여
  ip -o -4 addr show dev "$IFACE" | grep -q "$PC_IP"
  if [ $? -ne 0 ]; then
    echo "[PC_NET_WATCH] restore IP $PC_IP/24 on $IFACE"

    for IF in $(ip -o -4 addr show | awk '/192\.168\.50\.10/ {print $2}'); do
      sudo ip addr del 192.168.50.10/24 dev "$IF" 2>/dev/null || true
    done

    sudo ip link set "$IFACE" up
    sudo ip addr add "$PC_IP/24" dev "$IFACE" 2>/dev/null || true
  fi

  sudo ip route replace 192.168.50.0/24 dev "$IFACE" src "$PC_IP"

  ping -c 1 -W 1 "$TOPST_IP" >/dev/null 2>&1
  if [ $? -ne 0 ]; then
    echo "[PC_NET_WATCH] ping TOPST failed -> flush ARP"
    sudo ip neigh flush dev "$IFACE" 2>/dev/null || true
  fi

  sleep 2
done
