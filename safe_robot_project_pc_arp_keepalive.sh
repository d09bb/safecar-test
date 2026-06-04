#!/bin/bash

IFACE="enx00e04c531f40"
TOPST_IP="192.168.50.20"

echo "[PC_ARP_KEEPALIVE] start IFACE=$IFACE TOPST=$TOPST_IP"

while true
do
  ping -c 1 -W 1 "$TOPST_IP" >/dev/null 2>&1

  if [ $? -ne 0 ]; then
    echo "[PC_ARP_KEEPALIVE] ping failed -> flush neigh and retry"
    sudo ip neigh flush dev "$IFACE" 2>/dev/null || true
    sleep 0.2
    ping -c 1 -W 1 "$TOPST_IP" >/dev/null 2>&1
  fi

  sleep 1
done
