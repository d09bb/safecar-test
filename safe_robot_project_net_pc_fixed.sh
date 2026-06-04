#!/bin/bash

IFACE="enx00e04c531f40"
PC_IP="192.168.50.10"
TOPST_IP="192.168.50.20"

echo "[PC_NET] set PC <-> TOPST network"
echo "[PC_NET] iface=$IFACE"

# 1. 인터페이스 확인
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  echo "[PC_NET] ERROR: interface not found: $IFACE"
  ip -br link
  exit 1
fi

# 2. 기존 192.168.50.10 중복 제거
for IF in $(ip -o -4 addr show | awk '/192\.168\.50\.10/ {print $2}'); do
  echo "[PC_NET] remove old 192.168.50.10 from $IF"
  sudo ip addr del 192.168.50.10/24 dev "$IF" 2>/dev/null || true
done

# 3. 인터페이스 UP + IP 부여
sudo ip link set "$IFACE" up
sudo ip addr add "$PC_IP/24" dev "$IFACE" 2>/dev/null || true

# 4. route 고정
sudo ip route replace 192.168.50.0/24 dev "$IFACE" src "$PC_IP"

# 5. ARP/neigh 캐시 강제 초기화
echo "[PC_NET] flush ARP/neigh cache"
sudo ip neigh flush dev "$IFACE" 2>/dev/null || true
sudo ip neigh del "$TOPST_IP" dev "$IFACE" 2>/dev/null || true

# 6. 확인
echo "[PC_NET] result:"
ip -br addr show "$IFACE"
ip route get "$TOPST_IP"

echo "[PC_NET] ping TOPST"
ping -c 3 "$TOPST_IP"

echo "[PC_NET] neigh table:"
ip neigh show dev "$IFACE"
