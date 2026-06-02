#!/bin/bash
set -e

cd "$(dirname "$0")/src"

aarch64-linux-gnu-gcc -O2 -static \
  -o ../runtime/safe_robot_project_aig \
  aig_net_perception.c

echo "[BUILD] output: aig/runtime/safe_robot_project_aig"
