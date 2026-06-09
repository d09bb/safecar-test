#!/bin/sh

cd /home/root/safe_robot_project || exit 1

if [ -f ./robot_env.sh ]; then
  . ./robot_env.sh
fi

TOPST_PORT=${TOPST_PORT:-5005}
PC_TOPST_IP=${PC_TOPST_IP:-192.168.50.10}
CMD_PORT=${CMD_PORT:-5006}
TARGETS=${TARGETS:-0,1,2}

TOPST_SEND_HZ=${TOPST_SEND_HZ:-10}
TOPST_TTL_MS=${TOPST_TTL_MS:-2000}
TOPST_CENTER=${TOPST_CENTER:-320}
TOPST_DEADBAND=${TOPST_DEADBAND:-50}
TOPST_SPEED=${TOPST_SPEED:-40}
TOPST_REACH_AREA=${TOPST_REACH_AREA:-110000}
TOPST_REACH_COUNT=${TOPST_REACH_COUNT:-3}
TOPST_PERCEPTION_TIMEOUT_MS=${TOPST_PERCEPTION_TIMEOUT_MS:-2000}
TOPST_TARGET_LOST_HOLD_MS=${TOPST_TARGET_LOST_HOLD_MS:-250}

echo "[TOPST_NET] setup"
if [ -x ./safe_robot_project_net_topst_fixed.sh ]; then
  ./safe_robot_project_net_topst_fixed.sh
else
  echo "[TOPST_NET] WARN: network setup script not found"
fi

echo "[TOPST] stop old TOPST"
pkill -f "safe_robot_topst_clean.py" 2>/dev/null || true
pkill -f "safe_robot_project.py --role topst" 2>/dev/null || true

sleep 1

echo "[TOPST] safe_robot_project TOPST Supervisor start"
echo "[TOPST] listen=${TOPST_PORT}"
echo "[TOPST] cmd -> PC ${PC_TOPST_IP}:${CMD_PORT}"
echo "[TOPST] targets=${TARGETS}"
echo "[TOPST] reach_area=${TOPST_REACH_AREA}"

exec python3 -u safe_robot_project.py \
  --role topst \
  --listen-port "${TOPST_PORT}" \
  --pc-ip "${PC_TOPST_IP}" \
  --pc-port "${CMD_PORT}" \
  --targets "${TARGETS}" \
  --send-hz "${TOPST_SEND_HZ}" \
  --ttl-ms "${TOPST_TTL_MS}" \
  --center "${TOPST_CENTER}" \
  --deadband "${TOPST_DEADBAND}" \
  --speed "${TOPST_SPEED}" \
  --reach-area "${TOPST_REACH_AREA}" \
  --reach-count "${TOPST_REACH_COUNT}" \
  --perception-timeout-ms "${TOPST_PERCEPTION_TIMEOUT_MS}" \
  --target-lost-hold-ms "${TOPST_TARGET_LOST_HOLD_MS}"
