#!/bin/bash

echo "[PC_NET] fix PC <-> TOPST network"

# 192.168.50.10이 기존에 여러 인터페이스에 붙어 있으면 제거
for IF in $(ip -o -4 addr show | awk '/192\.168\.50\.10/ {print $2}'); do
  echo "[PC_NET] remove old 192.168.50.10 from $IF"
  sudo ip addr del 192.168.50.10/24 dev "$IF" 2>/dev/null || true
done

# TOPST와 연결된 유선 인터페이스 자동 선택
# wlan, lo 제외하고 carrier=1인 인터페이스 우선
IFACE=""
for IF in $(ls /sys/class/net); do
  [ "$IF" = "lo" ] && continue
  echo "$IF" | grep -q "wlan" && continue

  if [ -f "/sys/class/net/$IF/carrier" ]; then
    CARRIER=$(cat /sys/class/net/$IF/carrier 2>/dev/null)
    if [ "$CARRIER" = "1" ]; then
      IFACE="$IF"
      break
    fi
  fi
done

if [ -z "$IFACE" ]; then
  echo "[PC_NET] ERROR: no wired carrier interface found"
  ip -br link
  exit 1
fi

echo "[PC_NET] selected interface: $IFACE"

sudo ip link set "$IFACE" up
sudo ip addr add 192.168.50.10/24 dev "$IFACE"
sudo ip route replace 192.168.50.0/24 dev "$IFACE" src 192.168.50.10

echo "[PC_NET] result:"
ip -br addr show "$IFACE"
ip route get 192.168.50.20 || true
