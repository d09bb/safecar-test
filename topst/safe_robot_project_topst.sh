#!/bin/sh
cd /home/root/safe_robot_project

./safe_robot_project_net_topst_fixed.sh

pkill -f "safe_robot_project.py --role topst" 2>/dev/null

echo "[TOPST] safe_robot_project TOPST Supervisor start"
python3 -u safe_robot_project.py \
  --role topst \
  --listen-port 5005 \
  --pc-ip 192.168.50.10 \
  --pc-port 5006 \
  --targets 2 \
  --auto-start \
   \
  --send-hz 10 \
  --speed 35 \
  --center 320 \
  --deadband 70 \
  --perception-timeout-ms 1000 \
  --target-lost-hold-ms 0 \
  --reach-area 999999 \
  --reach-count 999
