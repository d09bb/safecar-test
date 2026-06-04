#!/bin/bash
set -e

cd "${SAFE_ROBOT_DIR:-$HOME/safe_robot_project}"

if [ -f ./robot_env.sh ]; then
  source ./robot_env.sh
fi

PC_TOPST_IP=${PC_TOPST_IP:-192.168.50.10}
TOPST_IP=${TOPST_IP:-192.168.50.20}
PI_IP=${PI_IP:-192.168.0.24}
PC_IP=${PC_IP:-192.168.0.8}

PERCEPTION_PORT=${PERCEPTION_PORT:-6002}
TOPST_PORT=${TOPST_PORT:-5005}
CMD_PORT=${CMD_PORT:-5006}
TARGET_MASK=${TARGET_MASK:-7}

echo "[PC] safe_robot_project PC Gateway start"
echo "[PC] PC_TOPST_IP = ${PC_TOPST_IP}"
echo "[PC] TOPST       = ${TOPST_IP}:${TOPST_PORT}"
echo "[PC] PI          = ${PI_IP}:${CMD_PORT}"
echo "[PC] PC_WIFI_IP  = ${PC_IP}"
echo "[PC] target_mask = ${TARGET_MASK}"

if [ -x ./safe_robot_project_net_pc.sh ]; then
  ./safe_robot_project_net_pc.sh
else
  echo "[PC] WARN: safe_robot_project_net_pc.sh not executable or not found"
fi

sudo fuser -k ${CMD_PORT}/udp ${PERCEPTION_PORT}/udp 2>/dev/null || true
pkill -f "safe_robot_project.py --role pc_gateway" 2>/dev/null || true

python3 -u safe_robot_project.py \
  --role pc_gateway \
  --perception-port "${PERCEPTION_PORT}" \
  --cmd-port "${CMD_PORT}" \
  --topst-ip "${TOPST_IP}" \
  --topst-port "${TOPST_PORT}" \
  --pi-ip "${PI_IP}" \
  --pi-cmd-port "${CMD_PORT}" \
  --target-mask "${TARGET_MASK}"
