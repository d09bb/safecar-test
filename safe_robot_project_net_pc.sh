#!/bin/bash
set -e

cd "${SAFE_ROBOT_DIR:-$HOME/safe_robot_project}"

if [ -f ./robot_env.sh ]; then
  source ./robot_env.sh
fi

PC_TOPST_IP=${PC_TOPST_IP:-192.168.50.10}
TOPST_IP=${TOPST_IP:-192.168.50.20}
TOPST_NET_CIDR=${TOPST_NET_CIDR:-192.168.50.0/24}

echo "[PC_NET] setup PC <-> TOPST network"
echo "[PC_NET] PC_TOPST_IP=${PC_TOPST_IP}"
echo "[PC_NET] TOPST_IP=${TOPST_IP}"

ESCAPED_IP=$(echo "$PC_TOPST_IP" | sed 's/\./\\./g')

for IF in $(ip -o -4 addr show | awk "/${ESCAPED_IP}/ {print \$2}"); do
  echo "[PC_NET] remove old ${PC_TOPST_IP} from $IF"
  sudo ip addr del "${PC_TOPST_IP}/24" dev "$IF" 2>/dev/null || true
done

IFACE=""

# Prefer wired interface with carrier=1, excluding lo and wlan.
for IF in $(ls /sys/class/net); do
  [ "$IF" = "lo" ] && continue
  echo "$IF" | grep -q "wlan" && continue
  echo "$IF" | grep -q "wl" && continue

  if [ -f "/sys/class/net/$IF/carrier" ]; then
    CARRIER=$(cat "/sys/class/net/$IF/carrier" 2>/dev/null || echo 0)
    if [ "$CARRIER" = "1" ]; then
      IFACE="$IF"
      break
    fi
  fi
done

if [ -z "$IFACE" ]; then
  echo "[PC_NET] ERROR: no wired carrier interface found"
  echo "[PC_NET] current interfaces:"
  ip -br link
  exit 1
fi

echo "[PC_NET] selected interface: $IFACE"

sudo ip link set "$IFACE" up
sudo ip addr add "${PC_TOPST_IP}/24" dev "$IFACE" 2>/dev/null || true
sudo ip route replace "${TOPST_NET_CIDR}" dev "$IFACE" src "${PC_TOPST_IP}"

echo "[PC_NET] result:"
ip -br addr show "$IFACE"
ip route get "$TOPST_IP" || true
