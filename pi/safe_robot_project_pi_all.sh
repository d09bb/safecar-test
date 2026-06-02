#!/bin/bash
BASE=/home/a/safe_robot_project
cd $BASE

echo "[PI] set eth0 = 192.168.60.1 for AI-G"
sudo ifconfig eth0 192.168.60.1 netmask 255.255.255.0 up

echo "[PI] stop old Pi-side processes"

safe_kill() {
  PATTERN="$1"
  PIDS=$(pgrep -f "$PATTERN" 2>/dev/null || true)

  for PID in $PIDS; do
    if [ "$PID" != "$$" ]; then
      sudo kill -9 "$PID" 2>/dev/null || true
    fi
  done
}

safe_kill "safe_robot_project.py --role pi_perception"
safe_kill "safe_robot_project.py --role pi_vehicle"
safe_kill "safe_robot_project.py --role pi_frame"
safe_kill "safe_robot_project_pi_frame_stable.py"
safe_kill "safe_robot_project_vehicle_stable.py"
safe_kill "safe_robot_project_pi_perception_tof.py"

sudo fuser -k 5006/udp 6002/udp 2>/dev/null || true
sudo fuser -k /dev/video0 /dev/video1 /dev/video2 /dev/video3 2>/dev/null || true

echo "[PI] start perception relay: AI-G UDP 6002 -> PC 192.168.0.8:6002"
python3 -u safe_robot_project_pi_perception_tof.py \
  > /tmp/safe_robot_project_pi_perception.log 2>&1 &

echo "[PI] start vehicle motor receiver: UDP 5006"
sudo python3 -u safe_robot_project_vehicle_stable.py \
  > /tmp/safe_robot_project_pi_vehicle.log 2>&1 &

echo "[PI] start frame sender: /dev/video0 -> AI-G 192.168.60.2:7000"
sudo python3 -u safe_robot_project_pi_frame_stable.py \
  --aig-ip 192.168.60.2 \
  --aig-port 7000 \
  --dev /dev/video0 \
  --width 640 \
  --height 480 \
  --fps 8 \
  > /tmp/safe_robot_project_pi_frame.log 2>&1 &

echo "[PI] started."
echo "  tail -f /tmp/safe_robot_project_pi_perception.log"
echo "  tail -f /tmp/safe_robot_project_pi_vehicle.log"
echo "  tail -f /tmp/safe_robot_project_pi_frame.log"
