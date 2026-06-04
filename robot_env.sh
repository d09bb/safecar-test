#!/bin/bash

# ============================================================
# PC Gateway environment
# 시연 장소에서 IP가 바뀌면 이 파일만 수정
# ============================================================

# PC <-> TOPST wired network
export PC_TOPST_IP=${PC_TOPST_IP:-192.168.50.10}
export TOPST_IP=${TOPST_IP:-192.168.50.20}

# PC <-> Raspberry Pi Wi-Fi network
# Raspberry Pi wlan0 IP
export PI_IP=${PI_IP:-192.168.0.24}

# PC Wi-Fi IP
# Raspberry Pi ultrasonic/perception relay sends PERCEPTION to this IP.
export PC_IP=${PC_IP:-192.168.0.8}

# Ports
export PERCEPTION_PORT=${PERCEPTION_PORT:-6002}
export TOPST_PORT=${TOPST_PORT:-5005}
export CMD_PORT=${CMD_PORT:-5006}

# Mission target mask for optional controller keepalive
# bit0=ID0, bit1=ID1, bit2=ID2
# 7 means 0,1,2 all selected.
export TARGET_MASK=${TARGET_MASK:-7}
